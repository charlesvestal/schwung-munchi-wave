/* munchi_wave.cpp -- see munchi_wave.h.
 *
 * The control code is NormalPage.h, MenuPage.h, ui.h and MidiManager.h from
 * CHOMPI WAVE 1.0 (originals in wave/upstream/), ported with their structure
 * intact. LEDs are surface.cpp's; what the pages' Draw() did to the ENGINE is
 * done here, on change and in UiTick().
 */
#include "munchi_wave.h"
#include "wave_card.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sched.h>
#include <algorithm>
#include <chrono>
#include <vector>

uint32_t daisy::System::now_ms = 0;

namespace munchi
{

/* ---- firmware tables ----------------------------------------------------- */

// NormalPage.h
static const uint8_t cc_map[3][6] = {{20, 21, 22, 23, 24, 25},
                                     {26, 27, 28, 29, 0, 30},
                                     {0, 0, 0, 0, 0, 0}};
static const uint8_t key_map[40] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x15, 0x00, 0x31, 0x32, 0x34,
    0x35, 0x37, 0x33, 0x36, 0x38, 0x30, 0x39, 0x3b, 0x3c, 0x3e,
    0x40, 0x3a, 0x3d, 0x3f, 0x41, 0x43, 0x45, 0x47, 0x48, 0x42,
    0x44, 0x46, 0x05, 0x17, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00,
};
static const float kEncoderFineStep     = .003f;
static const float kEncoderTempoStep    = .003125f;
static const float kEncoderCoarseStep   = .01f;
static const float kEncoderEnvCoarseStep = .03f;
static const float kEncoderCycleStep    = 1.f / 33.f;

// ui.h
static const float enc_defaults[3][6] = {
    {.5f, 0.f, 0.f, .5f, .5f, .84f},
    {.0f, 0.f, 0.f, .5f, 0.f, .5f},
    {0.f, 0.f, 0.f, 0.f, 0.f, 0.f},
};
static const uint8_t midi2key[49] = {
    40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,
    15, 7,  8,  12, 9,  10, 13, 11, 14, 16, 21, 17,
    18, 22, 19, 23, 20, 24, 29, 25, 30, 26, 31, 27, 28,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63};

int KeyIdToNote(int id)
{
    if(id < KEY_16 || id > KEY_25)
        return -1;
    return key_map[id];
}

int NoteToKeyId(int note)
{
    int k = note - 36;
    if(k < 12 || k > 36)
        return -1;
    return midi2key[k];
}

int KeyToSlot(int buttonID) // MenuPage::KeyToSlot
{
    if(buttonID == 7)
        return kSlotNone;
    else if(buttonID < 12)
        return buttonID - 6;
    else if(buttonID < 15)
        return kSlotNone;
    else if(buttonID == 15)
        return 1;
    else if(buttonID < 21)
        return buttonID - 10;
    else if(buttonID < 24)
        return kSlotNone;
    else if(buttonID < 29)
        return buttonID - 13;
    return kSlotNone;
}

/* ---- lifecycle ------------------------------------------------------------ */

MunchiWave::MunchiWave() {}

MunchiWave::~MunchiWave()
{
    StopCard();
    delete reverb_;
    free(del_mem_);
    free(table_mem_);
}

int MunchiWave::Init()
{
    reverb_  = new daisysp::Reverb();
    del_mem_ = (chompi::InterpolatedDelayLine::AudioSample *)calloc(
        kMaxDelayTime, sizeof(chompi::InterpolatedDelayLine::AudioSample));
    table_mem_ = (float(*)[MAX_SAMPLES_PER_CYCLE])calloc(
        wavetableLoader::kMaxPreload * CYCLES, sizeof(float) * MAX_SAMPLES_PER_CYCLE);
    if(!reverb_ || !del_mem_ || !table_mem_)
        return -1;

    daisy::System::now_ms = 0;
    wt_loader_.Init(table_mem_);

    // ui.h InitFromPresets
    float defaults[14] = {enc_defaults[0][0], enc_defaults[0][1], enc_defaults[0][1],
                          enc_defaults[1][1], enc_defaults[1][1], 1.f,
                          1.f, enc_defaults[1][0], .5f, 0.f, 0.f, 0.f, 0.f, 0.f};
    presets_.Init(defaults);

    // NormalPage::Init
    for(int k = 0; k < 6; k++)
        for(int p = 0; p < 3; p++)
            enc_values_[p][k] = enc_defaults[p][k];

    clock_.Init();
    seq_.Init(&engine_, &clock_, &hw_);
    seq_.setMidiChannel(opts_.midi_ch_out);
    engine_.Init(48000.f, del_mem_, reverb_, &wt_loader_);

    // MenuPage::Init
    final_comp_      = 0.f;
    res_             = .63f;
    del_time_        = .4f;
    pitch_lfo_rate_  = .58f;
    filter_lfo_rate_ = .58f;
    engine_.setDelayTime(del_time_);
    engine_.setGain(enc_values_[0][5]);
    engine_.setPan(enc_values_[1][5]);

    up_.Init(44100.0, 48000.0);
    down_.Init(48000.0, 44100.0);
    out44_count_ = 160;
    memset(out44_l_, 0, sizeof(out44_l_));
    memset(out44_r_, 0, sizeof(out44_r_));
    memset(out48_, 0, sizeof(out48_));
    init_time_ = 0;
    return 0;
}

