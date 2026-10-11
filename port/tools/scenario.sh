#!/bin/sh
# tools/scenario.sh <out.png> "<frames>" "<keys>"   e.g. "300,600" "500:+Z,505:-Z"
OUT=$1; FR=$2; KEYS=$3
S=$(mktemp -d)
cd "$(dirname "$0")/../build-pc" || exit 1
KOK_SHOT=$S/s:$FR KOK_SHOT_EXIT=1 KOK_KEYS="$KEYS" timeout 120 ./kok ../build-data 2>&1 | grep -v "^$" | sort | uniq -c | sort -rn | head -15
python3 - "$S" "$OUT" <<'PY'
import sys, glob
from PIL import Image
fs = sorted(glob.glob(sys.argv[1] + "/s_*.ppm"), key=lambda x: int(x.split("_")[-1][:-4]))
ims = [Image.open(f).resize((320, 240)) for f in fs]
if not ims: sys.exit("no frames")
cols = min(4, len(ims)); rows = (len(ims) + cols - 1) // cols
out = Image.new("RGB", (320 * cols, 240 * rows))
for i, im in enumerate(ims): out.paste(im, (320 * (i % cols), 240 * (i // cols)))
out.save(sys.argv[2])
print("frames:", [f.split("_")[-1][:-4] for f in fs])
PY
rm -rf "$S"
