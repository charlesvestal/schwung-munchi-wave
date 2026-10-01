# Munchi Wave — Manual

Munchi Wave turns Ableton Move into a **CHOMPI running WAVE**: an eight-voice
wavetable synth with a DJ filter, two LFOs, a 32-step sequencer, delay and
reverb. It runs the actual CHOMPI WAVE 1.0 firmware code, so it behaves like the
hardware.

1. [What it is](#1-what-it-is)
2. [Starting and stopping](#2-starting-and-stopping)
3. [A first session](#3-a-first-session)
4. [The pads](#4-the-pads)
5. [The sound](#5-the-sound)
6. [The sequencer](#6-the-sequencer)
7. [Presets](#7-presets)
8. [The Shift layer](#8-the-shift-layer)
9. [Wavetables: the card](#9-wavetables-the-card)
10. [Settings](#10-settings)
11. [MIDI](#11-midi)
12. [The screen and the lights](#12-the-screen-and-the-lights)
13. [Control reference](#13-control-reference)
14. [Differences from a real CHOMPI](#14-differences-from-a-real-chompi)

---

## 1. What it is

- **A wavetable synth.** Seven tables of 33 frames each. Every note reads one
  frame of the current table and morphs smoothly when you change frame or table.
  Each voice has its own envelope and filter.
- **Two LFOs**, one wobbling pitch and one sweeping the filter, each with its own
  depth, rate and on/off switch.
- **A step sequencer**, up to 32 steps, recorded by playing.
- **Effects:** delay or reverb on one knob, an output compressor that turns into
  saturation, and pan.
- **14 preset slots** plus a defaults slot.

## 2. Starting and stopping

Open Schwung's Tools menu (**Shift + Volume + Step 13**) and choose **Munchi
Wave**. Move stops and Munchi Wave takes over the whole device. The first launch
copies the factory wavetables and presets into place. For the first second and a
half the pads are ignored, as on the hardware.

To leave, press **Back**, then **Back** again within three seconds. Move
restarts. The sequence is lost on exit; presets and settings are kept.

## 3. A first session

1. **Play.** Press the pads. You start on the **defaults** patch: table 1,
   frame 1.
2. **Pick a preset.** Turn the **jog wheel**, or press **step buttons 1–14**.
   Step 15 is back to the defaults.
3. **Change the wave.** Turn **knob 6 (Table)** to go through the seven tables.
   Press **Right**, then turn **knob 1 (Frame)** to sweep through a table's 33
   frames.
4. **Shape it.** Press **Left** to get back. **Knob 2** is attack, **knob 3**
   release, **knob 5** the filter (left: low-pass, right: high-pass), **knob 4**
   delay (left) or reverb (right).
5. **Make it move.** Press **Right**: **knob 2** is now the pitch LFO's depth,
   **knob 3** the filter LFO's depth.
6. **Record a sequence.** Press **Loop** (it lights red), play a few pads one at
   a time, press **Loop** again. Press **Play**. Change tempo with **knob 7**.
7. **Keep it.** Hold **Shift**, tap **A#4** (top row, third black pad), tap a
   white pad to choose a slot, press **Shift** again. The patch is saved.

## 4. The pads

The pads are the CHOMPI's two-octave keyboard, laid out as piano rows:

```
row 4   .  C#4 D#4  .  F#4 G#4 A#4  .
row 3  C4  D4  E4  F4  G4  A4  B4  C5
row 2   .  C#3 D#3  .  F#3 G#3 A#3  .
row 1  C3  D3  E3  F3  G3  A3  B3  C4
```

White keys are lit grey, black keys darker, a playing key white. The two **C4**
pads are the same key.

WAVE's keyboard sounds **an octave below** its key names (the pad marked C4
plays C3), as on the hardware. **Up / Down** move it an octave either way (±1).

Pads play at full velocity, as the CHOMPI's keys did; turn on **Pad Velocity**
in [Settings](#10-settings) to play them dynamically.

## 5. The sound

| Knob | Does | With Shift |
|---|---|---|
| **1 Pitch** | Fine tune, ±1 octave | Tune in semitone steps |
| **Right**, **1 Frame** | Which of the table's 33 frames plays | Choose the table |
| **2 Attack** | Fade-in, up to 5 s | Attack in big steps |
| **Right**, **2 Pitch LFO** | Pitch LFO depth (up to ±2 semitones) | Pitch LFO rate |
| **3 Release** | Fade-out after the key, up to 1 s | Release in big steps |
| **Right**, **3 Filter LFO** | Filter LFO depth | Filter LFO rate |
| **4 Space** | Middle is dry; left adds delay, right adds reverb | Delay time (also reverb size) |
| **5 Filter** | Middle is open; left low-pass, right high-pass | Resonance |
| **6 Table** | Choose the wavetable (1–7) | — |
| **8 Pan** | Left / right | Output compressor |
| **Volume** | Volume | Output compressor |

**Left / Right** switch knobs 1–3 between their two pages; the lit arrow is the
way to the other one.

**LFO switches:** **Track 2** turns the pitch LFO on or off, **Track 3** the
filter LFO (also Shift + C#4 / D#4). Both run all the time; their depth knobs
set how much you hear.

**Output compressor** (Shift + knob 8 or Shift + Volume): the first half
compresses, the second half saturates harder and harder.

**Resets:** **Delete + touch** a knob puts it back to its default: knob 1 (pitch
or frame), 2 (attack, or pitch LFO depth and rate), 3 (release, or filter LFO
depth and rate), 4 or 5 (all the effects), 7 (tempo and step length), 8 or
Volume (pan or volume, and the compressor).

## 6. The sequencer

The sequencer records notes one after another, one step per note, and plays
them back in a loop.

| Do | Result |
|---|---|
| **Loop** | Start recording (red). Each key you play becomes a step when you let it go |
| **Sample** while recording | Add a rest |
| **Loop** again | Stop recording |
| **Play** | Play / stop the sequence |
| **Sample** while not recording | Mute the sequence while held |
| **Loop**, held 1.25 s | Delete the last step |
| **Loop + Play**, held 1.25 s | Clear the whole sequence (also **Delete + Loop**) |

Up to 32 steps. Recording adds to the end of the sequence, also while it plays.
**Rec** does the same as Loop.

**Tempo:** **knob 7**, 80–240 BPM; **Track 1** is tap tempo (tap twice) and
flashes the beat. **Shift + knob 7** sets the step length: 1/4, dotted 1/8, 1/8
(the default), 1/8 triplet or 1/16. It changes on the next step.

**Gate:** how long each step's note sounds, as a share of the step. Hold
**Shift** and press **F#3** (10%), **G#3** (50%, the default) or **A#3** (100%).

The sequence plays at the keyboard's current octave, so **Up / Down** transpose
it.

## 7. Presets

A preset holds the whole sound: pitch, table, frame, attack, release, both LFOs
(depth, rate and on/off), filter, resonance, space and delay time. Volume, pan,
tempo and the compressor are not part of it.

- **Choose:** step buttons 1–14, the jog wheel, or Shift + a white key. Step 15
  (or the top C) is the defaults. The current preset's step is lit white; slots
  that hold a preset are purple.
- **Save:** hold **Shift**, tap **A#4** (or Shift + Capture), tap the white key of
  the slot (it turns blue), press **Shift** to confirm.
- **Copy:** **Shift + G#4** (or Shift + Copy), tap the source (green), tap the
  destination (blue), **Shift** to confirm.
- **Erase:** **Shift + F#4** (or Shift + Delete), tap the slot (red), **Shift** to
  confirm.

To cancel, press the same function pad again. Presets are saved to the card's
`presets.json`, in WAVE's own format.

## 8. The Shift layer

Holding **Shift** turns the pads into the CHOMPI's menu. The black keys:

```
row 4   .  P.LFO F.LFO  .  ERASE COPY SAVE  .
row 2   .  OCT-  OCT+   .  10%   50%  100%  .
```

| Pad | Does |
|---|---|
| **C#3 / D#3** | Octave down / up |
| **F#3 / G#3 / A#3** | Sequencer gate 10% / 50% / 100% |
| **C#4 / D#4** | Pitch LFO / filter LFO on or off |
| **F#4 / G#4 / A#4** | Erase / copy / save a preset |

The **white keys** are the preset slots: C3 is slot 1 up to B4, slot 14; C5 is
the defaults. The knobs do their Shift jobs ([section 5](#5-the-sound)).

## 9. Wavetables: the card

Munchi Wave keeps its files where a CHOMPI did, on a "card", here a folder:

```
/data/UserData/UserLibrary/Samples/Schwung/Munchi Wave/
```

It holds `wavetable01.wav` to `wavetable07.wav`, `presets.json` and
`options.json`. Munchi Wave loads the **first seven `.wav` files in alphabetical
order**, so to use your own wavetables, copy them in with the Schwung web
manager's file browser (`http://move.local:7700`) and name them so they sort
where you want them. They load at launch.

The format is the common Serum layout: 33 frames of 2048 samples, mono, 32-bit
float (16- and 24-bit also work). Presets remember a table by its **position**,
not its name, so renaming or reordering files changes what presets play.

To get the factory files back, delete the hidden file `.munchi-card` from the
folder and relaunch; missing factory files are restored, existing ones are left
alone. The folder can go straight onto a CHOMPI's SD card running WAVE.

## 10. Settings

Press **Menu** (or click the jog wheel). Jog to move, click to change, Shift +
click to step backwards, **Back** or **Menu** to close. Saved to `options.json`.

| Setting | Does |
|---|---|
| **MIDI In Ch / MIDI Out Ch** | USB-A MIDI channels |
| **Clock Out** | Send MIDI clock and start/stop while the sequencer plays |
| **CC In / CC Out** | Accept / send the knob CCs |
| **Pad Velocity** | Pads play at their velocity |
| **Exit** | Leave Munchi Wave |

## 11. MIDI

On Move's **USB-A** port, on **MIDI In Ch**:

- **Notes 36–84** play (with velocity).
- **CC 20–25** set the six CHOMPI encoders on their current pages (not tempo);
  **CC 14** is the Sample button's job (rest/mute), **CC 15** is Loop.

On **MIDI Out Ch**, Munchi Wave sends its key presses and sequencer notes, knob
moves as CCs, and (with Clock Out) MIDI clock with start/stop while the sequence
plays.

## 12. The screen and the lights

The screen shows the preset and the table/frame, the current frame's waveform,
the last control you touched (or all eight knobs), and the sequencer: its state,
the playing step and length, the tempo and the step length.

| Light | Means |
|---|---|
| Pads | Keyboard in grey, a playing key white |
| Steps 1–15 | Presets: the current one white, saved ones purple, the defaults pink |
| Play | Green/teal while the sequence plays (alternating on the step), grey when stopped with a sequence |
| Loop | Red while recording |
| Sample | Red while muting |
| Track 1 | Flashes the beat |
| Track 2 / 3 | Pitch LFO / filter LFO on |
| Left / Right | Which way the other knob page is |
| Shift | The Shift layer is open |

## 13. Control reference

| Control | Alone | With Shift |
|---|---|---|
| Pads | play | menu functions / preset slots |
| Knob 1 | Pitch (Right: Frame) | Pitch in semitones (Right: Table) |
| Knob 2 | Attack (Right: Pitch LFO depth) | Attack, coarse (Right: Pitch LFO rate) |
| Knob 3 | Release (Right: Filter LFO depth) | Release, coarse (Right: Filter LFO rate) |
| Knob 4 | Space | Delay time |
| Knob 5 | Filter | Resonance |
| Knob 6 | Table | Table |
| Knob 7 | Tempo | Step length |
| Knob 8 | Pan | Compressor |
| Volume | Volume | Compressor |
| Left / Right | knob 1–3 page | — |
| Up / Down | octave | — |
| Jog / steps 1–15 | presets | — |
| Jog click / Menu | settings | — |
| Play | sequencer play / stop | — |
| Loop / Rec | sequencer record | — |
| Sample | rest (recording) / mute (held) | — |
| Delete + Loop | clear the sequence | — |
| Delete + touch knob | reset it | — |
| Delete / Copy / Capture | — | erase / copy / save |
| Track 1 / 2 / 3 | tap tempo / pitch LFO / filter LFO | — |
| Back ×2 | exit | — |

## 14. Differences from a real CHOMPI

- **Controls.** Move has more knobs and no encoder clicks, so pages are on
  Left/Right, clicks are Delete + touch, tap tempo is Track 1, and the wavetable
  (a Shift job on the CHOMPI) also has its own knob. Shift is the CHOMPI key in
  play mode; Sample is the CHOMPI key in record mode.
- **Level.** WAVE's engine is very quiet by design (the CHOMPI's analog output
  made up the level), so Munchi Wave adds fixed gain with a soft limiter.
- **Copying to Loop/Play.** WAVE inherited TAPE's "copy to the looper" target but
  has no looper, so a copy there just loaded the defaults; Play and Loop keep
  their sequencer jobs instead.
- **No screen reader** while Munchi Wave owns the device.
