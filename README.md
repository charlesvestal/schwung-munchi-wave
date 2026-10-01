# Munchi Wave

**CHOMPI WAVE 1.0 on Ableton Move.** An eight-voice wavetable synth with a DJ
filter, two LFOs, a 32-step sequencer, delay and reverb, running the actual WAVE
firmware code.

Munchi Wave is a [Schwung](https://github.com/charlesvestal/schwung) standalone
tool. Launched from Schwung's Tools menu, it stops Move and runs the whole device
itself (pads, knobs, buttons, lights, screen, audio). Back twice hands Move back.
Its sibling is [Munchi Tape](https://github.com/charlesvestal/schwung-munchi-tape),
the CHOMPI's sampler and looper.

It is a port of the firmware CHOMPI Club released as open source
([CHOMPI-Club/CHOMPI](https://github.com/CHOMPI-Club/CHOMPI), MIT). It is not an
official CHOMPI Club release; see [Credits](#credits).

**→ [The manual](docs/MANUAL.md)**: a first session, every control, the
sequencer, presets and wavetables.

## At a glance

- 8 voices, each a wavetable oscillator (7 tables × 33 frames, crossfaded) into
  an envelope and the CHOMPI's DJ filter
- Pitch and filter LFOs with depth, rate and on/off
- 32-step sequencer, recorded by playing, with gate length, step length and tap
  tempo; MIDI clock out
- Delay or reverb on one knob, an output compressor that turns into saturation
- 14 presets plus a defaults slot; the wavetable folder, `presets.json` and
  `options.json` are in WAVE's own formats, so they move to and from a CHOMPI's SD card

## Quick start

| Move | Does |
|---|---|
| Pads | the keyboard, two octaves as piano rows |
| Jog / steps 1–15 | presets (15 = defaults) |
| Knobs 1–8 | Pitch, Attack, Release, Space, Filter, Table, Tempo, Pan |
| Left / Right | knobs 1–3 become Frame, Pitch LFO, Filter LFO |
| Volume knob | Volume |
| Loop / Play | record / play the sequence |
| Sample | a rest while recording; mute while held |
| Shift (hold) | the CHOMPI menu: octave, gate, LFO switches, save/copy/erase |
| Track 1 / 2 / 3 | tap tempo / pitch LFO / filter LFO |
| Menu | settings |
| Back ×2 | exit |

## Install

From a release: download `munchi-wave-module.tar.gz` from the
[latest release](https://github.com/charlesvestal/schwung-munchi-wave/releases/latest/download/munchi-wave-module.tar.gz)
and install it with the Schwung web manager's custom-module upload, or paste
this repository's URL into its custom install. From a build:

```bash
./scripts/build.sh      # fetches the factory card, cross-compiles in Docker
./scripts/install.sh    # copies dist/munchi-wave to the Move
```

Then Tools menu (Shift + Volume + Step 13) → **Munchi Wave**.

## How it is built

| Path | What |
|---|---|
| `src/engine/wave/` | the WAVE engine, wavetable oscillator, sequencer and clock, from the firmware. Edited files keep their originals in `wave/upstream/` |
| `src/engine/munchi_wave.*` | the firmware's page logic (`NormalPage`, `MenuPage`, `ui.h`, `MidiManager`'s input), presets, options, wavetable loading, MIDI clock |
| `src/engine/resampler.h` | the engine runs at its native 48 kHz; this converts to and from Move's 44.1 kHz |
| `src/standalone/` | the SPI loop, the Move control mapping, lights and screen |
| `scripts/fetch-card.sh` | fetches the factory card from the CHOMPI repo at a pinned commit |
| `tests/render.cpp` | offline harness: scripted key/knob/menu events in, a WAV out |

```bash
c++ -std=c++17 -O2 -Isrc/engine tests/render.cpp src/engine/munchi_wave.cpp \
    src/engine/daisysp/*.cpp -o build-host/render
./scripts/fetch-card.sh build/card
MUNCHI_CARD=/tmp/scratch ./build-host/render build/card tests/scripts/basics.txt out.wav
```

## Credits

- **CHOMPI WAVE 1.0:** CHOMPI Club / Chase Bliss. MIT. The CHOMPI name, logo and
  character are CHOMPI Club's trademarks and are not licensed; this port is named
  Munchi Wave for that reason.
- **DaisySP:** Electrosmith. MIT.
- **Reverb, FX engine, limiter:** Émilie Gillet, Mutable Instruments. MIT.

See [THIRD_PARTY.md](THIRD_PARTY.md). Munchi Wave itself is MIT ([LICENSE](LICENSE)).
