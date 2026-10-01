/* wave_synth.cpp -- Munchi Wave as a Schwung sound generator.
 *
 * The same CHOMPI WAVE 1.0 engine the standalone tool runs (engine/wave/),
 * as a plugin_api_v2 synth for a Signal Chain slot: Move (or a sequencer, or
 * a keyboard) plays it, the knob grid edits it, the slot keeps its state.
 *
 *   engine     myEngine at its native 48 kHz, resampled down to 44.1 kHz
 *   presets    the card's presets.json (WAVE's 14 slots) plus the defaults
 *   tables     the first seven .wav files of the card: the user's Munchi Wave
 *              card if it has any (so the tool and the synth share tables and
 *              presets), else the factory card shipped beside dsp.so
 *
 * Not here: the step sequencer, the CHOMPI's keyboard pages and the preset
 * editing keys. In a chain, Move sequences and the host saves state.
 *
 * Threading (docs/MODULES.md "there is no control thread"): every entry point
 * runs on the SPI callback. create_instance allocates only the instance and
 * starts a loader thread, which demotes itself to SCHED_OTHER on cores 0-2,
 * allocates the delay, reverb and table memory, reads the card and initialises
 * the engine, then publishes `loaded`. Until then the synth is silent and
 * parameter writes are only stored; the first render after the load applies
 * them.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sched.h>
#include <strings.h>
#include <atomic>
#include <new>
#include <string>
#include <thread>

#include "plugin_api_v1.h"
#include "../engine/daisy_compat.h"
#include "../engine/wave/subtractiveEngine.h"
#include "../engine/resampler.h"
#include "../engine/wave_card.h"

uint32_t daisy::System::now_ms = 0;

using munchi::WavePresets;

static const host_api_v1_t *g_host = nullptr;
static const char *kUserCard = "/data/UserData/UserLibrary/Samples/Schwung/Munchi Wave";

/* ---- parameters ----------------------------------------------------------
 * Values are kept in display units (seconds, semitones, 1-based indices);
 * engine values are derived from them in Apply(). The defaults are WAVE's
 * (ui.h enc_defaults and MenuPage::Init). */
enum
{
    K_TABLE, K_FRAME, K_FILTER, K_RES, K_ATTACK, K_RELEASE, K_SPACE, K_VOLUME,
    K_PITCH, K_PLFO, K_PLFO_RATE, K_PLFO_ON, K_FLFO, K_FLFO_RATE, K_FLFO_ON, K_OCTAVE,
    K_DTIME, K_PAN, K_COMP, K_BEND,
    K_COUNT
};

struct ParamDef
{
    const char *key, *name;
    float       min, max, def, step;
    const char *unit; // nullptr: none; "enum": Off/On
};

static const ParamDef kDefs[K_COUNT] = {
    {"table", "Table", 1, 7, 1, 1, nullptr},
    {"frame", "Frame", 1, 33, 1, 1, nullptr},
    {"filter", "Filter", 0, 1, .5f, .01f, "%"},
    {"resonance", "Resonance", 0, 1, .63f, .01f, "%"},
    {"attack", "Attack", 0, 5, 0, .01f, "sec"},
    {"release", "Release", 0, 1, 0, .01f, "sec"},
    {"space", "Space", -1, 1, 0, .02f, nullptr},
    {"volume", "Volume", 0, 1, .84f, .01f, "%"},
    {"pitch", "Pitch", -12, 12, 0, .1f, "st"},
    {"pitch_lfo", "Pitch LFO", 0, 1, 0, .01f, "%"},
    {"pitch_lfo_rate", "P LFO Rate", 0, 1, .58f, .01f, "%"},
    {"pitch_lfo_on", "P LFO", 0, 1, 1, 1, "enum"},
    {"filter_lfo", "Filter LFO", 0, 1, 0, .01f, "%"},
    {"filter_lfo_rate", "F LFO Rate", 0, 1, .58f, .01f, "%"},
    {"filter_lfo_on", "F LFO", 0, 1, 1, 1, "enum"},
    {"octave", "Octave", -1, 1, 0, 1, nullptr},
    {"delay_time", "Delay Time", 0, 1, .4f, .01f, "%"},
    {"pan", "Pan", -1, 1, 0, .02f, nullptr},
    {"comp", "Comp", 0, 1, 0, .01f, "%"},
    {"bend_range", "Bend Range", 0, 12, 2, 1, "st"},
};

