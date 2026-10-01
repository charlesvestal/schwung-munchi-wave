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
      src/engine/munchi_wave.cpp \
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
