#!/bin/sh
# Lay out a memory-stick-ready install: dist/psp/PSP/GAME/KnightOfKnights/{EBOOT.PBP, data...}.
# Copy dist/psp/PSP onto the memory stick root (or PPSSPP's memstick directory).
# usage: package_psp.sh [game-dir]   (default: ../game/Knight050; data is prepared via make_data.sh)
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
GAME=${1:-$ROOT/../game/Knight050}
[ -d "$ROOT/build-data" ] || "$ROOT/tools/make_data.sh" "$GAME" "$ROOT/build-data"
export PSPDEV=${PSPDEV:-$HOME/pspdev}
export PATH=$PSPDEV/bin:$PATH
[ -d "$ROOT/build-psp" ] || psp-cmake -S "$ROOT" -B "$ROOT/build-psp" -DCMAKE_BUILD_TYPE=Release
cmake --build "$ROOT/build-psp" -j"$(nproc)"
OUT=$ROOT/dist/psp/PSP/GAME/KnightOfKnights
rm -rf "$ROOT/dist/psp"
mkdir -p "$OUT"
cp "$ROOT/build-psp/EBOOT.PBP" "$OUT/"
cp -r "$ROOT/build-data/." "$OUT/"
du -sh "$OUT"