/* ---- instance -------------------------------------------------------------- */

struct WaveSynth
{
    myEngine                               engine;
    wavetableLoader                        wt;
    WavePresets                            presets;
    daisysp::Reverb                       *reverb  = nullptr;
    chompi::InterpolatedDelayLine::AudioSample *del_mem = nullptr;
    float (*tables)[MAX_SAMPLES_PER_CYCLE] = nullptr;

    std::string       module_dir;
    std::thread       loader;
    std::atomic<bool> loaded{false};
    bool              applied = false;
    int               tables_loaded = 0;

    float p[K_COUNT];
    float bend    = 0.f; // -1..1
    int   octave_applied = 0;
    int   preset  = 0;   // index into the browser list: 0 = defaults
    int   pending_preset = -1; // chosen while the card was still loading
    int   preset_slots[16]; // browser index -> WAVE slot (15 = defaults)
    int   preset_count = 1;

    munchi::StreamResampler down;
    static constexpr int kBlock = 48;
    float out48[4][kBlock];
    float out44_l[512], out44_r[512];
    int   out44_count = 0;
};

static void loader_main(WaveSynth *s)
{
#ifdef __linux__
    struct sched_param sp = {};
    sched_setscheduler(0, SCHED_OTHER, &sp); // first: we inherited FIFO 70
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(0, &set);
    CPU_SET(1, &set);
    CPU_SET(2, &set);
    sched_setaffinity(0, sizeof(set), &set);
#endif
    s->reverb  = new(std::nothrow) daisysp::Reverb();
    s->del_mem = (chompi::InterpolatedDelayLine::AudioSample *)calloc(
        kMaxDelayTime, sizeof(chompi::InterpolatedDelayLine::AudioSample));
    s->tables = (float(*)[MAX_SAMPLES_PER_CYCLE])calloc(
        wavetableLoader::kMaxPreload * CYCLES, sizeof(float) * MAX_SAMPLES_PER_CYCLE);
    if(!s->reverb || !s->del_mem || !s->tables)
        return; // stays silent

    // the user's card first (shared with the Munchi Wave tool), then the
    // factory card in the module folder
    std::string dir = kUserCard;
    int         n   = munchi::LoadWavetableFolder(dir, s->tables, wavetableLoader::kMaxPreload);
    if(n == 0)
    {
        dir = s->module_dir + "/card";
        n   = munchi::LoadWavetableFolder(dir, s->tables, wavetableLoader::kMaxPreload);
    }
    s->tables_loaded = n;

    float defaults[14] = {.5f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, .5f, .58f, .5f, .63f, .58f, .4f, 3.f};
    s->presets.Init(defaults);
    if(FILE *f = fopen((dir + "/presets.json").c_str(), "rb"))
    {
        std::string json;
        char        buf[4096];
        size_t      r;
        while((r = fread(buf, 1, sizeof(buf), f)) > 0)
            json.append(buf, r);
        fclose(f);
        s->presets.Parse(json.c_str());
    }
    s->preset_slots[0] = 15;
    s->preset_count    = 1;
    for(int slot = 1; slot <= 14; slot++)
        if(s->presets.IsValid(slot))
            s->preset_slots[s->preset_count++] = slot;

    s->wt.Init(s->tables);
    s->wt.numWavetables = (int16_t)n;
    s->engine.Init(48000.f, s->del_mem, s->reverb, &s->wt);
    s->down.Init(48000.0, 44100.0);
    s->loaded.store(true, std::memory_order_release);
}

/* ---- applying parameters --------------------------------------------------- */

static void apply_pitch(WaveSynth *s)
{
    // .5 = no change; the engine maps 0..1 to -1..+1 octave
    float st = s->p[K_PITCH] + s->bend * s->p[K_BEND];
    s->engine.setGlobalPitch(daisysp::fclamp(.5f + st / 24.f, 0.f, 1.f));
}

