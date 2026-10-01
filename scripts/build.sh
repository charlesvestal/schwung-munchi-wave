#!/usr/bin/env bash
# Build Munchi Wave for Move (aarch64). Uses Docker unless CROSS_PREFIX is set.
#
#   ./scripts/build.sh        -> dist/munchi-wave/ and dist/munchi-wave-module.tar.gz
#
# The factory card is fetched on the HOST side (it needs git and the
# network), before the container compiles; see fetch-card.sh.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
IMAGE_NAME="munchi-builder"
cd "$REPO_ROOT"

if [ -z "${CROSS_PREFIX:-}" ] && [ ! -f "/.dockerenv" ]; then
    ./scripts/fetch-card.sh build/card
    if ! docker image inspect "$IMAGE_NAME" &>/dev/null; then
        docker build -t "$IMAGE_NAME" -f scripts/Dockerfile .
    fi
    docker run --rm -v "$REPO_ROOT:/build" -u "$(id -u):$(id -g)" -w /build \
        -e MUNCHI_CARD_FETCHED=1 "$IMAGE_NAME" ./scripts/build.sh
    exit 0
fi

CROSS_PREFIX="${CROSS_PREFIX:-aarch64-linux-gnu-}"

if [ -z "${MUNCHI_CARD_FETCHED:-}" ]; then
    ./scripts/fetch-card.sh build/card
fi
if [ ! -f build/card/wavetable01.wav ]; then
    echo "build: factory card missing (build/card); run scripts/fetch-card.sh" >&2
    exit 1
fi

MODULE_DIR="dist/munchi-wave"
TARBALL="dist/munchi-wave-module.tar.gz"
mkdir -p build dist
rm -rf "$MODULE_DIR"
mkdir -p "$MODULE_DIR"

SRCS="src/standalone/main.cpp src/standalone/surface.cpp src/standalone/font.cpp \
      src/engine/munchi_wave.cpp src/engine/wave_card.cpp \
      src/engine/daisysp/adsr.cpp src/engine/daisysp/svf.cpp src/engine/daisysp/dcblock.cpp \
      src/engine/daisysp/oscillator.cpp"

echo "Compiling standalone..."
${CROSS_PREFIX}g++ -O2 -g -std=c++17 -Wall -Wno-vla -Wno-unused-variable \
    -Wno-unused-but-set-variable -Wno-class-memaccess \
    -Isrc -Isrc/engine $SRCS \
    -o build/standalone \
    -static-libstdc++ -static-libgcc -lpthread -lm

echo "Packaging..."
cat src/module.json > "$MODULE_DIR/module.json"
cat build/standalone > "$MODULE_DIR/standalone"
chmod +x "$MODULE_DIR/standalone"
cat scripts/boot-entry.sh > "$MODULE_DIR/boot-entry.sh"
chmod +x "$MODULE_DIR/boot-entry.sh"
cat src/help.json > "$MODULE_DIR/help.json"
cat README.md > "$MODULE_DIR/README.md"
cat docs/MANUAL.md > "$MODULE_DIR/MANUAL.md"
cat LICENSE > "$MODULE_DIR/LICENSE"
cat THIRD_PARTY.md > "$MODULE_DIR/THIRD_PARTY.md"
mkdir -p "$MODULE_DIR/card"
for f in build/card/*; do
    cat "$f" > "$MODULE_DIR/card/$(basename "$f")"
done

rm -f "$TARBALL"
( cd dist && tar -czf "$(basename "$TARBALL")" munchi-wave )

echo "Output: $MODULE_DIR/  Tarball: $TARBALL ($(du -h "$TARBALL" | cut -f1))"

# ---- the sound generator: the same engine as a Signal Chain synth ----------
SYNTH_DIR="dist/munchi-wave-synth"
SYNTH_TARBALL="dist/munchi-wave-synth-module.tar.gz"
rm -rf "$SYNTH_DIR"
mkdir -p "$SYNTH_DIR/card"

echo "Compiling sound generator..."
# -fno-gnu-unique + --exclude-libs,ALL: no STB_GNU_UNIQUE symbols, so a new
# dsp.so loads without restarting Move; --no-undefined: a missing symbol is a
# build failure, not a module stuck on "Loading..."
${CROSS_PREFIX}g++ -O2 -g -std=c++17 -shared -fPIC -fvisibility=hidden -fno-gnu-unique \
    -Wall -Wno-vla -Wno-unused-variable -Wno-unused-but-set-variable -Wno-class-memaccess \
    -Isrc/synth -Isrc/engine \
    src/synth/wave_synth.cpp src/engine/wave_card.cpp \
    src/engine/daisysp/adsr.cpp src/engine/daisysp/svf.cpp src/engine/daisysp/dcblock.cpp \
    src/engine/daisysp/oscillator.cpp \
    -o build/dsp.so \
    -static-libstdc++ -static-libgcc -Wl,--exclude-libs,ALL -Wl,--no-undefined -lpthread -lm

cat src/synth/module.json > "$SYNTH_DIR/module.json"
cat build/dsp.so > "$SYNTH_DIR/dsp.so"
cat src/synth/help.json > "$SYNTH_DIR/help.json"
cat LICENSE > "$SYNTH_DIR/LICENSE"
cat THIRD_PARTY.md > "$SYNTH_DIR/THIRD_PARTY.md"
for f in build/card/*; do
    cat "$f" > "$SYNTH_DIR/card/$(basename "$f")"
done
rm -f "$SYNTH_TARBALL"
( cd dist && tar -czf "$(basename "$SYNTH_TARBALL")" munchi-wave-synth )
echo "Output: $SYNTH_DIR/  Tarball: $SYNTH_TARBALL ($(du -h "$SYNTH_TARBALL" | cut -f1))"
