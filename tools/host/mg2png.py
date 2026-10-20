#!/usr/bin/env python3
"""Decode a MantisPlanet .mg image (MG01, 320x240, RGB565 little-endian) to PNG."""
import sys, struct
from PIL import Image
def load(path):
    d = open(path, 'rb').read()
    assert d[:4] == b'MG01', 'not an .mg file'
    w, h = struct.unpack('<HH', d[4:8])
    im = Image.new('RGB', (w, h)); px = im.load()
    for i in range(w * h):
        v = d[8 + 2 * i] | (d[9 + 2 * i] << 8)
        px[i % w, i // w] = (((v >> 11) & 31) << 3, ((v >> 5) & 63) << 2, (v & 31) << 3)
    return im
if __name__ == '__main__':
    load(sys.argv[1]).save(sys.argv[2])
