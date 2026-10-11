"""PSP XMB images from the game's own title screen (pic/tbg01), so no game artwork is kept in the
repository: ICON0.PNG (144x80, the logo) and PIC1.PNG (480x272 background).
usage: python3 tools/make_icons.py <original game dir or data dir> <output dir>"""
import os, sys
from PIL import Image

src, out = sys.argv[1], sys.argv[2]
title = Image.open(os.path.join(src, "pic", "tbg01")).convert("RGB")   # 640x480 title screen
os.makedirs(out, exist_ok=True)
title.crop((60, 86, 580, 375)).resize((144, 80), Image.LANCZOS).save(os.path.join(out, "ICON0.PNG"))
pic = Image.new("RGB", (480, 272))
pic.paste(title.resize((363, 272), Image.LANCZOS), ((480 - 363) // 2, 0))
pic.save(os.path.join(out, "PIC1.PNG"))
