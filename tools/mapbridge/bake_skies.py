#!/usr/bin/env python3
"""Convert every skybox (gfx/env/<name>{ft,bk,lf,rt,up,dn}.tga) to PNG for Maya's sky preview. System python (PIL)."""
import glob, os
from PIL import Image
OUT = os.path.expanduser("~/.cache/gsbridge/tex"); os.makedirs(OUT, exist_ok=True); n = 0
for c in ("/home/tim/Desktop/dod-gc/content/dod", "/home/tim/Desktop/dod-gc/content/valve"):
    for f in glob.glob(c + "/gfx/env/*.tga"):
        b = os.path.basename(f)[:-4]; dst = os.path.join(OUT, "sky_%s.png" % b)
        if not os.path.exists(dst): Image.open(f).convert("RGB").save(dst); n += 1
print("skies converted:", n)
