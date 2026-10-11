#!/bin/sh
# print disassembly between two hex addresses: tools/dis.sh 43ec30 43f250
python3 -c '
import sys, re
s, e = int(sys.argv[1], 16), int(sys.argv[2], 16)
for l in open(sys.argv[3]):
    m = re.match(r"\s+([0-9a-f]+):\t(?:[0-9a-f ]+\t)?(.*)", l)
    if m and s <= int(m.group(1), 16) < e and m.group(2).strip() and m.group(2).strip() != "nop":
        print(m.group(1) + "\t" + m.group(2).strip())
' "$1" "$2" "$(dirname "$0")/../re/decomp/text.asm"
