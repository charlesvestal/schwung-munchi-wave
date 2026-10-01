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
