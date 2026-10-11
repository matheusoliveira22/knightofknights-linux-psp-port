#!/bin/sh
# Run the PSP build in PPSSPP with scripted input and collect screenshots into a contact sheet.
# usage: psprun.sh out.png "f1,f2,..." ["frame:+VK,frame:-VK,..."] [timeout_s]
set -e
OUT=$1; SHOTS=$2; KEYS=$3; TMO=${4:-180}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
G=$HOME/.var/app/org.ppsspp.PPSSPP/config/ppsspp/PSP/GAME/KnightOfKnights
flatpak kill org.ppsspp.PPSSPP 2>/dev/null || true
sleep 1
cp "$ROOT/build-psp/EBOOT.PBP" "$G/"
rm -f "$G"/shot_*.ppm "$G/log.txt"
{ echo "shot $SHOTS"; echo perf; [ -n "$KEYS" ] && echo "keys $KEYS"; echo exit; [ -n "$NOAUDIO" ] && echo noaudio; for e in $PSPENV; do echo "env $e"; done; } > "$G/test.txt"
LAST=${SHOTS##*,}
# software rendering: the hardware renderer keeps framebuffers on the host GPU, so the game's own
# VRAM screenshots would be stale there (set PSPHW=1 to watch the hardware renderer instead)
INI=$HOME/.var/app/org.ppsspp.PPSSPP/config/ppsspp/PSP/SYSTEM/ppsspp.ini
cp "$INI" "$INI.psprun"
[ -z "$PSPHW" ] && sed -i 's/^SoftwareRenderer = False/SoftwareRenderer = True/' "$INI"
flatpak run org.ppsspp.PPSSPP "$G/EBOOT.PBP" > "$(dirname "$OUT")/ppsspp.log" 2>&1 &
i=0
while [ ! -f "$G/shot_$LAST.ppm" ] && [ $i -lt "$TMO" ]; do
    sleep 1; i=$((i + 1))
    grep -q "Bad memory" "$(dirname "$OUT")/ppsspp.log" && break
done
sleep 1
flatpak kill org.ppsspp.PPSSPP 2>/dev/null || true
rm -f "$G/test.txt"
mv "$INI.psprun" "$INI"
grep -a "Bad memory" -A3 "$(dirname "$OUT")/ppsspp.log" || true
cat "$G/log.txt" 2>/dev/null | sort | uniq -c | sort -rn | head -15
python3 - "$OUT" "$G" "$SHOTS" <<'PY'
import sys, os
from PIL import Image
out, g, shots = sys.argv[1], sys.argv[2], sys.argv[3].split(',')
ims = [Image.open(f"{g}/shot_{f}.ppm") for f in shots if os.path.exists(f"{g}/shot_{f}.ppm")]
print("shots:", len(ims), "of", len(shots))
if ims:
    cols = min(3, len(ims)); rows = (len(ims) + cols - 1) // cols
    sheet = Image.new("RGB", (480 * cols, 272 * rows))
    for i, im in enumerate(ims): sheet.paste(im, (480 * (i % cols), 272 * (i // cols)))
    sheet.save(out)
PY
