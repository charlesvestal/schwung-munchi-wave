/* munchi_wave.h -- CHOMPI WAVE 1.0, minus the hardware.
 *
 *   audio      WAVE's engine (wave/subtractiveEngine.h) at its native 48 kHz,
 *              resampled to and from Move's 44.1 kHz.
 *   controls   NormalPage + MenuPage + ui.h + MidiManager's input side, with
 *              their switch statements kept, driven by CHOMPI key ids.
 *   card       wavetables, presets.json and options.json in a folder, as on
 *              the CHOMPI's SD card; a worker does the writes.
 *
 * One thread (the SPI loop) runs audio and controls, as the firmware did.
 */
#pragma once
#include "daisy_compat.h"
#include "wave/Sequencer.h"
#include "wave/wave_hardware.h"
#include "resampler.h"
#include <atomic>
#include <string>
#include <thread>

namespace munchi
{

/* CHOMPI Hardware::SwId */
enum SwId
{
    ENC_1_SW = 0, ENC_2_SW, ENC_3_SW, ENC_4_SW, NC_6,
    KEY_26, // the CHOMPI key
    SW_TOG,
    KEY_16, KEY_2, KEY_3, KEY_4, KEY_5, KEY_17, KEY_18, KEY_19, KEY_1,
    KEY_6, KEY_7, KEY_8, KEY_9, KEY_10, KEY_20, KEY_21, KEY_22,
    KEY_11, KEY_12, KEY_13, KEY_14, KEY_15, KEY_23, KEY_24, KEY_25,
    ENC_6_SW,
    KEY_27, // play
    KEY_28, // loop
    NC_1, NC_2, NC_3, NC_4, NC_5,
    SR_LAST,
};
static constexpr int ENC_5_SW = 4;

int KeyIdToNote(int key_id); // 48..72, or -1
int NoteToKeyId(int note);   // inverse, -1 outside the 25 keys
int KeyToSlot(int key_id);   // white key -> preset slot 1..15, else kSlotNone

struct WaveOptions
{
    int  midi_ch_in     = 0; // 0-based
    int  midi_ch_out    = 0;
    bool midi_clock_out = true;
    bool midi_cc_in     = true;
    bool midi_cc_out    = true;
    bool pad_velocity   = false; // Munchi
};

/** PresetManager (WAVE): 14 slots x 14 controls, presets.json format v3. */
struct WavePresets
{
    static constexpr int kSlots    = 14;
    static constexpr int kControls = 14;
    float slotValues[15][kControls];
    bool  slotValid[15];
    bool  updated = false;

    void  Init(const float *defaults);
    bool  IsValid(size_t slot);
    float GetValue(size_t slot, size_t control);
    void  SetValue(float value, size_t slot, size_t control);
    void  Invalidate(uint8_t slot);
    void  Save(size_t slot);
    void  Copy(uint8_t src, uint8_t dst);
    bool  Parse(const char *json);
    std::string Serialize() const;
};

class MunchiWave
{
  public:
    MunchiWave();
    ~MunchiWave();

    int  Init();
    /** Loads the card (seeding it from `factory_dir` on first run) and
     *  starts the writer thread. Wavetables are 2 MB: read before audio. */
    void StartCard(const std::string &card_dir, const std::string &factory_dir);
    void StopCard();
    int  TablesLoaded() const { return wt_loader_.numWavetables; }

    void ProcessHostBlock(const int16_t *in, int16_t *out, int frames);

    /* controls -- CHOMPI key ids and encoder indices, as in the firmware */
    void Button(int sw_id, bool rising);
    void Encoder(int knob, int page, int turns, bool menu);
    void SetKnobPage(int knob, int page) { knob_page_[knob] = page; }
    void MenuKey(bool rising);  // Shift: the CHOMPI key in play mode
    void RestKey(bool rising);  // Sample: the CHOMPI key in record mode
    void Click(int sw);         // an encoder click with the menu's meaning
    void TapTempo();            // ENC_5_SW in play mode
    void SelectSlot(int slot);  // a preset (1-14) or the defaults (15)
    void ClearSequence();
    void Midi(const uint8_t *msg, int len);
    bool PopMidiOut(Hardware::Msg *m) { return hw_.Pop(m); }
    void SetPadVelocity(float v) { pad_velocity_ = opts_.pad_velocity ? v : 127.f; }

