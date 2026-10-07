#!/usr/bin/env python3
"""Ekraanipilt üle WiFi: tools/shot.py out.png [IP]"""
import struct, sys, urllib.request
from PIL import Image
ip = sys.argv[2] if len(sys.argv) > 2 else open('/tmp/shock_ip').read().strip()
rle = urllib.request.urlopen(f'http://{ip}/dev/shot', timeout=30).read()
px = bytearray()
for k in range(0, len(rle) - 3, 4):
    cnt, v = struct.unpack_from('<HH', rle, k)
    px += bytes((((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)) * cnt
Image.frombytes('RGB', (800, 480), bytes(px[:800 * 480 * 3])).save(sys.argv[1])
print('salvestatud', sys.argv[1])
