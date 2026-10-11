"""Bake the monospace Shift-JIS bitmap font used for the game's GDI text ("MS Gothic" 16/32 px).

usage: python3 -I bakefont.py <game dir> <out kokfont.bin> [jp.ttf] [ascii.ttf]

Glyph set: ASCII, half-width katakana, kana, full-width ASCII/punctuation and every character
appearing in the exe's strings or the save files. File format (little endian):
  "KFNT" u32 nsizes; per size: u32 height, u32 nglyphs, then nglyphs x {u16 sjis, u8 width},
  then nglyphs bitmaps of width*height bytes (8-bit coverage).
"""
import os, re, struct, sys
from PIL import Image, ImageDraw, ImageFont

game, out = sys.argv[1], sys.argv[2]
jp_ttf = sys.argv[3] if len(sys.argv) > 3 else "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf"
asc_ttf = sys.argv[4] if len(sys.argv) > 4 else "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"

chars = set(chr(c) for c in range(0x20, 0x7F))
chars.update(bytes([b]).decode("cp932") for b in range(0xA1, 0xE0))
for lo, hi in ((0x3000, 0x3040), (0x3041, 0x3097), (0x30A0, 0x3100), (0xFF01, 0xFF5F), (0x2460, 0x2474),
               (0x2190, 0x2194), (0x25A0, 0x25D0), (0x2600, 0x2606)):
    for c in range(lo, hi):
        try:
            chr(c).encode("cp932"); chars.add(chr(c))
        except UnicodeEncodeError:
            pass

pat = re.compile(rb"(?:[\x20-\x7e]|[\x81-\x9f\xe0-\xfc][\x40-\x7e\x80-\xfc]|[\xa1-\xdf])+")
def harvest(data):
    for m in pat.finditer(data):
        try:
            chars.update(m.group().decode("cp932"))
        except UnicodeDecodeError:
            pass

def pe_data_sections(d):
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    o = pe + 24 + struct.unpack_from("<H", d, pe + 20)[0]
    for i in range(struct.unpack_from("<H", d, pe + 6)[0]):
        nm, vs, va, rs, ro = struct.unpack_from("<8sIIII", d, o + 40 * i)
        if nm.startswith((b".data", b".rdata")):
            yield d[ro:ro + rs]

cstr = re.compile(rb"(?:[\x20-\x7e]|[\x81-\x9f\xe0-\xfc][\x40-\x7e\x80-\xfc]|[\xa1-\xdf]){2,}\x00")
for root, _, files in os.walk(game):
    for f in files:
        p = os.path.join(root, f)
        if f.lower().endswith(".exe"):
            for seg in pe_data_sections(open(p, "rb").read()):
                for m in cstr.finditer(seg):
                    harvest(m.group()[:-1])
        elif f.lower().endswith((".dat", ".txt")) and "save" in p.replace("\\", "/").split("/"):
            harvest(open(p, "rb").read())

def sjis(c):
    b = c.encode("cp932")
    return b[0] if len(b) == 1 else (b[0] << 8 | b[1])

glyphs = sorted((sjis(c), c) for c in chars if c.isprintable() or c == "　")
blob = [b"KFNT", struct.pack("<I", 2)]
for h in (16, 32):
    fj = ImageFont.truetype(jp_ttf, int(h * 0.92))
    fa = ImageFont.truetype(asc_ttf, int(h * 0.86))
    table, bits = [], []
    for code, c in glyphs:
        w = h // 2 if code < 0x100 else h
        img = Image.new("L", (w, h), 0)
        dr = ImageDraw.Draw(img)
        font = fa if code < 0x80 else fj
        if c.strip() and c != "　":
            l, t, r, b = dr.textbbox((0, 0), c, font=font)
            x = (w - (r - l)) // 2 - l
            asc, desc = font.getmetrics()
            y = (h - (asc + desc)) // 2
            dr.text((x, y), c, fill=255, font=font)
        table.append(struct.pack("<HB", code, w))
        bits.append(img.tobytes())
    blob.append(struct.pack("<II", h, len(glyphs)))
    blob.extend(table)
    blob.extend(bits)
open(out, "wb").write(b"".join(blob))
print(f"{len(glyphs)} glyphs -> {out} ({sum(map(len, blob))} bytes)")
