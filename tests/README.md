# Tests

`render.cpp` drives the engine offline: it loads a card, runs a script of
key/knob/menu events in engine time, and writes the output WAV.

```bash
c++ -std=c++17 -O2 -Isrc/engine tests/render.cpp src/engine/munchi_wave.cpp \
    src/engine/daisysp/*.cpp -o build-host/render
./scripts/fetch-card.sh build/card
MUNCHI_CARD=/tmp/scratch ./build-host/render build/card tests/scripts/basics.txt out.wav
```

Key ids are CHOMPI `Hardware::SwId` values (src/engine/munchi_wave.h):
15 = C3, 18 = C4, 28 = C5, 33 = Play, 34 = Loop, 31 = Save (A#4).
`slot N` selects a preset; `menukey 1/0` is Shift; `rest 1/0` is Sample.

`synth_test.cpp` drives the sound generator the way the chain host does:
create, wait for the card, read `chain_params` / `ui_hierarchy`, play a chord,
change preset, round-trip `state` into a second instance. It writes the audio
and both contract strings next to the output WAV.

```bash
c++ -std=c++17 -O2 -Isrc/synth -Isrc/engine tests/synth_test.cpp \
    src/engine/wave_card.cpp src/engine/daisysp/*.cpp -o build-host/synth_test
./build-host/synth_test build out.wav     # build/card holds the factory card
```