static void apply(WaveSynth *s, int k)
{
    myEngine &e = s->engine;
    float     v = s->p[k];
    switch(k)
    {
        case K_TABLE: e.nextTable((int8_t)(lrintf(v) - 1), true); break;
        case K_FRAME: e.setCycle((int8_t)(lrintf(v) - 1), true); break;
        case K_FILTER: e.setMasterCutoff(v); break;
        case K_RES: e.setMasterResonance(v); break;
        case K_ATTACK: e.setAttack(v / 5.f); break;
        case K_RELEASE: e.setRelease(v); break;
        case K_SPACE: e.setDelayFeedback((v + 1.f) * .5f); break;
        case K_VOLUME: e.setGain(v); break;
        case K_PITCH:
        case K_BEND: apply_pitch(s); break;
        case K_PLFO: e.setPitchLfoDepth(v); break;
        case K_PLFO_RATE: e.setPitchLfoRate(v); break;
        case K_PLFO_ON: e.setPitchLfoOn(v > .5f); break;
        case K_FLFO: e.setLfoDepth(v); break;
        case K_FLFO_RATE: e.setLfoRate(v); break;
        case K_FLFO_ON: e.setFilterLfoOn(v > .5f); break;
        case K_OCTAVE:
        {
            // setOctave() is relative (the CHOMPI's two keys); step to the value
            int want = (int)lrintf(v);
            e.setOctave(want - s->octave_applied);
            s->octave_applied = e.getOctave();
            break;
        }
        case K_DTIME: e.setDelayTime(v); break;
        case K_PAN: e.setPan((v + 1.f) * .5f); break;
        case K_COMP: e.setFinalComp(v); break;
    }
}

static void set_value(WaveSynth *s, int k, float v)
{
    const ParamDef &d = kDefs[k];
    if(d.step >= 1.f)
        v = roundf(v);
    s->p[k] = daisysp::fclamp(v, d.min, d.max);
    if(s->applied)
        apply(s, k);
}

/* MenuPage::SetVoiceSlot: a WAVE preset slot (1-14) or the defaults (15) */
static void load_preset(WaveSynth *s, int index)
{
    if(index < 0 || index >= s->preset_count)
        return;
    s->preset      = index;
    const int slot = s->preset_slots[index];
    float     c[14];
    if(slot == 15)
    {
        const float d[14] = {.5f, 0, 0, 0, 0, 0, 0, .5f, .58f, .5f, .63f, .58f, .4f, 3};
        memcpy(c, d, sizeof(c));
    }
    else
        for(int i = 0; i < 14; i++)
            c[i] = s->presets.GetValue(slot, i);
    set_value(s, K_PITCH, (c[0] - .5f) * 24.f);
    set_value(s, K_FRAME, c[1] + 1.f);
    set_value(s, K_TABLE, c[2] + 1.f);
    set_value(s, K_ATTACK, c[3] * 5.f);
    set_value(s, K_PLFO, c[4]);
    set_value(s, K_RELEASE, c[5]);
    set_value(s, K_FLFO, c[6]);
    set_value(s, K_FILTER, c[7]);
    set_value(s, K_PLFO_RATE, c[8]);
    set_value(s, K_SPACE, c[9] * 2.f - 1.f);
    set_value(s, K_RES, c[10]);
    set_value(s, K_FLFO_RATE, c[11]);
    set_value(s, K_DTIME, c[12]);
    int t = (int)lrintf(c[13]);
    set_value(s, K_PLFO_ON, (t & 1) ? 1.f : 0.f);
    set_value(s, K_FLFO_ON, (t & 2) ? 1.f : 0.f);
}

static int find_key(const char *key)
{
    for(int k = 0; k < K_COUNT; k++)
        if(!strcmp(kDefs[k].key, key))
            return k;
    return -1;
}

