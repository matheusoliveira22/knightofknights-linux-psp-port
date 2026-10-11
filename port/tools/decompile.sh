#!/bin/sh
# Rebuild re/decomp/ (the Ghidra decompile the game code is generated from) from your own copy of
# the game executable. Afterwards run tools/postgen.sh to regenerate src/game/gen.
#
# usage: tools/decompile.sh "<game dir>/Knight of Knights.exe" [ghidra install dir]
# needs Ghidra 12.x (support/analyzeHeadless, JDK 21), python3 and binutils (objdump).
# The Ghidra directory can also be given in GHIDRA_HOME.
set -e
[ -n "$1" ] || { echo "usage: $0 <Knight of Knights.exe> [ghidra dir]"; exit 1; }
EXE=$(readlink -f "$1")
GHIDRA=${2:-$GHIDRA_HOME}
[ -n "$GHIDRA" ] || { echo "pass the Ghidra install directory or set GHIDRA_HOME"; exit 1; }
H=$GHIDRA/support/analyzeHeadless
[ -f "$EXE" ] || { echo "no such file: $1"; exit 1; }
[ -x "$H" ] || { echo "not a Ghidra install (no support/analyzeHeadless): $GHIDRA"; exit 1; }
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${KOK_DECOMP_OUT:-$ROOT/re/decomp}
SC=$ROOT/re/scripts
MD5=$(md5sum "$EXE" | cut -d' ' -f1)
[ "$MD5" = 9f24022f66b8fa6ae4a82057bd1d3b33 ] ||
    echo "warning: unexpected executable (md5 $MD5); the port is made for Knight of Knights 0.50"
PROJ=$(mktemp -d)
trap 'rm -rf "$PROJ"' EXIT
mkdir -p "$OUT"
run() { "$H" "$PROJ" knight "$@" -scriptPath "$SC" >> "$OUT/ghidra.log" 2>&1; }
: > "$OUT/ghidra.log"

echo "[1/5] import and auto analysis (a few minutes)"
run -import "$EXE"
echo "[2/5] extra entry points (functions only reached through tables)"
run -process -noanalysis -postScript CreateFromTables.java "$SC/extra_entries.txt"
echo "[3/5] fix CRT x87 helpers and stack-argument counts, decompile"
run -process -noanalysis -postScript FixX87.java -postScript FixParams.java 4282b0:6 428ba0:4 \
    -postScript DecompileAll.java "$OUT" -postScript DecompileMarked.java "$OUT" \
    -postScript GlobalTypes.java "$OUT/global_types.tsv"
echo "[4/5] data types of the globals the game code references"
python3 - "$OUT/all_marked.c" > "$PROJ/dat.txt" <<'PY'
import re, sys
src = open(sys.argv[1], encoding="latin-1").read()
parts = re.split(r"^// ===== FUN_([0-9a-f]{8})", src, flags=re.M)
dat = set()
for i in range(1, len(parts), 2):
    if int(parts[i], 16) < 0x444000:          # game code (the CRT above it is not ported)
        dat |= set(re.findall(r"DAT_([0-9a-f]{8})", parts[i + 1]))
print("\n".join(sorted(dat)))
PY
run -process -noanalysis -readOnly -postScript ListingTypes.java "$PROJ/dat.txt" "$OUT/listing_types.tsv"
echo "[5/5] disassembly listing (tools/dis.sh)"
LC_ALL=C objdump -d -M intel "$EXE" > "$OUT/text.asm"
echo "done: $OUT (now run tools/postgen.sh)"
