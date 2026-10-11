"""Annotate COM vtable calls in Ghidra's all.c with method names.

usage: python3 -I annotate.py all.c > all_annot.c
Known globals resolve exactly; otherwise all interfaces whose method at that
offset takes the same argument count are listed as candidates.
"""
import re, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from comvt import VT, by_offset

KNOWN = {
    "DAT_004a3864": "DDraw2", "DAT_004a3878": "D3D2", "DAT_004a387c": "D3DDev2", "DAT_004a3858": "DDSurf", "DAT_004a385c": "DDSurf", "DAT_004a3860": "DDSurf",
    "DAT_004a3880": "RM2", "DAT_004a3884": "Device2", "DAT_004a3888": "Viewport",
    "DAT_004a388c": "Frame2", "DAT_004a3890": "Frame2", "DAT_004a3894": "Frame2", "DAT_004a3898": "Frame2",
    "DAT_0049b670": "DMPerf", "DAT_0049b678": "DMLoader", "DAT_0049b6a8": "DSound",
}
OFF = by_offset()
MARK = "(**(code **)"

def balanced(s, i):
    """s[i] == '('; return index just past the matching ')'."""
    depth = 0
    for j in range(i, len(s)):
        if s[j] == "(": depth += 1
        elif s[j] == ")":
            depth -= 1
            if depth == 0: return j + 1
    return len(s)

def split_off(e):
    """'(*X + 0x58)' -> ('X', 0x58); '*X' -> ('X', 0)."""
    e = e.strip()
    while e.startswith("(") and balanced(e, 0) == len(e): e = e[1:-1].strip()
    depth, cut = 0, -1
    for j, c in enumerate(e):
        if c in "([": depth += 1
        elif c in ")]": depth -= 1
        elif depth == 0 and e.startswith(" + ", j): cut = j
    off = 0
    if cut >= 0 and re.fullmatch(r"0x[0-9a-f]+|\d+", e[cut + 3:].strip()):
        off, e = int(e[cut + 3:].strip(), 0), e[:cut].strip()
    while e.startswith("(") and balanced(e, 0) == len(e): e = e[1:-1].strip()
    if not e.startswith("*"): return None, None
    obj = re.sub(r"^\((?:int|undefined4) \*\)", "", e[1:].strip())
    while obj.startswith("(") and balanced(obj, 0) == len(obj): obj = obj[1:-1].strip()
    return obj, off

def count_args(s, i):
    depth, n, empty = 1, 0, True
    while i < len(s) and depth:
        c = s[i]
        if c in "([": depth += 1
        elif c in ")]": depth -= 1
        elif c == "," and depth == 1: n += 1
        if depth and not c.isspace(): empty = False
        i += 1
    return (0 if empty else n + 1), i

def annotate(line):
    out, i = [], 0
    while True:
        i = line.find(MARK, i)
        if i < 0: break
        j = i + len(MARK)
        if j >= len(line) or line[j] != "(":
            i = j; continue
        k = balanced(line, j)
        obj, off = split_off(line[j:k])
        rest = line[k:].lstrip()
        if obj is None or not rest.startswith(")"):
            i = k; continue
        p = line.index(")", k) + 1
        while p < len(line) and line[p] == " ": p += 1
        if p >= len(line) or line[p] != "(":
            i = k; continue
        argc, _ = count_args(line, p + 1)
        argc -= 1  # `this`
        if obj in KNOWN:
            meth = VT[KNOWN[obj]]
            out.append(f"{KNOWN[obj]}::{meth[off // 4][0] if off // 4 < len(meth) else '?'}")
        elif off <= 8:
            out.append(["QueryInterface", "AddRef", "Release"][off // 4])
        else:
            c = [f"{a}::{n}" for a, n, ac in OFF.get(off, []) if ac == argc] or \
                [f"{a}::{n}?" for a, n, ac in OFF.get(off, [])]
            out.append("|".join(sorted(set(c)))[:160])
        i = k
    return line.rstrip("\n") + ("  /* " + " ; ".join(out) + " */" if out else "") + "\n"

for l in open(sys.argv[1], encoding="latin-1"):
    sys.stdout.write(annotate(l))