    /* state for the lights and the screen */
    myEngine        &Eng() { return engine_; }
    Sequencer       &Seq() { return seq_; }
    clockManager    &Clock() { return clock_; }
    bool             MenuActive() const { return menu_active_; }
    int              PresetMode() const { return preset_mode_; }
    int              SelectedSlot() const { return selected_slot_; }
    int              CopySrc() const { return copy_src_; }
    bool             SlotValid(int s) { return s == 15 || presets_.IsValid(s); }
    float            EncValue(int page, int knob) const { return enc_values_[page][knob]; }
    float            DelayTime() const { return del_time_; }
    float            Resonance() const { return res_; }
    float            PitchLfoRate() const { return pitch_lfo_rate_; }
    float            FilterLfoRate() const { return filter_lfo_rate_; }
    float            FinalComp() const { return final_comp_; }
    int              Div() const { return div_shown_; }
    bool             Ready() const { return ready_; }
    const WaveOptions &Opts() const { return opts_; }
    WaveOptions     &MutableOpts() { return opts_; }
    void             OptionsChanged();
    void             LoadOptions(const std::string &path);
    void             SaveOptions();
    uint32_t         NowMs() const { return daisy::System::now_ms; }

    void ScriptCommand(const char *op, const char *a, float b);

    enum
    {
        PM_NONE = 0,
        PM_ERASE_SEL,
        PM_ERASING,
        PM_COPY_SRC,
        PM_COPY_DEST,
        PM_COPYING,
        PM_SAVE_SEL,
        PM_SAVING,
    };

  private:
    void RunEngineBlock();
    void UiTick();
    void NormalButton(int id, bool rising);
    void NormalEncoder(int enc, int turns, int steps_per_rev);
    void ApplyKnob(int knob, int page);
    bool MenuButton(int id, bool rising);
    void MenuEncoder(int enc, int turns);
    void MenuFocusGained();
    bool MenuIsClosable();
    void SetVoiceSlot(size_t slot);
    void DumpValuePresets(uint8_t slot);
    int  LoadTables();
    void WriterMain();

    myEngine         engine_;
    Sequencer        seq_;
    clockManager     clock_;
    Hardware         hw_;
    wavetableLoader  wt_loader_;
    daisysp::Reverb *reverb_ = nullptr;
    chompi::InterpolatedDelayLine::AudioSample *del_mem_ = nullptr;
    float (*table_mem_)[MAX_SAMPLES_PER_CYCLE] = nullptr;

    StreamResampler up_, down_;
    static constexpr int kEngineBlock = 48;
    float in48_[4][kEngineBlock];
    float out48_[4][kEngineBlock];
    int   in48_count_ = 0;
    float out44_l_[1024], out44_r_[1024];
    int   out44_count_ = 0;
    double clock_phase_ = 0.0;
    bool  ready_ = false;

    /* ui.h / NormalPage / MenuPage state */
    float   enc_values_[3][6];
    int     knob_page_[6]    = {0, 0, 0, 0, 0, 0};
    bool    switch_state_    = true; // play mode: the CHOMPI key is the menu
    bool    init_ignore_     = true;
    uint32_t init_time_      = 0;
    bool    key_note_on_[64] = {};
    uint8_t midi_note_sent_[64] = {};
    bool    menu_active_     = false;
    bool    menu_chompi_pressed_ = false;
    int     preset_mode_     = PM_NONE;
    uint8_t selected_slot_   = kSlotNone;
    uint8_t copy_src_        = kSlotNone;
    uint32_t blink_startt_   = 0;
    float   res_ = .63f, del_time_ = .4f;
    float   pitch_lfo_rate_ = .58f, filter_lfo_rate_ = .58f;
    float   pre_quantized_amount_ = .5f;
    float   final_comp_ = 0.f;
    int8_t  table_detent_acc_ = 0;
    bool    key_cc_[2] = {false, false};
    float   pad_velocity_ = 127.f;
    int     div_shown_ = 2;

    WaveOptions opts_;
    WavePresets presets_;

    /* writer thread: presets.json / options.json */
    std::string       card_dir_;
    std::thread       writer_;
    std::atomic<bool> writer_run_{false};
    std::atomic<bool> presets_ready_{false}, options_ready_{false};
    std::string       presets_snapshot_, options_snapshot_;
    uint32_t          presets_check_t_ = 0;
};

} // namespace munchi