void MunchiWave::OptionsChanged()
{
    seq_.setMidiChannel(opts_.midi_ch_out);
    hw_.cc_out_enabled = opts_.midi_cc_out;
    SaveOptions();
}

/* ---- card ----------------------------------------------------------------- */

static bool is_file(const std::string &p)
{
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

static void mkdirs(const std::string &path)
{
    std::string p;
    for(size_t i = 0; i < path.size(); i++)
    {
        p += path[i];
        if(path[i] == '/' && p.size() > 1)
            mkdir(p.c_str(), 0775);
    }
    mkdir(path.c_str(), 0775);
}

static int copy_file(const std::string &from, const std::string &to)
{
    FILE *in = fopen(from.c_str(), "rb");
    if(!in)
        return -1;
    FILE *out = fopen((to + ".tmp").c_str(), "wb");
    if(!out)
    {
        fclose(in);
        return -1;
    }
    char   buf[65536];
    size_t n;
    while((n = fread(buf, 1, sizeof(buf), in)) > 0)
        fwrite(buf, 1, n, out);
    fclose(in);
    fflush(out);
    fsync(fileno(out));
    fclose(out);
    return rename((to + ".tmp").c_str(), to.c_str());
}

static std::string read_text(const std::string &p)
{
    std::string s;
    if(FILE *f = fopen(p.c_str(), "rb"))
    {
        char   buf[4096];
        size_t n;
        while((n = fread(buf, 1, sizeof(buf), f)) > 0)
            s.append(buf, n);
        fclose(f);
    }
    return s;
}

/* wavetableLoader::loadNamesFromSD + loadAllToMemory (wave_card.cpp) */
int MunchiWave::LoadTables()
{
    int loaded = LoadWavetableFolder(card_dir_, table_mem_, wavetableLoader::kMaxPreload);
    wt_loader_.numWavetables = loaded;
    return loaded;
}

void MunchiWave::StartCard(const std::string &card_dir, const std::string &factory_dir)
{
    card_dir_ = card_dir;
    mkdirs(card_dir_);
    const std::string marker = card_dir_ + "/.munchi-card";
    if(!is_file(marker) && !factory_dir.empty())
    {
        if(DIR *d = opendir(factory_dir.c_str()))
        {
            while(struct dirent *e = readdir(d))
            {
                std::string n = e->d_name;
                if(n[0] == '.')
                    continue;
                if(!is_file(card_dir_ + "/" + n))
                    copy_file(factory_dir + "/" + n, card_dir_ + "/" + n);
            }
            closedir(d);
        }
        if(FILE *f = fopen(marker.c_str(), "wb"))
        {
            fputs("Munchi Wave card. Delete this file to re-copy missing factory files.\n", f);
            fclose(f);
        }
    }

    std::string pj = read_text(card_dir_ + "/presets.json");
    if(!pj.empty())
        presets_.Parse(pj.c_str());
    presets_.updated = false;

    LoadTables();

    // chompi_main: once the tables are in, select the defaults slot
    SetVoiceSlot(15);
    ready_ = true;

    writer_run_.store(true);
    writer_ = std::thread([this] { WriterMain(); });
}

void MunchiWave::StopCard()
{
    if(!writer_run_.load())
        return;
    writer_run_.store(false);
    if(writer_.joinable())
        writer_.join();
    // flush what was pending
    if(presets_.updated)
    {
        presets_snapshot_ = presets_.Serialize();
        presets_ready_.store(true);
    }
    auto write = [&](const char *name, std::string &s, std::atomic<bool> &flag) {
        if(!flag.load())
            return;
        std::string p = card_dir_ + "/" + name, tmp = p + ".tmp";
        if(FILE *f = fopen(tmp.c_str(), "wb"))
        {
            fwrite(s.data(), 1, s.size(), f);
            fflush(f);
            fsync(fileno(f));
            fclose(f);
            rename(tmp.c_str(), p.c_str());
        }
        flag.store(false);
    };
    write("presets.json", presets_snapshot_, presets_ready_);
    write("options.json", options_snapshot_, options_ready_);
}

void MunchiWave::WriterMain()
{
#ifdef __linux__
    struct sched_param sp = {};
    sched_setscheduler(0, SCHED_OTHER, &sp);
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(0, &set);
    CPU_SET(1, &set);
    CPU_SET(2, &set);
    sched_setaffinity(0, sizeof(set), &set);
#endif
    auto write = [&](const char *name, std::string &s, std::atomic<bool> &flag) {
        if(!flag.load(std::memory_order_acquire))
            return;
        std::string p = card_dir_ + "/" + name, tmp = p + ".tmp";
        if(FILE *f = fopen(tmp.c_str(), "wb"))
        {
            fwrite(s.data(), 1, s.size(), f);
            fflush(f);
            fsync(fileno(f));
            fclose(f);
            rename(tmp.c_str(), p.c_str());
        }
        flag.store(false, std::memory_order_release);
    };
    while(writer_run_.load())
    {
        write("presets.json", presets_snapshot_, presets_ready_);
        write("options.json", options_snapshot_, options_ready_);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

/* options.json, WAVE's format */
static bool json_value(const std::string &s, const char *name, std::string *out)
{
    size_t p = s.find(std::string("\"") + name + "\"");
    if(p == std::string::npos || (p = s.find("\"value\"", p)) == std::string::npos
       || (p = s.find(':', p)) == std::string::npos)
        return false;
    p++;
    while(p < s.size() && strchr(" \t\r\n", s[p]))
        p++;
    size_t e = p;
    while(e < s.size() && !strchr(",}\r\n", s[e]))
        e++;
    *out = s.substr(p, e - p);
    return true;
}

void MunchiWave::LoadOptions(const std::string &path)
{
    std::string s = read_text(path), v;
    if(json_value(s, "Midi In Channel", &v))
        opts_.midi_ch_in = std::max(0, std::min(15, atoi(v.c_str()) - 1));
    if(json_value(s, "Midi Out Channel", &v))
        opts_.midi_ch_out = std::max(0, std::min(15, atoi(v.c_str()) - 1));
    if(json_value(s, "MIDI Clock Out", &v))
        opts_.midi_clock_out = v == "true";
    if(json_value(s, "MIDI CC In", &v))
        opts_.midi_cc_in = v == "true";
    if(json_value(s, "MIDI CC Out", &v))
        opts_.midi_cc_out = v == "true";
    if(json_value(s, "Pad Velocity", &v))
        opts_.pad_velocity = v == "true";
    seq_.setMidiChannel(opts_.midi_ch_out);
    hw_.cc_out_enabled = opts_.midi_cc_out;
}

void MunchiWave::SaveOptions()
{
    if(options_ready_.load())
        return;
    char buf[1024];
    auto item = [](char *b, size_t n, const char *name, const char *val, bool last) {
        return snprintf(b, n, "\t\t{\n\t\t\t\"name\": \"%s\",\n\t\t\t\"value\": %s\n\t\t}%s\n",
                        name, val, last ? "" : ",");
    };
    char in[16], out[16];
    snprintf(in, sizeof(in), "%d", opts_.midi_ch_in + 1);
    snprintf(out, sizeof(out), "%d", opts_.midi_ch_out + 1);
    int n = snprintf(buf, sizeof(buf), "{\n\t\"chompi\": [\n");
    n += item(buf + n, sizeof(buf) - n, "Midi In Channel", in, false);
    n += item(buf + n, sizeof(buf) - n, "Midi Out Channel", out, false);
    n += item(buf + n, sizeof(buf) - n, "MIDI Clock Out", opts_.midi_clock_out ? "true" : "false", false);
    n += item(buf + n, sizeof(buf) - n, "MIDI CC In", opts_.midi_cc_in ? "true" : "false", false);
    n += item(buf + n, sizeof(buf) - n, "MIDI CC Out", opts_.midi_cc_out ? "true" : "false", false);
    n += item(buf + n, sizeof(buf) - n, "Pad Velocity", opts_.pad_velocity ? "true" : "false", true);
    snprintf(buf + n, sizeof(buf) - n, "\t]\n}");
    options_snapshot_ = buf;
    options_ready_.store(true, std::memory_order_release);
}

/* ---- audio ---------------------------------------------------------------- */

void MunchiWave::ProcessHostBlock(const int16_t *in, int16_t *out, int frames)
{
    for(int i = 0; i < frames; i++)
    {
        up_.Push(in[i * 2] * (1.f / 32768.f), in[i * 2 + 1] * (1.f / 32768.f));
        while(up_.CanPull())
        {
            float l, r;
            up_.Pull(&l, &r);
            // WAVE has no audio input; the frames only pace the engine
            if(++in48_count_ == kEngineBlock)
            {
                RunEngineBlock();
                in48_count_ = 0;
            }
        }
    }
    int n = frames < out44_count_ ? frames : out44_count_;
    for(int i = 0; i < n; i++)
    {
        out[i * 2]     = (int16_t)f2s16(out44_l_[i]);
        out[i * 2 + 1] = (int16_t)f2s16(out44_r_[i]);
    }
    for(int i = n; i < frames; i++)
        out[i * 2] = out[i * 2 + 1] = 0;
    memmove(out44_l_, out44_l_ + n, (out44_count_ - n) * sizeof(float));
    memmove(out44_r_, out44_r_ + n, (out44_count_ - n) * sizeof(float));
    out44_count_ -= n;
}

void MunchiWave::RunEngineBlock()
{
    if(!ready_)
    {
        for(int i = 0; i < kEngineBlock; i++)
            down_.Push(0.f, 0.f);
    }
    else
    {
        // AudioCallback(): MIDI, controls, sequencer, Prepare, Process
        UiTick();
        if(seq_.getPlaying())
            seq_.checkAndPop();
        engine_.Prepare();

        const float *ins[4] = {in48_[0], in48_[1], in48_[2], in48_[3]};
        float       *outs[4] = {out48_[0], out48_[1], out48_[2], out48_[3]};
        engine_.Process(ins, outs, kEngineBlock);

        // WAVE's headphone and line outs are the same mix. The engine sums
        // eight voices at 0.2 each and divides by eight, and the CHOMPI's
        // analog output stage made up the rest; one held note peaks around
        // -34 dBFS here without it. kOutputTrim brings a note to roughly
        // Munchi Tape's level, and SoftLimit keeps big chords off the rail.
        const float kOutputTrim = 4.f;
        for(int i = 0; i < kEngineBlock; i++)
            down_.Push(daisysp::SoftLimit(out48_[0][i] * kOutputTrim),
                       daisysp::SoftLimit(out48_[1][i] * kOutputTrim));

        // MidiClockCallback: 12 PPQN at the clock manager's tempo, while the
        // sequencer plays and clock out is on
        if(seq_.getPlaying() && opts_.midi_clock_out)
        {
            clock_phase_ += clock_.midiClockHz() * kEngineBlock / 48000.0;
            while(clock_phase_ >= 1.0)
            {
                clock_phase_ -= 1.0;
                hw_.queueMidiClock();
            }
        }
        else
            clock_phase_ = 0.0;
    }
    while(down_.CanPull() && out44_count_ < 1024)
    {
        down_.Pull(&out44_l_[out44_count_], &out44_r_[out44_count_]);
        out44_count_++;
    }
    daisy::System::now_ms++;
}

void MunchiWave::UiTick()
{
    const uint32_t now = daisy::System::now_ms;
    if(init_ignore_ && now - init_time_ > 1500)
        init_ignore_ = false;

    // NormalPage::Draw
    seq_.checkReset();

    if(menu_active_ && MenuIsClosable())
        menu_active_ = false;

    // TestPresets() every 5 s, written by the worker
    if(now - presets_check_t_ > 5000)
    {
        presets_check_t_ = now;
        if(presets_.updated && !presets_ready_.load() && writer_run_.load())
        {
            presets_snapshot_ = presets_.Serialize();
            presets_.updated  = false;
            presets_ready_.store(true, std::memory_order_release);
        }
    }
}

/* ---- NormalPage ----------------------------------------------------------- */

void MunchiWave::ApplyKnob(int knob, int page)
{
    float v = enc_values_[page][knob];
    switch(knob)
    {
        case 1:
            if(page == 0)
                engine_.setAttack(v);
            else
                engine_.setPitchLfoDepth(v);
            break;
        case 2:
            if(page == 0)
                engine_.setRelease(v);
            else
                engine_.setLfoDepth(v);
            break;
        case 3:
            if(page == 0)
                engine_.setDelayFeedback(v);
            else
                engine_.setMasterCutoff(v);
            break;
        case 5:
            if(page == 0)
                engine_.setGain(v);
            else
                engine_.setPan(v);
            break;
    }
}

void MunchiWave::Button(int id, bool rising)
{
    if(menu_active_ && MenuButton(id, rising))
        return;
    NormalButton(id, rising);
}

void MunchiWave::NormalButton(int buttonID, bool rising)
{
    if(init_ignore_ || !ready_)
        return;
    switch(buttonID)
    {
        case NC_1:
        case NC_2:
        case NC_3:
        case NC_4:
        case NC_5:
        case ENC_1_SW:
        case ENC_2_SW:
        case ENC_3_SW:
        case ENC_4_SW:
        case ENC_6_SW:
        case SW_TOG: break;

        case ENC_5_SW:
            if(!rising)
                hw_.SendCC(opts_.midi_ch_out, cc_map[0][4], enc_values_[0][4] * 127.f);
            else
                enc_values_[0][4] = clock_.processTapClock(enc_values_[0][4]);
            break;

        case KEY_27: seq_.playButton(rising); break;

        case KEY_28:
            seq_.loopButton(rising);
            hw_.SendCC(opts_.midi_ch_out, 15, rising ? 127 : 0);
            break;

        case KEY_26:
            if(!switch_state_)
            {
                hw_.SendCC(opts_.midi_ch_out, 14, rising ? 127 : 0);
                if(rising)
                {
                    if(seq_.getRecording())
                        seq_.insertRest();
                    else
                        seq_.setMuted(true);
                }
                else
                    seq_.setMuted(false);
            }
            break;

        default:
            if(buttonID < 0 || buttonID >= 64)
                break;
            if(rising)
            {
                int base = buttonID < 40 ? key_map[buttonID] : 0;
                if(KeyIdToNote(buttonID) < 0)
                    break;
                key_note_on_[buttonID] = true;
                engine_.request_fifo.PushBack(KeyRequest(KeyRequest::Type::START, base - 60,
                                                         buttonID, pad_velocity_));
                int n = base + 12 * engine_.getOctave();
                n     = n < 0 ? 0 : n > 127 ? 127 : n;
                midi_note_sent_[buttonID] = (uint8_t)n;
                hw_.queueMidiNote(opts_.midi_ch_out, (uint8_t)n, 127, NoteOn);
            }
            else if(key_note_on_[buttonID])
            {
                key_note_on_[buttonID] = false;
                engine_.request_fifo.PushBack(KeyRequest(KeyRequest::Type::STOP, 0, buttonID, 127.f));
                if(seq_.getRecording())
                    seq_.insertNextKey(KeyRequest(KeyRequest::Type::STOP,
                                                  key_map[buttonID] - 60, buttonID, 127.f));
                hw_.queueMidiNote(opts_.midi_ch_out, midi_note_sent_[buttonID], 127, NoteOff);
            }
            break;
    }
}

void MunchiWave::Encoder(int knob, int page, int turns, bool menu)
{
    if(knob < 0 || knob > 5 || turns == 0)
        return;
    knob_page_[knob] = page;
    int t = (knob == 0 || knob == 4) ? turns : turns * 3;
    if(menu)
        MenuEncoder(knob, t);
    else
        NormalEncoder(knob, t, 0);
}

void MunchiWave::NormalEncoder(int encoderID, int turns, int stepsPerRevolution)
{
    if(init_ignore_ || !ready_)
        return;
    int page = knob_page_[encoderID];
    if(stepsPerRevolution > 0)
        enc_values_[page][encoderID] = turns / 127.f;
    else
    {
        float inc = turns * kEncoderCoarseStep;
        if((encoderID == 0 && page == 0) || (encoderID == 1 && page == 0)
           || (encoderID == 2 && page == 0))
            inc = turns * kEncoderFineStep;
        else if(encoderID == 4 && page == 0)
            inc = turns * kEncoderTempoStep;
        else if(encoderID == 0 && page == 1)
            inc = turns * kEncoderCycleStep;
        enc_values_[page][encoderID] += inc;
    }
    enc_values_[page][encoderID] = daisysp::fclamp(enc_values_[page][encoderID], 0.f, 1.f);

    if(encoderID == 0 && page == 0)
        engine_.setGlobalPitch(enc_values_[0][0]);
    else if(encoderID == 0 && page == 1)
    {
        if(stepsPerRevolution > 0)
            engine_.setCycle((uint8_t)floorf(enc_values_[1][0] * 32.f), true);
        else
            engine_.setCycle(turns, false);
    }
    else if(encoderID == 4)
    {
        int t = clock_.getTempo() + turns;
        t     = t > 480 ? 480 : t < 160 ? 160 : t;
        clock_.changeTempo(t);
    }

    if(stepsPerRevolution == 0 && cc_map[page][encoderID])
        hw_.SendCC(opts_.midi_ch_out, cc_map[page][encoderID],
                   enc_values_[page][encoderID] * 127);
    ApplyKnob(encoderID, page);
}

/* ---- MenuPage ------------------------------------------------------------- */

void MunchiWave::MenuKey(bool rising)
{
    // ui.h opened the menu page on a CHOMPI-key press in play mode and then
    // delivered that press to it
    if(rising && !menu_active_)
    {
        menu_active_ = true;
        MenuFocusGained();
    }
    if(menu_active_)
        MenuButton(KEY_26, rising);
}

void MunchiWave::RestKey(bool rising)
{
    // NormalPage KEY_26 with the switch in record mode
    bool saved    = switch_state_;
    switch_state_ = false;
    NormalButton(KEY_26, rising);
    switch_state_ = saved;
}

void MunchiWave::Click(int sw)
{
    if(sw == ENC_5_SW)
    {
        MenuButton(ENC_5_SW, true); // the menu's tempo reset
        MenuButton(ENC_5_SW, false);
        return;
    }
    MenuButton(sw, true);
    MenuButton(sw, false);
}

void MunchiWave::TapTempo()
{
    NormalButton(ENC_5_SW, true);
    NormalButton(ENC_5_SW, false);
}

void MunchiWave::SelectSlot(int slot)
{
    if(slot < 1 || slot > 15 || !(slot == 15 || presets_.slotValid[slot - 1]))
        return;
    selected_slot_ = (uint8_t)slot;
    SetVoiceSlot(slot);
}

void MunchiWave::ClearSequence()
{
    seq_.clearSequence();
    seq_.cleared_all = true;
}

void MunchiWave::MenuFocusGained()
{
    pre_quantized_amount_ = enc_values_[0][0];
    menu_chompi_pressed_  = true;
    copy_src_             = kSlotNone;
    preset_mode_          = PM_NONE;
}

bool MunchiWave::MenuIsClosable()
{
    return daisy::System::now_ms - blink_startt_ > 1250
           && (preset_mode_ == PM_SAVING || preset_mode_ == PM_COPYING
               || preset_mode_ == PM_ERASING
               || (preset_mode_ == PM_NONE && !menu_chompi_pressed_));
}

void MunchiWave::MenuEncoder(int encoderID, int turns)
{
    int   page = knob_page_[encoderID];
    float inc  = turns * kEncoderCoarseStep;
    if(preset_mode_ != PM_NONE)
        return;

    if(encoderID == 0)
    {
        if(page == 0) // stepped pitch, in semitones
        {
            pre_quantized_amount_ = daisysp::fclamp(pre_quantized_amount_ + inc, 0.f, 1.f);
            float normalized      = (pre_quantized_amount_ - .5f) * 2.f;
            int   semis           = (int)roundf(normalized * 12);
            enc_values_[0][0]     = (semis / 12.f) * .5f + .5f;
            engine_.setGlobalPitch(enc_values_[0][0]);
        }
        else // wavetable select: two detents per table
        {
            table_detent_acc_ += turns;
            while(table_detent_acc_ >= 2 || table_detent_acc_ <= -2)
            {
                int8_t dir = table_detent_acc_ > 0 ? 1 : -1;
                engine_.nextTable(dir, false);
                table_detent_acc_ -= 2 * dir;
            }
        }
    }
    else if(encoderID == 1)
    {
        if(page == 0)
        {
            enc_values_[0][1]
                = daisysp::fclamp(enc_values_[0][1] + turns * kEncoderEnvCoarseStep, 0.f, 1.f);
            engine_.setAttack(enc_values_[0][1]);
        }
        else
        {
            pitch_lfo_rate_ = daisysp::fclamp(pitch_lfo_rate_ + inc, 0.f, 1.f);
            engine_.setPitchLfoRate(pitch_lfo_rate_);
        }
    }
    else if(encoderID == 2)
    {
        if(page == 0)
        {
            enc_values_[0][2]
                = daisysp::fclamp(enc_values_[0][2] + turns * kEncoderEnvCoarseStep, 0.f, 1.f);
            engine_.setRelease(enc_values_[0][2]);
        }
        else
        {
            filter_lfo_rate_ = daisysp::fclamp(filter_lfo_rate_ + inc, 0.f, 1.f);
            engine_.setLfoRate(filter_lfo_rate_);
        }
    }
    else if(encoderID == 3)
    {
        if(page == 0)
        {
            del_time_ = daisysp::fclamp(del_time_ + inc, 0.f, 1.f);
            engine_.setDelayTime(del_time_);
        }
        else
        {
            res_ = daisysp::fclamp(res_ + inc, 0.f, 1.f);
            engine_.setMasterResonance(res_);
        }
    }
    else if(encoderID == 4)
    {
        clock_.changeDiv(turns);
    }
    else if(encoderID == 5)
    {
        final_comp_ = daisysp::fclamp(final_comp_ + inc, 0.f, 1.f);
        engine_.setFinalComp(final_comp_);
    }
}

void MunchiWave::DumpValuePresets(uint8_t slot)
{
    presets_.SetValue(enc_values_[0][0], slot, 0);
    presets_.SetValue((float)engine_.getCycle(), slot, 1);
    presets_.SetValue((float)engine_.getTable(), slot, 2);
    presets_.SetValue(enc_values_[0][1], slot, 3);
    presets_.SetValue(enc_values_[1][1], slot, 4);
    presets_.SetValue(enc_values_[0][2], slot, 5);
    presets_.SetValue(enc_values_[1][2], slot, 6);
    presets_.SetValue(enc_values_[1][3], slot, 7);
    presets_.SetValue(pitch_lfo_rate_, slot, 8);
    presets_.SetValue(enc_values_[0][3], slot, 9);
    presets_.SetValue(res_, slot, 10);
    presets_.SetValue(filter_lfo_rate_, slot, 11);
    presets_.SetValue(del_time_, slot, 12);
    presets_.SetValue((float)((engine_.getPitchLfoOn() ? 1 : 0)
                              | (engine_.getFilterLfoOn() ? 2 : 0)),
                      slot, 13);
}

void MunchiWave::SetVoiceSlot(size_t slot)
{
    engine_.setVoiceSlot(slot);
    if(slot == 15 || !presets_.IsValid(slot))
    {
        enc_values_[0][0] = enc_defaults[0][0];
        enc_values_[0][1] = enc_defaults[0][1];
        enc_values_[0][2] = enc_defaults[0][2];
        enc_values_[1][0] = enc_defaults[1][0];
        enc_values_[1][1] = enc_defaults[1][1];
        enc_values_[1][2] = enc_defaults[1][2];
        enc_values_[0][3] = enc_defaults[0][3];
        enc_values_[1][3] = enc_defaults[1][3];
        engine_.setCycle(0, true);
        engine_.nextTable(0, true);
        res_             = .63f;
        del_time_        = .4f;
        pitch_lfo_rate_  = .58f;
        filter_lfo_rate_ = .58f;
        engine_.setPitchLfoOn(true);
        engine_.setFilterLfoOn(true);
    }
    else
    {
        enc_values_[0][0] = presets_.GetValue(slot, 0);
        uint8_t cyc       = (int)presets_.GetValue(slot, 1);
        engine_.setCycle(cyc, true);
        enc_values_[1][0] = cyc * kEncoderCycleStep;
        uint8_t tab       = (int)presets_.GetValue(slot, 2);
        engine_.nextTable(tab, true);
        enc_values_[0][1] = presets_.GetValue(slot, 3);
        enc_values_[1][1] = presets_.GetValue(slot, 4);
        enc_values_[0][2] = presets_.GetValue(slot, 5);
        enc_values_[1][2] = presets_.GetValue(slot, 6);
        enc_values_[1][3] = presets_.GetValue(slot, 7);
        pitch_lfo_rate_   = presets_.GetValue(slot, 8);
        enc_values_[0][3] = presets_.GetValue(slot, 9);
        res_              = presets_.GetValue(slot, 10);
        filter_lfo_rate_  = presets_.GetValue(slot, 11);
        del_time_         = presets_.GetValue(slot, 12);
        int t             = (int)roundf(presets_.GetValue(slot, 13));
        engine_.setPitchLfoOn(t & 1);
        engine_.setFilterLfoOn(t & 2);
    }
    engine_.setGlobalPitch(enc_values_[0][0]);
    engine_.setAttack(enc_values_[0][1]);
    engine_.setPitchLfoDepth(enc_values_[1][1]);
    engine_.setLfoDepth(enc_values_[1][2]);
    engine_.setPitchLfoRate(pitch_lfo_rate_);
    engine_.setLfoRate(filter_lfo_rate_);
    engine_.setRelease(enc_values_[0][2]);
    engine_.setMasterCutoff(enc_values_[1][3]);
    engine_.setDelayFeedback(enc_values_[0][3]);
    engine_.setMasterResonance(res_);
    engine_.setDelayTime(del_time_);
}

bool MunchiWave::MenuButton(int buttonID, bool rising)
{
    switch(buttonID)
    {
        case NC_1:
        case NC_2:
        case NC_3:
        case NC_4:
        case NC_5: break;

        case ENC_4_SW: // pitch knob: reset this page
            if(rising)
            {
                if(knob_page_[0] == 0)
                {
                    enc_values_[0][0] = enc_defaults[0][0];
                    engine_.setGlobalPitch(.5f);
                    pre_quantized_amount_ = .5f;
                }
                else
                {
                    enc_values_[1][0] = enc_defaults[1][0];
                    engine_.setCycle(0, true);
                }
            }
            break;

        case ENC_5_SW:
            if(rising)
            {
                clock_.resetDiv();
                clock_.changeTempo(320);
                enc_values_[0][4] = .5f;
            }
            break;

        case ENC_1_SW:
            if(rising)
            {
                const int page          = knob_page_[1];
                enc_values_[page][1] = enc_defaults[page][1];
                if(page == 0)
                    engine_.setAttack(enc_values_[0][1]);
                else
                {
                    engine_.setPitchLfoDepth(enc_values_[1][1]);
                    pitch_lfo_rate_ = .58f;
                    engine_.setPitchLfoRate(pitch_lfo_rate_);
                }
            }
            break;

        case ENC_2_SW:
            if(rising)
            {
                const int page          = knob_page_[2];
                enc_values_[page][2] = enc_defaults[page][2];
                if(page == 0)
                    engine_.setRelease(enc_values_[0][2]);
                else
                {
                    engine_.setLfoDepth(enc_values_[1][2]);
                    filter_lfo_rate_ = .58f;
                    engine_.setLfoRate(filter_lfo_rate_);
                }
            }
            break;

        case ENC_6_SW:
            if(rising)
            {
                const int page          = knob_page_[5];
                enc_values_[page][5] = enc_defaults[page][5];
                if(page == 0)
                    engine_.setGain(enc_values_[0][5]);
                else
                    engine_.setPan(enc_values_[1][5]);
                final_comp_ = 0.f;
                engine_.setFinalComp(final_comp_);
            }
            break;

        case ENC_3_SW: // magic wand
            if(rising)
            {
                enc_values_[0][3] = enc_defaults[0][3];
                enc_values_[1][3] = enc_defaults[1][3];
                engine_.setDelayFeedback(enc_values_[0][3]);
                engine_.setMasterCutoff(enc_values_[1][3]);
                res_      = .63f;
                del_time_ = .4f;
                engine_.setMasterResonance(res_);
                engine_.setDelayTime(del_time_);
            }
            break;

        case SW_TOG: break;

        case KEY_26: // chompi: confirm
            menu_chompi_pressed_ = rising;
            if(rising && selected_slot_ != kSlotNone)
            {
                if(preset_mode_ == PM_SAVE_SEL)
                {
                    engine_.stopAllVoices();
                    blink_startt_ = daisy::System::now_ms;
                    preset_mode_  = PM_SAVING;
                    DumpValuePresets(selected_slot_);
                    presets_.Save(selected_slot_);
                    SetVoiceSlot(selected_slot_);
                }
                else if(preset_mode_ == PM_COPY_DEST)
                {
                    engine_.stopAllVoices();
                    blink_startt_ = daisy::System::now_ms;
                    preset_mode_  = PM_COPYING;
                    presets_.Copy(copy_src_, selected_slot_);
                    SetVoiceSlot(selected_slot_);
                }
                else if(preset_mode_ == PM_ERASE_SEL)
                {
                    engine_.stopAllVoices();
                    preset_mode_  = PM_ERASING;
                    blink_startt_ = daisy::System::now_ms;
                    presets_.Invalidate(selected_slot_);
                    SetVoiceSlot(15);
                }
            }
            return false;

        case KEY_16:
        case KEY_17:
            if(rising)
                engine_.setOctave(buttonID == KEY_16 ? -1 : 1);
            else
                return false;
            break;

        case KEY_18:
        case KEY_19:
        case KEY_20:
            if(!rising)
                return false;
            seq_.setGate(buttonID == KEY_18 ? .1f : buttonID == KEY_19 ? .5f : 1.f);
            break;

        case KEY_21:
            if(rising)
                engine_.setPitchLfoOn(!engine_.getPitchLfoOn());
            break;

        case KEY_22:
            if(rising)
                engine_.setFilterLfoOn(!engine_.getFilterLfoOn());
            break;

        case KEY_23: // erase
            if(rising)
            {
                if(preset_mode_ == PM_NONE)
                    preset_mode_ = PM_ERASE_SEL;
                else if(preset_mode_ == PM_ERASE_SEL)
                    preset_mode_ = PM_NONE;
                else
                    break;
                selected_slot_ = kSlotNone;
            }
            else
                return false;
            break;

        case KEY_24: // copy
            if(rising)
            {
                if(preset_mode_ == PM_NONE)
                    preset_mode_ = PM_COPY_SRC;
                else if(preset_mode_ == PM_COPY_SRC || preset_mode_ == PM_COPY_DEST)
                    preset_mode_ = PM_NONE;
                else
                    break;
                copy_src_      = kSlotNone;
                selected_slot_ = kSlotNone;
            }
            else
                return false;
            break;

        case KEY_25: // save
            if(rising)
            {
                if(preset_mode_ == PM_NONE)
                    preset_mode_ = PM_SAVE_SEL;
                else if(preset_mode_ == PM_SAVE_SEL)
                    preset_mode_ = PM_NONE;
                else
                    break;
                selected_slot_ = kSlotNone;
            }
            else
                return false;
            break;

        default: // white keys and play/pause
            if(rising)
            {
                int slot_req = KeyToSlot(buttonID);
                // Munchi: WAVE inherited TAPE's "Loop/Play = the looper" copy
                // target, but has no looper; a copy there loaded the defaults.
                // Play and Loop keep their sequencer jobs instead.
                if(buttonID == KEY_27 || buttonID == KEY_28)
                    return false;
                if(slot_req == kSlotNone)
                {
                }
                else if(preset_mode_ == PM_COPY_SRC && presets_.IsValid(slot_req))
                {
                    copy_src_    = slot_req;
                    preset_mode_ = PM_COPY_DEST;
                }
                else if(preset_mode_ == PM_COPY_DEST && copy_src_ != slot_req && slot_req != 15)
                {
                    selected_slot_ = slot_req;
                }
                else if(preset_mode_ == PM_NONE
                        && (slot_req == 15 || presets_.slotValid[slot_req - 1]))
                {
                    selected_slot_ = slot_req;
                    SetVoiceSlot(selected_slot_);
                }
                else if(preset_mode_ == PM_ERASE_SEL && slot_req != 15
                        && presets_.slotValid[slot_req - 1])
                {
                    selected_slot_ = slot_req;
                }
                else if(preset_mode_ == PM_SAVE_SEL && slot_req != 15)
                {
                    selected_slot_ = slot_req;
                }
            }
            else if(buttonID < 29)
                return false; // allow releasing notes in shift menu
            break;
    }
    return true;
}

/* ---- MIDI in (MidiManager::ProcessMidiIn) --------------------------------- */

void MunchiWave::Midi(const uint8_t *msg, int len)
{
    if(len < 3 || !ready_)
        return;
    uint8_t type = msg[0] & 0xF0, ch = msg[0] & 0x0F;
    if(ch != opts_.midi_ch_in)
        return;
    if(type == 0x90 && msg[2] > 0)
    {
        int key = msg[1] - 36;
        if(key > 48 || key < 0)
            return;
        engine_.request_fifo.PushBack(KeyRequest(KeyRequest::Type::START, key - 24,
                                                 midi2key[key], (float)(msg[2] + 1)));
    }
    else if(type == 0x80 || (type == 0x90 && msg[2] == 0))
    {
        int key = msg[1] - 36;
        if(key > 48 || key < 0)
            return;
        engine_.request_fifo.PushBack(
            KeyRequest(KeyRequest::Type::STOP, key - 24, midi2key[key], 127.f));
    }
    else if(type == 0xB0)
    {
        if(!opts_.midi_cc_in || menu_active_)
            return;
        uint8_t cc = msg[1], val = msg[2];
        if(cc >= 20 && cc < 26)
        {
            uint8_t knob = cc - 20;
            if(knob == 4)
                return;
            NormalEncoder(knob, val, 1);
        }
        else if(cc == 14 || cc == 15)
        {
            // CC 14 is the CHOMPI key in record mode (the firmware dropped it
            // in play mode; on Move that role is always the Sample button's)
            const uint8_t idx  = cc - 14;
            const bool    last = key_cc_[idx];
            if(val > 84)
                key_cc_[idx] = true;
            else if(val < 42)
                key_cc_[idx] = false;
            if(last == key_cc_[idx])
                return;
            if(cc == 14)
                RestKey(key_cc_[idx]);
            else
                Button(KEY_28, key_cc_[idx]);
        }
    }
}

/* ---- harness -------------------------------------------------------------- */

void MunchiWave::ScriptCommand(const char *op, const char *a, float b)
{
    if(!strcmp(op, "on"))
    {
        init_ignore_ = false;
        Button(atoi(a), true);
    }
    else if(!strcmp(op, "off"))
        Button(atoi(a), false);
    else if(!strcmp(op, "slot"))
        SelectSlot(atoi(a));
    else if(!strcmp(op, "enc") || !strcmp(op, "menc"))
    {
        int knob = 0, page = 0;
        sscanf(a, "%d.%d", &knob, &page);
        init_ignore_ = false;
        Encoder(knob, page, (int)b, op[0] == 'm');
    }
    else if(!strcmp(op, "menukey"))
        MenuKey(atoi(a) != 0);
    else if(!strcmp(op, "rest"))
        RestKey(atoi(a) != 0);
    else if(!strcmp(op, "note"))
    {
        uint8_t m[3] = {uint8_t(0x90 | opts_.midi_ch_in), (uint8_t)atoi(a),
                        (uint8_t)(b < 0 ? 100 : b)};
        Midi(m, 3);
    }
    else if(!strcmp(op, "noteoff"))
    {
        uint8_t m[3] = {uint8_t(0x80 | opts_.midi_ch_in), (uint8_t)atoi(a), 0};
        Midi(m, 3);
    }
}

} // namespace munchi
