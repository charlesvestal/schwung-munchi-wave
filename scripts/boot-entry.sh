#!/bin/sh
# Boot-target entry for Munchi Wave (a standalone tool). Verbatim from
# Schwung docs/BOOT_TARGETS.md, "a standalone tool as a boot target".
# Ships beside the `standalone`
# binary in the module payload; module.json points `exec` at it.
#
# It deliberately does NOT `exec` the binary, for the two reasons below.

set -u

dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BIN="$dir/standalone"
# Both paths are overridable so the script can be exercised off the device
# (Schwung does the same with SCHWUNG_PROC_DIR in schwung-entry.sh). Nothing
# on the Move sets either variable.
SCHWUNG_ENTRY="${SCHWUNG_ENTRY:-/data/UserData/schwung/schwung-entry.sh}"
MOVE_ORIGINAL="${MOVE_ORIGINAL:-/opt/move/MoveOriginal}"

# Never strand the device: anything wrong here still boots stock Move.
[ -x "$BIN" ] || exec "$MOVE_ORIGINAL"

"$BIN" &
child=$!

# Reason 1 to keep a shell in front of the binary: `/etc/init.d/move stop`
# TERMs the service pid, which through the selector's exec chain is this
# script. Forward it, then exit WITHOUT the handover below — a stop must not
# start Schwung. A target that stays alive here keeps /dev/ablspi0.0 open and
# the next start finds the device busy (black screen, only kill -9 clears it).
terminate() {
    kill -TERM "$child" 2>/dev/null
    i=0
    while [ "$i" -lt 10 ] && kill -0 "$child" 2>/dev/null; do
        sleep 1
        i=$((i + 1))
    done
    kill -KILL "$child" 2>/dev/null
    exit 0
}
trap terminate TERM INT

wait "$child"

# Reason 2: the tool's own quit gesture exits the binary. Run as a Schwung
# tool, launch-standalone.sh restarts Move at this point; run as a boot target
# nothing else would, and quitting would leave a dead device.
if [ -x "$SCHWUNG_ENTRY" ]; then
    exec "$SCHWUNG_ENTRY"
fi
exec "$MOVE_ORIGINAL"
