# Third-party work in Munchi Wave

Munchi Wave is a port of **CHOMPI WAVE 1.0** to Ableton Move. Everything it is
built from is MIT-licensed; the notices below travel with every copy.

| Component | Copyright | License | Where |
|---|---|---|---|
| CHOMPI WAVE 1.0 firmware (engine, sequencer, clock, UI logic) | © CHOMPI Club | MIT | `src/engine/wave/` (originals of edited files in `src/engine/wave/upstream/`) |
| CHOMPI WAVE 1.0 factory card (7 wavetables, presets.json, options.json) | © CHOMPI Club | MIT | fetched at build time into `card/` from [CHOMPI-Club/CHOMPI](https://github.com/CHOMPI-Club/CHOMPI) @ `a73d7326` |
| DaisySP (ADSR, oscillator, SVF, DC block, delay line, dsp utilities) | © Electrosmith, Corp. | MIT | `src/engine/daisysp/` (its `LICENSE` beside it) |
| `reverb.h`, `fx_engine.h`, `limiter.h` | © Émilie Gillet (Mutable Instruments) | MIT | `src/engine/wave/` — original notices kept in the files |
| 5x7 font | Schwung standalone example | MIT | `src/standalone/font.cpp` |

Authorship of the firmware, per the CHOMPI repository's own `THIRD_PARTY.md`
(copied to `docs/CHOMPI-THIRD_PARTY.md`): Electrosmith engineered the original
platform; TAPE 2.0, TEMPO and WAVE were written at Chase Bliss after CHOMPI Club
became part of Chase Bliss. CHOMPI Club releases them under the MIT license.

## Trademarks

The CHOMPI name, logo, character and related marks are trademarks of CHOMPI
Club and are **not** covered by the MIT license (`docs/CHOMPI-TRADEMARKS.md`).
Munchi Wave is an independent port, not an official CHOMPI Club release; it is
named differently for that reason and uses no CHOMPI artwork.
