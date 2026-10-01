/* wave_card.h -- WAVE's card formats, shared by the Munchi Wave standalone
 * tool and the Munchi Wave sound generator:
 *
 *   WavePresets          PresetManager.h: presets.json, format v3
 *   LoadWavetable        one wavetable .wav (33 frames x 2048)
 *   LoadWavetableFolder  the first seven .wav files in a folder, A-Z
 *
 * File I/O: call from a loader or worker thread, never the audio thread.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>
#include "wave/WavetableManager.h"

namespace munchi
{

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

bool LoadWavetable(const std::string &path, float (*dst)[MAX_SAMPLES_PER_CYCLE]);
int  LoadWavetableFolder(const std::string &dir, float (*mem)[MAX_SAMPLES_PER_CYCLE], int max_tables);

} // namespace munchi