static bool json_number(const char *json, const char *key, float *out)
{
    char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if(!p)
        return false;
    p += strlen(pat);
    while(*p == ' ' || *p == ':' || *p == '"')
        p++;
    char *end;
    float v = strtof(p, &end);
    if(end == p)
        return false;
    *out = v;
    return true;
}

/* ---- plugin API ------------------------------------------------------------ */

static void *v2_create_instance(const char *module_dir, const char *json_defaults)
{
    (void)json_defaults;
    WaveSynth *s = new(std::nothrow) WaveSynth();
    if(!s)
        return nullptr;
    s->module_dir = module_dir ? module_dir : ".";
    for(int k = 0; k < K_COUNT; k++)
        s->p[k] = kDefs[k].def;
    s->preset_slots[0] = 15;
    s->loader          = std::thread(loader_main, s);
    return s;
}

static void v2_destroy_instance(void *instance)
{
    WaveSynth *s = (WaveSynth *)instance;
    if(!s)
        return;
    if(s->loader.joinable())
        s->loader.join();
    delete s->reverb;
    free(s->del_mem);
    free(s->tables);
    delete s;
}

static void v2_on_midi(void *instance, const uint8_t *msg, int len, int source)
{
    (void)source;
    WaveSynth *s = (WaveSynth *)instance;
    if(!s || len < 1 || !s->applied)
        return;
    const uint8_t type = msg[0] & 0xF0;
    if((type == 0x90 || type == 0x80) && len >= 3)
    {
        const int  note = msg[1] & 127;
        const bool on   = type == 0x90 && msg[2] > 0;
        // transpose 0 is C3 (MIDI 48) in the engine's keyToFrequency
        s->engine.request_fifo.PushBack(KeyRequest(on ? KeyRequest::Type::START : KeyRequest::Type::STOP,
                                                   (float)(note - 48), note, on ? (float)msg[2] : 127.f));
    }
    else if(type == 0xE0 && len >= 3)
    {
        int v   = (msg[1] & 127) | ((msg[2] & 127) << 7);
        s->bend = (v - 8192) / 8192.f;
        apply_pitch(s);
    }
    else if(type == 0xB0 && len >= 3 && (msg[1] == 120 || msg[1] == 123))
        s->engine.stopAllVoices();
}

static void v2_set_param(void *instance, const char *key, const char *val)
{
    WaveSynth *s = (WaveSynth *)instance;
    if(!s || !key || !val)
        return;
    if(!strcmp(key, "state"))
    {
        float v;
        if(json_number(val, "preset", &v))
            s->preset = (int)v; // names the browser row; the values follow
        for(int k = 0; k < K_COUNT; k++)
            if(json_number(val, kDefs[k].key, &v))
                set_value(s, k, v);
        return;
    }
    if(!strcmp(key, "preset"))
    {
        if(s->loaded.load(std::memory_order_acquire))
            load_preset(s, atoi(val));
        else
            s->pending_preset = atoi(val);
        return;
    }
    if(!strcmp(key, "all_notes_off"))
    {
        if(s->applied)
            s->engine.stopAllVoices();
        return;
    }
    int k = find_key(key);
    if(k < 0)
        return;
    float v;
    if(kDefs[k].unit && !strcmp(kDefs[k].unit, "enum") && !(val[0] >= '0' && val[0] <= '9'))
        v = !strcasecmp(val, "on") ? 1.f : 0.f;
    else
        v = strtof(val, nullptr);
    set_value(s, k, v);
}

static int put(char *buf, int len, const std::string &s)
{
    if((int)s.size() >= len)
        return -1;
    memcpy(buf, s.c_str(), s.size() + 1);
    return (int)s.size();
}

