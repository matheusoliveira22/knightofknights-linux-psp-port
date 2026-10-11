"""Capture an X11 window's client area to PNG: xwdshot.py <window-name-substring> <out.png>"""
import struct, subprocess, sys, re
from PIL import Image
tree = subprocess.run(["xwininfo", "-root", "-tree"], capture_output=True, text=True).stdout
wid = [l for l in tree.splitlines() if sys.argv[1] in l and re.search(r"\d+x\d+", l)][0].split()[0]
info = subprocess.run(["xwininfo", "-id", wid], capture_output=True, text=True).stdout
g = lambda k: int(re.search(k + r":\s+(-?\d+)", info).group(1))
x, y, w, h = g("Absolute upper-left X"), g("Absolute upper-left Y"), g("Width"), g("Height")
d = subprocess.run(["xwd", "-silent", "-root"], capture_output=True).stdout
f = struct.unpack(">25I", d[:100])
hs, W, H, bpl, ncolors = f[0], f[4], f[5], f[12], f[19]
off = hs + ncolors * 12
img = Image.frombuffer("RGBX", (W, H), d[off:off + bpl * H], "raw", "BGRX", bpl, 1).convert("RGB")
img.crop((x, y, x + w, y + h)).save(sys.argv[2])
print(x, y, w, h)
