#!/bin/bash
# Copy a local build to the Move. Schwung's Tools menu picks it up.
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$(dirname "$SCRIPT_DIR")"
HOST="${MOVE_HOST:-ableton@move.local}"
DEST=/data/UserData/schwung/modules/tools/munchi-wave

[ -d dist/munchi-wave ] || { echo "Run ./scripts/build.sh first."; exit 1; }
ssh "$HOST" "mkdir -p $DEST"
# the card is large and rarely changes: copy it only when missing
if ssh "$HOST" "[ -f $DEST/card/wavetable01.wav ]"; then
    scp dist/munchi-wave/module.json dist/munchi-wave/help.json \
        dist/munchi-wave/README.md dist/munchi-wave/LICENSE dist/munchi-wave/THIRD_PARTY.md dist/munchi-wave/MANUAL.md "$HOST:$DEST/"
else
    scp -r dist/munchi-wave/card dist/munchi-wave/module.json dist/munchi-wave/help.json \
        dist/munchi-wave/README.md dist/munchi-wave/LICENSE dist/munchi-wave/THIRD_PARTY.md dist/munchi-wave/MANUAL.md "$HOST:$DEST/"
fi
# upload beside, then rename: a running Munchi Wave holds the old binary open
# ("text file busy" on a plain copy) and keeps it until it exits
scp dist/munchi-wave/standalone "$HOST:$DEST/standalone.new"
scp scripts/boot-entry.sh "$HOST:$DEST/boot-entry.sh"
ssh "$HOST" "chmod +x $DEST/standalone.new $DEST/boot-entry.sh && mv -f $DEST/standalone.new $DEST/standalone"
echo "Installed to $DEST. Launch it from Schwung's Tools menu."

# the sound generator: Signal Chain loads it by path, so it lives under
# sound_generators/ (it picks up a new dsp.so the next time a slot loads it)
SDEST=/data/UserData/schwung/modules/sound_generators/munchi-wave-synth
if [ -d dist/munchi-wave-synth ]; then
    ssh "$HOST" "mkdir -p $SDEST"
    scp -r dist/munchi-wave-synth/card dist/munchi-wave-synth/module.json dist/munchi-wave-synth/help.json \
        dist/munchi-wave-synth/LICENSE dist/munchi-wave-synth/THIRD_PARTY.md "$HOST:$SDEST/"
    scp dist/munchi-wave-synth/dsp.so "$HOST:$SDEST/dsp.so.new"
    ssh "$HOST" "mv -f $SDEST/dsp.so.new $SDEST/dsp.so"
    echo "Installed the sound generator to $SDEST. Pick Munchi Wave as a slot's synth."
fi