static int v2_get_param(void *instance, const char *key, char *buf, int buf_len)
{
    WaveSynth *s = (WaveSynth *)instance;
    if(!s || !key)
        return -1;
    const bool loaded = s->loaded.load(std::memory_order_acquire);

    if(!strcmp(key, "name"))
        return snprintf(buf, buf_len, "Munchi Wave");
    if(!strcmp(key, "preset"))
        return snprintf(buf, buf_len, "%d", s->preset);
    if(!strcmp(key, "preset_count"))
        return snprintf(buf, buf_len, "%d", loaded ? s->preset_count : 1);
    if(!strcmp(key, "preset_name"))
    {
        int slot = loaded && s->preset < s->preset_count ? s->preset_slots[s->preset] : 15;
        if(slot == 15)
            return snprintf(buf, buf_len, "Default Wave");
        return snprintf(buf, buf_len, "Preset %d", slot);
    }

    int k = find_key(key);
    if(k >= 0)
    {
        const ParamDef &d = kDefs[k];
        if(d.step >= 1.f)
            return snprintf(buf, buf_len, "%d", (int)lrintf(s->p[k]));
        return snprintf(buf, buf_len, "%.3f", s->p[k]);
    }

    if(!strcmp(key, "ui_hierarchy"))
    {
        return put(buf, buf_len,
            "{\"modes\":null,\"levels\":{"
            "\"root\":{\"name\":\"Munchi Wave\","
            "\"list_param\":\"preset\",\"count_param\":\"preset_count\",\"name_param\":\"preset_name\","
            "\"knobs\":[\"table\",\"frame\",\"filter\",\"resonance\",\"attack\",\"release\",\"space\",\"volume\"],"
            "\"params\":[\"table\",\"frame\",\"filter\",\"resonance\",\"attack\",\"release\",\"space\",\"volume\","
            "{\"level\":\"mod\",\"label\":\"Pitch + LFOs\"},{\"level\":\"out\",\"label\":\"Output\"}]},"
            "\"mod\":{\"name\":\"Pitch + LFOs\","
            "\"knobs\":[\"pitch\",\"pitch_lfo\",\"pitch_lfo_rate\",\"pitch_lfo_on\","
            "\"octave\",\"filter_lfo\",\"filter_lfo_rate\",\"filter_lfo_on\"],"
            "\"params\":[\"pitch\",\"pitch_lfo\",\"pitch_lfo_rate\",\"pitch_lfo_on\","
            "\"octave\",\"filter_lfo\",\"filter_lfo_rate\",\"filter_lfo_on\"]},"
            "\"out\":{\"name\":\"Output\","
            "\"knobs\":[\"delay_time\",\"pan\",\"comp\",\"bend_range\"],"
            "\"params\":[\"delay_time\",\"pan\",\"comp\",\"bend_range\"]}"
            "}}");
    }

    if(!strcmp(key, "chain_params"))
    {
        std::string out = "[";
        char        tmp[256];
        for(int i = 0; i < K_COUNT; i++)
        {
            const ParamDef &d = kDefs[i];
            int max = d.max;
            if(i == K_TABLE)
                max = loaded && s->tables_loaded > 0 ? s->tables_loaded : 7;
            if(d.unit && !strcmp(d.unit, "enum"))
                snprintf(tmp, sizeof(tmp),
                         "%s{\"key\":\"%s\",\"name\":\"%s\",\"type\":\"enum\","
                         "\"options\":[\"Off\",\"On\"],\"default\":%d}",
                         i ? "," : "", d.key, d.name, (int)d.def);
            else if(d.step >= 1.f)
                snprintf(tmp, sizeof(tmp),
                         "%s{\"key\":\"%s\",\"name\":\"%s\",\"type\":\"int\","
                         "\"min\":%d,\"max\":%d,\"default\":%d%s%s%s}",
                         i ? "," : "", d.key, d.name, (int)d.min, max, (int)d.def,
                         d.unit ? ",\"unit\":\"" : "", d.unit ? d.unit : "", d.unit ? "\"" : "");
            else
                snprintf(tmp, sizeof(tmp),
                         "%s{\"key\":\"%s\",\"name\":\"%s\",\"type\":\"float\","
                         "\"min\":%g,\"max\":%g,\"default\":%g,\"step\":%g%s%s%s}",
                         i ? "," : "", d.key, d.name, d.min, d.max, d.def, d.step,
                         d.unit ? ",\"unit\":\"" : "", d.unit ? d.unit : "", d.unit ? "\"" : "");
            out += tmp;
        }
        out += "]";
        return put(buf, buf_len, out);
    }

    if(!strcmp(key, "state"))
    {
        std::string out = "{\"v\":1";
        char        tmp[64];
        snprintf(tmp, sizeof(tmp), ",\"preset\":%d", s->preset);
        out += tmp;
        for(int k2 = 0; k2 < K_COUNT; k2++)
        {
            snprintf(tmp, sizeof(tmp), ",\"%s\":%.4f", kDefs[k2].key, s->p[k2]);
            out += tmp;
        }
        out += "}";
        return put(buf, buf_len, out);
    }
    return -1;
}

