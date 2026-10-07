#!/usr/bin/env python3
"""assets/logo.png (800x480) -> src/logo.rle: RGB565 RLE kirjed (u16 kordus, u16 piksel), little-endian.
Käivita pärast logo muutmist: ~/.espvenv/bin/python tools/make_logo.py"""
import os, struct
from PIL import Image
here = os.path.dirname(os.path.abspath(__file__))
img = Image.open(os.path.join(here, '..', 'assets', 'logo.png')).convert('RGB').resize((800, 480))
px = [((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3) for r, g, b in img.getdata()]
out = bytearray()
i = 0
while i < len(px):
    v = px[i]; n = 1
    while i + n < len(px) and px[i + n] == v and n < 65535: n += 1
    out += struct.pack('<HH', n, v); i += n
open(os.path.join(here, '..', 'src', 'logo.rle'), 'wb').write(out)
print('logo.rle %d baiti (%d kirjet)' % (len(out), len(out) // 4))
