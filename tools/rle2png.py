#!/usr/bin/env python3
"""RLE (u16 kordus, u16 RGB565) -> PNG: tools/rle2png.py kaust"""
import glob, os, struct, sys
from PIL import Image
for f in glob.glob(os.path.join(sys.argv[1], '*.rle')):
    rle = open(f, 'rb').read(); px = bytearray()
    for k in range(0, len(rle) - 3, 4):
        cnt, v = struct.unpack_from('<HH', rle, k)
        px += bytes((((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)) * cnt
    Image.frombytes('RGB', (800, 480), bytes(px[:800 * 480 * 3])).save(f[:-4] + '.png')
    print(os.path.basename(f[:-4]) + '.png')
