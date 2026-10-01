/* surface.h -- Move's controls, played as a CHOMPI running WAVE.
 *
 * Every Move control is turned into the CHOMPI key or encoder it stands for;
 * munchi_wave.cpp (the firmware's own page logic) decides what that means.
 * This file owns the translation, the lights and the screen. In short:
 *
 *   pads          the 25-key keyboard, two octaves as piano rows
 *   Shift         the CHOMPI key in play mode: the menu layer (octave, gate,
 *                 LFO switches, erase/copy/save, preset slots on the keys)
 *   Sample        the CHOMPI key in record mode: a rest while recording the
 *                 sequence, otherwise mute it while held
 *   Play / Loop   the sequencer's two keys (Rec is a second Loop)
 *   knobs 1-8     page 1: Table, Frame, Filter, Resonance, Attack, Release,
 *                 Space, Tempo; page 2 (Right): Pitch, Pitch LFO depth/rate,
 *                 Filter LFO depth/rate, Delay time, Pan, Compressor
 *   volume knob   Volume;  Shift: pitch in semitones, coarse attack/release,
 *                 step length (the menu page's encoder gestures)
 *   Delete+touch  an encoder click (reset)
 *   Track 1-3     tap tempo, pitch LFO on/off, filter LFO on/off
 *   steps 1-15    presets 1-14 and the defaults; jog browses them
 *   Up/Down       octave;  Menu / jog click: settings;  Back x2: exit
 */
#pragma once
#include <stdint.h>
#include "display.h"
#include "../engine/munchi_wave.h"

namespace munchi
{

class Surface
{
  public:
    explicit Surface(MunchiWave &e);

    void HandleInternal(uint8_t status, uint8_t d1, uint8_t d2);
    void Tick(uint8_t *spi);
    void InvalidateLeds();
    bool WantsExit() const { return exit_; }
    /** Tests: draw the current screen and return its pixels (128 x 64). */
    const uint8_t *TestScreen() { Draw(); return disp_.Pixels(); }
    int  WriteAllOff(uint8_t *spi, int start);

  private:
    int  PadToKey(int pad) const;
    void OnPad(int pad, int vel, bool on);
    void OnStep(int step, bool on);
    void OnKnob(int knob, int delta);
    void OnKnobTouch(int knob, bool on);
    void OnButton(int cc, bool press);
    void Show(const char *name, const char *value);
    int   KnobParam(int knob, bool shift) const;
    void  ParamText(int param, char *value);
    float ParamValue(int param);
    void SettingsActivate(int dir);
    int  NextPreset(int from, int dir);

    void    ComputeLeds();
    uint8_t NormalKeyColor(int id);
    uint8_t MenuKeyColor(int id);

    void Draw();
    void DrawMain();
    void DrawSettings();

    MunchiWave &eng_;
    Display     disp_;

    bool shift_ = false, delete_ = false;
    int  page_  = 0;
    int  pad_hold_[64] = {0};

    char     show_name_[24] = {0}, show_value_[24] = {0};
    uint32_t show_t_ = 0;

    bool     settings_ = false;
    int      settings_cursor_ = 0;
    bool     exit_prompt_ = false;
    uint32_t exit_prompt_t_ = 0;
    bool     exit_ = false;

    uint8_t  want_pad_[32], sent_pad_[32];
    uint8_t  want_step_[16], sent_step_[16];
    uint8_t  want_cc_[128], sent_cc_[128];
    bool     cc_used_[128] = {false};
    int      refresh_i_ = 0;
    uint32_t tick_ = 0;
};

} // namespace munchi
