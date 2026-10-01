#include "surface.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

namespace munchi
{

/* ---- Move control numbers ---------------------------------------------- */
enum
{
    CC_JOG_CLICK = 3,
    CC_JOG       = 14,
    CC_TRACK4    = 40, // reversed: CC 43 is track 1
    CC_TRACK1    = 43,
    CC_SHIFT     = 49,
    CC_MENU      = 50,
    CC_BACK      = 51,
    CC_CAPTURE   = 52,
    CC_DOWN      = 54,
    CC_UP        = 55,
    CC_LOOP      = 58,
    CC_COPY      = 60,
    CC_LEFT      = 62,
    CC_RIGHT     = 63,
    CC_KNOB1     = 71,
    CC_VOLUME    = 79,
    CC_PLAY      = 85,
    CC_REC       = 86,
    CC_SAMPLE    = 118,
    CC_DELETE    = 119,
};

/* ---- palette (schwung src/shared/constants.mjs) --------------------------- */
enum
{
    C_OFF       = 0,
    C_WHITE     = 120,
    C_GREY      = 118,
    C_GREY_DIM  = 123,
    C_GREY_DARK = 124,
    C_RED       = 127,
    C_GREEN     = 126,
    C_BLUE      = 125,
    C_YELLOW    = 7,
    C_PINK      = 25,
    C_PINK_DIM  = 113,
    C_PURPLE    = 23,
    C_PURPLE_DIM = 109,
    C_TEAL      = 15,
    C_AZURE     = 16,
};

static const int kNumSettings = 7;

/* the keyboard on the pads: see the Tape port; identical layout */
static const int kPadNote[32] = {
    48, 50, 52, 53, 55, 57, 59, 60, //
    -1, 49, 51, -1, 54, 56, 58, -1, //
    60, 62, 64, 65, 67, 69, 71, 72, //
    -1, 61, 63, -1, 66, 68, 70, -1, //
};

static bool is_black(int note)
{
    int n = note % 12;
    return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
}

// clockManager freeDivs: 24, 18, 12, 8, 6 twelfths of a quarter at tempo/2
static const char *kDivNames[5] = {"1/4", "1/8.", "1/8", "1/8T", "1/16"};

Surface::Surface(MunchiWave &e) : eng_(e) { InvalidateLeds(); }

void Surface::InvalidateLeds()
{
    memset(sent_pad_, 0xFF, sizeof(sent_pad_));
    memset(sent_step_, 0xFF, sizeof(sent_step_));
    memset(sent_cc_, 0xFF, sizeof(sent_cc_));
}

int Surface::PadToKey(int pad) const
{
    if(pad < 0 || pad >= 32 || kPadNote[pad] < 0)
        return -1;
    return NoteToKeyId(kPadNote[pad]);
}

void Surface::Show(const char *name, const char *value)
{
    snprintf(show_name_, sizeof(show_name_), "%s", name);
    snprintf(show_value_, sizeof(show_value_), "%s", value);
    show_t_ = eng_.NowMs() ? eng_.NowMs() : 1;
}

/* ---- input ---------------------------------------------------------------- */

void Surface::HandleInternal(uint8_t status, uint8_t d1, uint8_t d2)
{
    uint8_t type = status & 0xF0;
    if(type == 0x90 || type == 0x80)
    {
        bool on = type == 0x90 && d2 > 0;
        if(d1 <= 9)
            OnKnobTouch(d1, on);
        else if(d1 >= 16 && d1 <= 31)
            OnStep(d1 - 16, on);
        else if(d1 >= 68 && d1 <= 99)
            OnPad(d1 - 68, d2, on);
        return;
    }
    if(type != 0xB0)
        return;
    int delta = d2 < 64 ? d2 : (int)d2 - 128;
    if(d1 >= CC_KNOB1 && d1 < CC_KNOB1 + 8)
        OnKnob(d1 - CC_KNOB1, delta);
    else if(d1 == CC_VOLUME)
        OnKnob(8, delta);
    else if(d1 == CC_JOG)
    {
        if(settings_)
        {
            settings_cursor_ += delta > 0 ? 1 : -1;
            if(settings_cursor_ < 0)
                settings_cursor_ = 0;
            if(settings_cursor_ > kNumSettings - 1)
                settings_cursor_ = kNumSettings - 1;
        }
        else
        {
            int s = NextPreset(eng_.Eng().getVoiceSlot(), delta > 0 ? 1 : -1);
            eng_.SelectSlot(s);
            char v[16];
            if(s == 15)
                snprintf(v, sizeof(v), "DEFAULTS");
            else
                snprintf(v, sizeof(v), "%d", s);
            Show("PRESET", v);
        }
    }
    else
        OnButton(d1, d2 >= 64);
}

void Surface::OnPad(int pad, int vel, bool on)
{
    int id = PadToKey(pad);
    if(id < 0)
        return;
    if(on)
    {
        eng_.SetPadVelocity((float)vel);
        if(pad_hold_[id]++ == 0)
            eng_.Button(id, true);
    }
    else if(pad_hold_[id] > 0 && --pad_hold_[id] == 0)
        eng_.Button(id, false);
}

void Surface::OnStep(int step, bool on)
{
    int slot = step + 1;
    if(!on || slot > 15)
        return;
    if(eng_.SlotValid(slot))
    {
        eng_.SelectSlot(slot);
        char v[16];
        if(slot == 15)
            snprintf(v, sizeof(v), "DEFAULTS");
        else
            snprintf(v, sizeof(v), "%d", slot);
        Show("PRESET", v);
    }
}

/* ---- knobs ----------------------------------------------------------------
 * Every sound control has a knob: two pages of eight (Left/Right), plus the
 * volume knob. Each is a CHOMPI encoder on one of its pages, reached the way
 * the play page reached it or the way the menu page did. Shift keeps only the
 * menu's extra gestures: pitch in semitones, coarse attack/release, and the
 * tempo knob's step length. */
enum Param
{
    P_TABLE, P_FRAME, P_FILTER, P_RES, P_ATTACK, P_RELEASE, P_SPACE, P_TEMPO,
    P_PITCH, P_PLFO_D, P_PLFO_R, P_FLFO_D, P_FLFO_R, P_TIME, P_PAN, P_COMP,
    P_VOLUME, P_PITCH_ST, P_ATTACK_C, P_RELEASE_C, P_STEP, P_NONE,
};

struct ParamDef
{
    const char *name, *abbrev;
    int         enc, page;
    bool        menu;
    int         shift;
    int         click; // Delete + touch: the encoder click, or -1
};

static const ParamDef kParams[] = {
    {"TABLE", "TAB", 0, 1, true, P_NONE, -1},
    {"FRAME", "FRM", 0, 1, false, P_NONE, ENC_4_SW},
    {"FILTER", "FLT", 3, 1, false, P_NONE, ENC_3_SW},
    {"RESONANCE", "RES", 3, 1, true, P_NONE, ENC_3_SW},
    {"ATTACK", "ATK", 1, 0, false, P_ATTACK_C, ENC_1_SW},
    {"RELEASE", "REL", 2, 0, false, P_RELEASE_C, ENC_2_SW},
    {"SPACE", "SPC", 3, 0, false, P_NONE, ENC_3_SW},
    {"TEMPO", "BPM", 4, 0, false, P_STEP, ENC_5_SW},
    {"PITCH", "PCH", 0, 0, false, P_PITCH_ST, ENC_4_SW},
    {"PITCH LFO", "PLF", 1, 1, false, P_NONE, ENC_1_SW},
    {"P.LFO RATE", "PRT", 1, 1, true, P_NONE, ENC_1_SW},
    {"FILTER LFO", "FLF", 2, 1, false, P_NONE, ENC_2_SW},
    {"F.LFO RATE", "FRT", 2, 1, true, P_NONE, ENC_2_SW},
    {"DELAY TIME", "TIM", 3, 0, true, P_NONE, ENC_3_SW},
    {"PAN", "PAN", 5, 1, false, P_NONE, ENC_6_SW},
    {"COMP", "CMP", 5, 0, true, P_NONE, -1},
    {"VOLUME", "VOL", 5, 0, false, P_COMP, ENC_6_SW},
    {"PITCH STEP", "PCH", 0, 0, true, P_NONE, -1},
    {"ATTACK", "ATK", 1, 0, true, P_NONE, -1},
    {"RELEASE", "REL", 2, 0, true, P_NONE, -1},
    {"STEP LENGTH", "STP", 4, 0, true, P_NONE, -1},
};

static const int kPage[2][8] = {
    {P_TABLE, P_FRAME, P_FILTER, P_RES, P_ATTACK, P_RELEASE, P_SPACE, P_TEMPO},
    {P_PITCH, P_PLFO_D, P_PLFO_R, P_FLFO_D, P_FLFO_R, P_TIME, P_PAN, P_COMP},
};

int Surface::KnobParam(int knob, bool shift) const
{
    int p = knob == 8 ? P_VOLUME : kPage[page_][knob];
    if(shift && kParams[p].shift != P_NONE)
        p = kParams[p].shift;
    return p;
}

void Surface::OnKnob(int knob, int delta)
{
    if(delta == 0 || knob < 0 || knob > 8)
        return;
    int p = KnobParam(knob, shift_);
    const ParamDef &d = kParams[p];
    eng_.Encoder(d.enc, d.page, delta, d.menu);
    char value[24];
    ParamText(p, value);
    Show(d.name, value);
}

void Surface::OnKnobTouch(int knob, bool on)
{
    if(!on || knob > 8)
        return;
    int p = KnobParam(knob, false);
    const ParamDef &d = kParams[p];
    if(delete_)
    {
        if(d.click < 0)
            return;
        eng_.SetKnobPage(d.enc, d.page);
        eng_.Click(d.click);
        Show(d.click == ENC_3_SW ? "EFFECTS" : d.name, "RESET");
        return;
    }
    p = KnobParam(knob, shift_);
    char value[24];
    ParamText(p, value);
    Show(kParams[p].name, value);
}

void Surface::OnButton(int cc, bool press)
{
    MunchiWave &e = eng_;
    switch(cc)
    {
        case CC_SHIFT:
            shift_ = press;
            e.MenuKey(press);
            break;

        case CC_SAMPLE: e.RestKey(press); break;

        case CC_LOOP:
        case CC_REC:
            if(press && delete_)
            {
                e.ClearSequence();
                Show("SEQUENCE", "CLEARED");
                break;
            }
            e.Button(KEY_28, press);
            break;

        case CC_PLAY: e.Button(KEY_27, press); break;

        case CC_DELETE:
            delete_ = press;
            if(shift_ || (e.MenuActive() && e.PresetMode() != 0))
                e.Button(KEY_23, press);
            break;

        case CC_COPY:
            if(shift_ || e.MenuActive())
                e.Button(KEY_24, press);
            break;

        case CC_CAPTURE:
            if(shift_ || e.MenuActive())
                e.Button(KEY_25, press);
            break;

        case CC_LEFT:
        case CC_RIGHT:
            if(press)
            {
                page_ = cc == CC_RIGHT ? 1 : 0;
                Show("KNOBS", page_ ? "PAGE 2" : "PAGE 1");
            }
            break;

        case CC_UP:
        case CC_DOWN:
            if(press)
            {
                e.Eng().setOctave(cc == CC_UP ? 1 : -1);
                char v[8];
                snprintf(v, sizeof(v), "%+d", e.Eng().getOctave());
                Show("OCTAVE", v);
            }
            break;

        case CC_TRACK1:
            if(press)
            {
                e.TapTempo();
                char v[16];
                snprintf(v, sizeof(v), "%d BPM", e.Clock().getTempo() / 2);
                Show("TAP TEMPO", v);
            }
            break;
        case CC_TRACK1 - 1:
            if(press)
            {
                e.Eng().setPitchLfoOn(!e.Eng().getPitchLfoOn());
                Show("PITCH LFO", e.Eng().getPitchLfoOn() ? "ON" : "OFF");
            }
            break;
        case CC_TRACK1 - 2:
            if(press)
            {
                e.Eng().setFilterLfoOn(!e.Eng().getFilterLfoOn());
                Show("FILTER LFO", e.Eng().getFilterLfoOn() ? "ON" : "OFF");
            }
            break;

        case CC_MENU:
        case CC_JOG_CLICK:
            if(!press)
                break;
            if(settings_ && cc == CC_JOG_CLICK)
                SettingsActivate(shift_ ? -1 : 1);
            else
                settings_ = !settings_;
            exit_prompt_ = false;
            break;

        case CC_BACK:
            if(!press)
                break;
            if(settings_)
            {
                settings_ = false;
                break;
            }
            if(exit_prompt_ && e.NowMs() - exit_prompt_t_ < 3000)
                exit_ = true;
            else
            {
                exit_prompt_   = true;
                exit_prompt_t_ = e.NowMs();
            }
            break;
    }
}

int Surface::NextPreset(int from, int dir)
{
    // presets 1..14 that hold a patch, then 15 (the defaults), wrapping
    int s = from;
    for(int i = 0; i < 15; i++)
    {
        s += dir;
        if(s > 15)
            s = 1;
        if(s < 1)
            s = 15;
        if(eng_.SlotValid(s))
            return s;
    }
    return from;
}

/* ---- settings ------------------------------------------------------------- */

static const char *kSettingNames[kNumSettings] = {
    "MIDI IN CH", "MIDI OUT CH", "CLOCK OUT", "CC IN", "CC OUT", "PAD VELOCITY", "EXIT",
};

void Surface::SettingsActivate(int dir)
{
    WaveOptions &o = eng_.MutableOpts();
    switch(settings_cursor_)
    {
        case 0: o.midi_ch_in = (o.midi_ch_in + 16 + dir) % 16; break;
        case 1: o.midi_ch_out = (o.midi_ch_out + 16 + dir) % 16; break;
        case 2: o.midi_clock_out = !o.midi_clock_out; break;
        case 3: o.midi_cc_in = !o.midi_cc_in; break;
        case 4: o.midi_cc_out = !o.midi_cc_out; break;
        case 5: o.pad_velocity = !o.pad_velocity; break;
        case 6: exit_ = true; return;
    }
    eng_.OptionsChanged();
}

/* ---- knob text ------------------------------------------------------------ */

static void pct(char *out, float v) { snprintf(out, 24, "%d%%", (int)lrintf(v * 100.f)); }
static float lfo_hz(float v) { return .14f * powf(65.41f / .14f, v); }

void Surface::ParamText(int p, char *value)
{
    myEngine &x = eng_.Eng();
    switch(p)
    {
        case P_TABLE:
            snprintf(value, 24, "%d / %d", x.getTable() + 1, eng_.TablesLoaded());
            return;
        case P_FRAME: snprintf(value, 24, "%d / 33", x.getCycle() + 1); return;
        case P_FILTER:
        {
            float v = eng_.EncValue(1, 3);
            if(fabsf(v - .5f) < .02f)
                strcpy(value, "OPEN");
            else
                snprintf(value, 24, "%s %d", v < .5f ? "LP" : "HP",
                         (int)lrintf(fabsf(v - .5f) * 200.f));
            return;
        }
        case P_RES: pct(value, eng_.Resonance()); return;
        case P_ATTACK:
        case P_ATTACK_C: snprintf(value, 24, "%.2fs", eng_.EncValue(0, 1) * 5.f); return;
        case P_RELEASE:
        case P_RELEASE_C: snprintf(value, 24, "%.2fs", eng_.EncValue(0, 2)); return;
        case P_SPACE:
        {
            float v = eng_.EncValue(0, 3);
            if(fabsf(v - .5f) < .01f)
                strcpy(value, "DRY");
            else
                snprintf(value, 24, "%s %d%%", v < .5f ? "DLY" : "REV",
                         (int)lrintf(fabsf(v - .5f) * 200.f));
            return;
        }
        case P_TEMPO: snprintf(value, 24, "%d BPM", eng_.Clock().getTempo() / 2); return;
        case P_STEP: strcpy(value, kDivNames[eng_.Clock().divPos() % 5]); return;
        case P_PITCH: snprintf(value, 24, "%+.1f ST", (eng_.EncValue(0, 0) - .5f) * 24.f); return;
        case P_PITCH_ST:
            snprintf(value, 24, "%+d ST", (int)lrintf((eng_.EncValue(0, 0) - .5f) * 24.f));
            return;
        case P_PLFO_D: pct(value, eng_.EncValue(1, 1)); return;
        case P_PLFO_R: snprintf(value, 24, "%.2f HZ", lfo_hz(eng_.PitchLfoRate())); return;
        case P_FLFO_D: pct(value, eng_.EncValue(1, 2)); return;
        case P_FLFO_R: snprintf(value, 24, "%.2f HZ", lfo_hz(eng_.FilterLfoRate())); return;
        case P_TIME: pct(value, eng_.DelayTime()); return;
        case P_PAN:
        {
            float v = eng_.EncValue(1, 5);
            if(fabsf(v - .5f) < .01f)
                strcpy(value, "C");
            else
                snprintf(value, 24, "%s%d", v < .5f ? "L" : "R", (int)lrintf(fabsf(v - .5f) * 200.f));
            return;
        }
        case P_COMP: pct(value, eng_.FinalComp()); return;
        case P_VOLUME: pct(value, eng_.EncValue(0, 5)); return;
    }
    value[0] = 0;
}

float Surface::ParamValue(int p)
{
    myEngine &x = eng_.Eng();
    switch(p)
    {
        case P_TABLE:
            return eng_.TablesLoaded() > 1 ? x.getTable() / (float)(eng_.TablesLoaded() - 1) : 0.f;
        case P_FRAME: return x.getCycle() / 32.f;
        case P_FILTER: return eng_.EncValue(1, 3);
        case P_RES: return eng_.Resonance();
        case P_ATTACK: return eng_.EncValue(0, 1);
        case P_RELEASE: return eng_.EncValue(0, 2);
        case P_SPACE: return eng_.EncValue(0, 3);
        case P_TEMPO: return (eng_.Clock().getTempo() - 160) / 320.f;
        case P_PITCH: return eng_.EncValue(0, 0);
        case P_PLFO_D: return eng_.EncValue(1, 1);
        case P_PLFO_R: return eng_.PitchLfoRate();
        case P_FLFO_D: return eng_.EncValue(1, 2);
        case P_FLFO_R: return eng_.FilterLfoRate();
        case P_TIME: return eng_.DelayTime();
        case P_PAN: return eng_.EncValue(1, 5);
        case P_COMP: return eng_.FinalComp();
    }
    return 0.f;
}

/* ---- LEDs ----------------------------------------------------------------- */

uint8_t Surface::NormalKeyColor(int id)
{
    if(eng_.Eng().isKeyPlaying(id))
        return C_WHITE;
    return is_black(KeyIdToNote(id)) ? C_GREY_DARK : C_GREY_DIM;
}

/* MenuPage: function keys, then the preset slots on the white keys */
uint8_t Surface::MenuKeyColor(int id)
{
    myEngine  &x     = eng_.Eng();
    const int  pm    = eng_.PresetMode();
    const bool blink = (eng_.NowMs() / 250) & 1;
    const float gate = eng_.Seq().getGate();

    switch(id)
    {
        case KEY_16: return x.getOctave() < 0 ? C_AZURE : C_GREY_DARK;
        case KEY_17: return x.getOctave() > 0 ? C_AZURE : C_GREY_DARK;
        case KEY_18: return gate < .2f ? C_PINK : C_OFF;
        case KEY_19: return gate > .2f && gate < .9f ? C_PINK : C_OFF;
        case KEY_20: return gate > .9f ? C_PINK : C_OFF;
        case KEY_21: return x.getPitchLfoOn() ? C_YELLOW : C_OFF;
        case KEY_22: return x.getFilterLfoOn() ? C_YELLOW : C_OFF;
        case KEY_23:
            return (pm == MunchiWave::PM_ERASE_SEL || pm == MunchiWave::PM_ERASING
                    || pm == MunchiWave::PM_NONE)
                       ? C_RED
                       : C_OFF;
        case KEY_24:
            return (pm == MunchiWave::PM_COPY_SRC || pm == MunchiWave::PM_COPY_DEST
                    || pm == MunchiWave::PM_COPYING || pm == MunchiWave::PM_NONE)
                       ? C_GREEN
                       : C_OFF;
        case KEY_25:
            return (pm == MunchiWave::PM_SAVE_SEL || pm == MunchiWave::PM_SAVING
                    || pm == MunchiWave::PM_NONE)
                       ? C_BLUE
                       : C_OFF;
    }

    int s = KeyToSlot(id);
    if(s == kSlotNone)
        return C_OFF;
    const int sel = eng_.SelectedSlot(), src = eng_.CopySrc();
    if(s == sel && pm == MunchiWave::PM_SAVE_SEL)
        return C_BLUE;
    if(s == sel && pm == MunchiWave::PM_ERASE_SEL)
        return C_RED;
    if(s == sel && pm == MunchiWave::PM_COPY_DEST)
        return C_BLUE;
    if(s == src && pm == MunchiWave::PM_COPY_DEST)
        return C_GREEN;
    if(s == eng_.Eng().getVoiceSlot() && pm == MunchiWave::PM_NONE)
        return C_WHITE;
    if(pm == MunchiWave::PM_SAVE_SEL || pm == MunchiWave::PM_ERASE_SEL
       || pm == MunchiWave::PM_COPY_SRC || pm == MunchiWave::PM_COPY_DEST)
    {
        if(!blink || s == 15)
            return C_OFF;
        if(eng_.SlotValid(s))
            return C_PURPLE_DIM;
        return (pm == MunchiWave::PM_SAVE_SEL || pm == MunchiWave::PM_COPY_DEST) ? C_GREY
                                                                                 : C_OFF;
    }
    if(s == 15)
        return C_PINK;
    return eng_.SlotValid(s) ? C_PURPLE : C_OFF;
}

void Surface::ComputeLeds()
{
    const bool menu = eng_.MenuActive();
    const uint32_t now = eng_.NowMs();
    Sequencer &seq = eng_.Seq();

    for(int p = 0; p < 32; p++)
    {
        int id       = PadToKey(p);
        want_pad_[p] = id < 0 ? C_OFF : (menu ? MenuKeyColor(id) : NormalKeyColor(id));
    }
    for(int s = 0; s < 16; s++)
    {
        int slot = s + 1;
        uint8_t c = C_OFF;
        if(slot == eng_.Eng().getVoiceSlot())
            c = C_WHITE;
        else if(slot == 15)
            c = C_PINK_DIM;
        else if(slot < 15 && eng_.SlotValid(slot))
            c = C_PURPLE_DIM;
        want_step_[s] = c;
    }

    memset(want_cc_, 0, sizeof(want_cc_));
    auto set = [&](int cc, uint8_t v) {
        want_cc_[cc] = v;
        cc_used_[cc] = true;
    };
    // play: green while running (flashing on the step), grey with a sequence
    uint8_t play = C_OFF;
    if(seq.getPlaying())
        play = seq.getLeftLights() ? C_GREEN : C_TEAL;
    else if(seq.getSequence())
        play = C_GREY;
    // loop: red while recording; blinking when the sequence is full
    uint8_t loop = seq.getRecording() ? C_RED : (seq.getSequence() ? C_GREY_DIM : C_OFF);
    set(CC_PLAY, play);
    set(CC_LOOP, loop);
    set(CC_REC, loop);
    set(CC_SAMPLE, seq.muted ? C_RED : (seq.getRecording() ? C_GREY : C_OFF));
    set(CC_SHIFT, menu ? C_WHITE : C_OFF);
    // track 1 flashes the beat
    {
        float beat_ms = 60000.f / (eng_.Clock().getTempo() / 2.f);
        set(CC_TRACK1, fmodf((float)now, beat_ms) < 60.f ? C_WHITE : C_GREY_DARK);
    }
    set(CC_TRACK1 - 1, eng_.Eng().getPitchLfoOn() ? C_WHITE : C_GREY_DARK);
    set(CC_TRACK1 - 2, eng_.Eng().getFilterLfoOn() ? C_WHITE : C_GREY_DARK);
    set(CC_TRACK4, C_OFF);
    set(CC_LEFT, page_ == 1 ? C_WHITE : C_OFF);
    set(CC_RIGHT, page_ == 0 ? C_WHITE : C_OFF);
    set(CC_UP, eng_.Eng().getOctave() < 1 ? C_GREY : C_OFF);
    set(CC_DOWN, eng_.Eng().getOctave() > -1 ? C_GREY : C_OFF);
    set(CC_MENU, settings_ ? C_WHITE : C_GREY_DARK);
    set(CC_BACK, exit_prompt_ ? C_WHITE : C_GREY_DARK);
    set(CC_DELETE, menu ? C_RED : C_GREY_DARK);
    set(CC_COPY, menu ? C_GREEN : C_OFF);
    set(CC_CAPTURE, menu ? C_BLUE : C_OFF);
}

static inline bool put_pkt(uint8_t *spi, int *n, uint8_t cin_cable, uint8_t st,
                           uint8_t d1, uint8_t d2)
{
    if(*n >= SCHWUNG_MIDI_OUT_MAX)
        return false;
    uint8_t *p = spi + SCHWUNG_OFF_OUT_MIDI + (*n) * 4;
    p[0] = cin_cable;
    p[1] = st;
    p[2] = d1;
    p[3] = d2;
    (*n)++;
    return true;
}

void Surface::Tick(uint8_t *spi)
{
    tick_++;
    int n = 0;

    // MIDI out to USB-A (cable 2): clock and transport are 1-byte system
    // messages (CIN 0xF / 0x5), notes and CCs their channel CIN
    Hardware::Msg m;
    while(n < 8 && eng_.PopMidiOut(&m))
    {
        if(m.len == 1)
            put_pkt(spi, &n, 0x2F, m.bytes[0], 0, 0);
        else
            put_pkt(spi, &n, (uint8_t)(0x20 | (m.bytes[0] >> 4)), m.bytes[0], m.bytes[1],
                    m.bytes[2]);
    }

    if((tick_ & 7) == 0)
        ComputeLeds();

    refresh_i_ = (refresh_i_ + 1) % (32 + 16 + 128);
    if(refresh_i_ < 32)
        sent_pad_[refresh_i_] = 0xFF;
    else if(refresh_i_ < 48)
        sent_step_[refresh_i_ - 32] = 0xFF;
    else
        sent_cc_[refresh_i_ - 48] = 0xFF;

    for(int p = 0; p < 32 && n < SCHWUNG_MIDI_OUT_MAX; p++)
        if(want_pad_[p] != sent_pad_[p]
           && put_pkt(spi, &n, 0x09, 0x90, (uint8_t)(68 + p), want_pad_[p]))
            sent_pad_[p] = want_pad_[p];
    for(int s = 0; s < 16 && n < SCHWUNG_MIDI_OUT_MAX; s++)
        if(want_step_[s] != sent_step_[s]
           && put_pkt(spi, &n, 0x09, 0x90, (uint8_t)(16 + s), want_step_[s]))
            sent_step_[s] = want_step_[s];
    for(int c = 0; c < 128 && n < SCHWUNG_MIDI_OUT_MAX; c++)
        if(cc_used_[c] && want_cc_[c] != sent_cc_[c]
           && put_pkt(spi, &n, 0x0B, 0xB0, (uint8_t)c, want_cc_[c]))
            sent_cc_[c] = want_cc_[c];

    if(disp_.AtFrameStart())
        Draw();
    disp_.PushSlice(spi);
}

int Surface::WriteAllOff(uint8_t *spi, int start)
{
    int n = 0, i = start;
    for(; i < 32 + 16 + 128 && n < SCHWUNG_MIDI_OUT_MAX; i++)
    {
        if(i < 32)
            put_pkt(spi, &n, 0x09, 0x90, (uint8_t)(68 + i), 0);
        else if(i < 48)
            put_pkt(spi, &n, 0x09, 0x90, (uint8_t)(16 + i - 32), 0);
        else if(cc_used_[i - 48])
            put_pkt(spi, &n, 0x0B, 0xB0, (uint8_t)(i - 48), 0);
    }
    return i >= 32 + 16 + 128 ? -1 : i;
}

/* ---- screen --------------------------------------------------------------- */

void Surface::Draw()
{
    disp_.Clear();
    if(exit_prompt_ && eng_.NowMs() - exit_prompt_t_ > 3000)
        exit_prompt_ = false;
    if(exit_prompt_)
    {
        disp_.TextCentered(18, "EXIT MUNCHI WAVE?");
        disp_.TextCentered(34, "BACK AGAIN TO EXIT");
        disp_.TextCentered(46, "THE SEQUENCE IS LOST");
        return;
    }
    if(settings_)
        DrawSettings();
    else
        DrawMain();
}

void Surface::DrawSettings()
{
    disp_.Text(0, 0, "SETTINGS");
    disp_.HLine(0, 9, 128);
    const WaveOptions &o = eng_.Opts();
    int first = settings_cursor_ - 4 < 0 ? 0 : settings_cursor_ - 4;
    for(int row = 0; row < 5; row++)
    {
        int i = first + row;
        if(i > kNumSettings - 1)
            break;
        char v[16] = "";
        switch(i)
        {
            case 0: snprintf(v, sizeof(v), "%d", o.midi_ch_in + 1); break;
            case 1: snprintf(v, sizeof(v), "%d", o.midi_ch_out + 1); break;
            case 2: strcpy(v, o.midi_clock_out ? "ON" : "OFF"); break;
            case 3: strcpy(v, o.midi_cc_in ? "ON" : "OFF"); break;
            case 4: strcpy(v, o.midi_cc_out ? "ON" : "OFF"); break;
            case 5: strcpy(v, o.pad_velocity ? "ON" : "OFF"); break;
        }
        int y = 12 + row * 10;
        disp_.Text(2, y, kSettingNames[i]);
        disp_.TextRight(126, y, v);
        if(i == settings_cursor_)
            disp_.Invert(0, y - 1, 128, 9);
    }
}

void Surface::DrawMain()
{
    myEngine  &x   = eng_.Eng();
    Sequencer &seq = eng_.Seq();
    char       buf[40];

    if(!eng_.Ready())
    {
        disp_.TextCentered(28, "LOADING WAVETABLES");
        return;
    }

    // header: preset | table and frame
    if(eng_.Eng().getVoiceSlot() == 15)
        snprintf(buf, sizeof(buf), "WAVE DEFAULT");
    else
        snprintf(buf, sizeof(buf), "WAVE P%d", eng_.Eng().getVoiceSlot());
    disp_.Text(0, 0, buf);
    snprintf(buf, sizeof(buf), "T%d F%d", x.getTable() + 1, x.getCycle() + 1);
    disp_.TextRight(128, 0, buf);

    // the current frame, drawn
    {
        const int y0 = 10, h = 12;
        float(*tab)[MAX_SAMPLES_PER_CYCLE] = x.myVoices[0].wt.wavetableMemory_;
        int cyc = x.getCycle();
        int prev = -1;
        for(int px = 0; px < 128; px++)
        {
            float v  = tab[cyc][px * MAX_SAMPLES_PER_CYCLE / 128];
            int   py = y0 + h / 2 - (int)lrintf(v * (h / 2 - 1));
            if(prev >= 0)
            {
                int a = prev < py ? prev : py, b = prev < py ? py : prev;
                disp_.VLine(px, a, b - a + 1);
            }
            else
                disp_.Pixel(px, py);
            prev = py;
        }
    }

    const uint32_t now = eng_.NowMs();
    int y = 25;
    if(eng_.MenuActive())
    {
        const char *l1 = "SHIFT", *l2 = "PADS: OCT GATE LFO", *l3 = "WHITE KEYS: PRESET";
        switch(eng_.PresetMode())
        {
            case MunchiWave::PM_ERASE_SEL: l1 = "ERASE"; l2 = "PICK A PRESET"; l3 = "SHIFT TO CONFIRM"; break;
            case MunchiWave::PM_COPY_SRC: l1 = "COPY"; l2 = "PICK SOURCE"; l3 = ""; break;
            case MunchiWave::PM_COPY_DEST: l1 = "COPY"; l2 = "PICK DESTINATION"; l3 = "SHIFT TO CONFIRM"; break;
            case MunchiWave::PM_SAVE_SEL: l1 = "SAVE PRESET"; l2 = "PICK A SLOT"; l3 = "SHIFT TO CONFIRM"; break;
            case MunchiWave::PM_SAVING: l1 = "SAVED"; l2 = ""; l3 = ""; break;
            case MunchiWave::PM_COPYING: l1 = "COPIED"; l2 = ""; l3 = ""; break;
            case MunchiWave::PM_ERASING: l1 = "ERASED"; l2 = ""; l3 = ""; break;
        }
        disp_.Text(0, y, l1);
        disp_.Text(0, y + 10, l2);
        disp_.Text(0, y + 20, l3);
        return;
    }
    if(show_t_ && now - show_t_ < 1500)
    {
        disp_.Text(0, y, show_name_);
        disp_.Text(0, y + 10, show_value_, 2);
    }
    else
    {
        for(int i = 0; i < 8; i++)
        {
            int p  = kPage[page_][i];
            int cx = (i % 4) * 32, cy = y + (i / 4) * 13;
            disp_.Text(cx, cy, kParams[p].abbrev);
            disp_.Frame(cx, cy + 8, 28, 3);
            disp_.HLine(cx, cy + 9, 1 + (int)(ParamValue(p) * 27));
        }
    }

    // sequencer line
    const char *state = seq.getRecording() ? "REC" : seq.getPlaying() ? "PLAY" : "SEQ";
    if(seq.sequenceLength)
        snprintf(buf, sizeof(buf), "%s %d/%d", state, seq.getPlaying() ? seq.currentIdx + 1 : 0,
                 seq.sequenceLength);
    else
        snprintf(buf, sizeof(buf), "%s -", state);
    disp_.Text(0, 56, buf);
    snprintf(buf, sizeof(buf), "%d %s", eng_.Clock().getTempo() / 2,
             kDivNames[eng_.Clock().divPos() % 5]);
    disp_.TextRight(128, 56, buf);
}

} // namespace munchi
