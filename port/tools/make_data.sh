#!/bin/sh
# Prepare a data directory for the port from an original "Knight of Knights" installation.
#   tools/make_data.sh <original game dir> <output dir>
# Copies the game data (exe included: the port reads its data tables from it), renders the MIDI
# music to OGG with FluidSynth (cut at the MIDI's end so loops restart at once), bakes the font and
# makes the PSP icons (psp/ICON0.PNG, psp/PIC1.PNG).
set -eu
SRC=$1
OUT=$2
SF2=${SF2:-/usr/share/sounds/sf2/FluidR3_GM.sf2}
TOOLS=$(cd "$(dirname "$0")" && pwd)

mkdir -p "$OUT/music"
for d in dat obj pic tex wave save; do
    rm -rf "$OUT/$d"
    cp -r "$SRC/$d" "$OUT/$d"
done
cp "$SRC/Knight of Knights.exe" "$OUT/kok.exe"

for mid in "$SRC"/midi/*.mid; do
    name=$(basename "$mid" .mid)
    ogg="$OUT/music/$name.ogg"
    [ -f "$ogg" ] && [ "$ogg" -nt "$mid" ] && [ "$ogg" -nt "$TOOLS/midi_trim.py" ] && continue
    tmp=$(mktemp --suffix=.wav)
    fluidsynth -ni -q -g 0.6 -r 44100 -F "$tmp" "$SF2" "$mid" >/dev/null
    python3 "$TOOLS/midi_trim.py" "$mid" "$tmp"   # loop point = end of the MIDI, like DirectMusic
    oggenc -Q -q 3 -o "$ogg" "$tmp"
    rm -f "$tmp"
    echo "music: $name"
done

python3 "$TOOLS/bakefont.py" "$SRC" "$OUT/kokfont.bin"
# PSP menu icon and background from the game's title screen (used by the PSP build)
python3 "$TOOLS/make_icons.py" "$SRC" "$TOOLS/../psp"