static int v2_get_error(void *instance, char *buf, int buf_len)
{
    WaveSynth *s = (WaveSynth *)instance;
    if(s && s->loaded.load(std::memory_order_acquire) && s->tables_loaded == 0)
        return snprintf(buf, buf_len, "No wavetables: the card folder is empty");
    return 0;
}

static void run_engine_block(WaveSynth *s)
{
    // MidiManager fed one request per AudioCallback; a chain delivers a
    // chord in one block, so drain them all
    while(!s->engine.request_fifo.IsEmpty())
        s->engine.Prepare();
    static const float zero[WaveSynth::kBlock] = {0};
    const float *ins[4]  = {zero, zero, zero, zero};
    float       *outs[4] = {s->out48[0], s->out48[1], s->out48[2], s->out48[3]};
    s->engine.Process(ins, outs, WaveSynth::kBlock);
    // the same trim as the standalone tool (munchi_wave.cpp RunEngineBlock):
    // the engine averages eight voices and the CHOMPI's analog stage made up
    // the rest
    const float kOutputTrim = 4.f;
    for(int i = 0; i < WaveSynth::kBlock; i++)
        s->down.Push(daisysp::SoftLimit(s->out48[0][i] * kOutputTrim),
                     daisysp::SoftLimit(s->out48[1][i] * kOutputTrim));
    while(s->down.CanPull() && s->out44_count < 512)
    {
        s->down.Pull(&s->out44_l[s->out44_count], &s->out44_r[s->out44_count]);
        s->out44_count++;
    }
    daisy::System::now_ms++;
}

static void v2_render_block(void *instance, int16_t *out, int frames)
{
    WaveSynth *s = (WaveSynth *)instance;
    if(!s || !s->loaded.load(std::memory_order_acquire))
    {
        memset(out, 0, frames * 4);
        return;
    }
    if(!s->applied)
    {
        s->applied = true;
        for(int k = 0; k < K_COUNT; k++)
            apply(s, k);
        if(s->pending_preset >= 0)
            load_preset(s, s->pending_preset);
    }
    int guard = 0;
    while(s->out44_count < frames && guard++ < 16)
        run_engine_block(s);
    int n = frames < s->out44_count ? frames : s->out44_count;
    for(int i = 0; i < n; i++)
    {
        out[i * 2]     = (int16_t)f2s16(s->out44_l[i]);
        out[i * 2 + 1] = (int16_t)f2s16(s->out44_r[i]);
    }
    for(int i = n; i < frames; i++)
        out[i * 2] = out[i * 2 + 1] = 0;
    memmove(s->out44_l, s->out44_l + n, (s->out44_count - n) * sizeof(float));
    memmove(s->out44_r, s->out44_r + n, (s->out44_count - n) * sizeof(float));
    s->out44_count -= n;
}

static plugin_api_v2_t g_api;

extern "C" __attribute__((visibility("default"))) plugin_api_v2_t *
move_plugin_init_v2(const host_api_v1_t *host)
{
    g_host = host;
    memset(&g_api, 0, sizeof(g_api));
    g_api.api_version      = 2;
    g_api.create_instance  = v2_create_instance;
    g_api.destroy_instance = v2_destroy_instance;
    g_api.on_midi          = v2_on_midi;
    g_api.set_param        = v2_set_param;
    g_api.get_param        = v2_get_param;
    g_api.get_error        = v2_get_error;
    g_api.render_block     = v2_render_block;
    return &g_api;
}
