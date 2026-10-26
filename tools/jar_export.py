#!/usr/bin/env python3
"""Bug-jar sprites: from the artist's layered art (the same jars on a flat magenta and on a grass background)
to src/jar_art.h.

  python3 tools/jar_export.py        (run from the repo root; needs numpy, scipy, pillow)

1. Two-background matting: where a pixel looks the same on both backgrounds it's solid; where it shifts with
   the background it's glass, and how much it shifts gives its transparency. (The grass behind each jar is
   estimated from the grass around it.) Any colour that comes out magenta is background leaking through and
   is re-solved against the glass's own pale tint.
2. BACK = the clear composite jar on its shelf (drawn behind the bugs). FRONT = the separated front layer:
   highlights, the front of the wire handle, the rim (drawn over the bugs), aligned onto the composite and
   trimmed below the lid. Its faint glass tint is halved (the composite already has the glass).
3. Both shrunk to 100 sprite pixels tall (premultiplied box filter), exported as RGB565 + alpha in flash.
"""
import numpy as np
from PIL import Image
from scipy import ndimage

MAG, GRASS = 'assets/jar/jar_on_magenta.jpeg', 'assets/jar/jar_on_grass.jpeg'
COMPOSITE_BOX = (40, 382, 568, 842)      # y0, y1, x0, x1 in the source images
FRONT_BOX = (395, 708, 850, 1080)
GLASS = np.array([212., 236., 240.])

M = np.asarray(Image.open(MAG).convert('RGB')).astype(float)
G = np.asarray(Image.open(GRASS).convert('RGB')).astype(float)
B1 = np.median(np.concatenate([M[:30].reshape(-1, 3), M[-30:].reshape(-1, 3)]), axis=0)

def matte(box, pad=14):
    y0, y1, x0, x1 = box
    m = M[y0 - pad:y1 + pad, x0 - pad:x1 + pad]; g = G[y0 - pad:y1 + pad, x0 - pad:x1 + pad]
    fg = np.linalg.norm(m - B1, axis=2) > 22
    fg = ndimage.binary_closing(fg, iterations=3); fg = ndimage.binary_fill_holes(fg); fg = ndimage.binary_dilation(fg, iterations=4)
    w = (~fg).astype(float)
    num = np.stack([ndimage.gaussian_filter(g[..., c] * w, 18) for c in range(3)], 2); den = ndimage.gaussian_filter(w, 18)[..., None]
    B2 = np.where(fg[..., None], num / np.maximum(den, 1e-3), g)
    dB = B1 - B2; dC = m - g
    a = np.clip(1.0 - (dC * dB).sum(2) / np.maximum((dB * dB).sum(2), 1.0), 0, 1); a[~fg] = 0
    F = np.clip((m - (1 - a)[..., None] * B1) / np.maximum(a, 1e-3)[..., None], 0, 255)
    bad = fg & (F[..., 0] > F[..., 1] + 35) & (F[..., 2] > F[..., 1] + 15)
    d = GLASS - B1
    a2 = np.clip(((m - B1) * d).sum(2) / (d * d).sum(), 0, 1)
    a = np.where(bad, a2, a); F = np.where(bad[..., None], GLASS, F); a[a < 0.06] = 0
    return np.dstack([F, a * 255])

C = matte(COMPOSITE_BOX); Fr = matte(FRONT_BOX)
ce = (C[..., 3] > 153).astype(float); fe = (Fr[..., 3] > 153).astype(float)
best = (-1, 0, 0)
for dy in range(-30, 31):
    for dx in range(-60, 61):
        ys0, yf0, xs0, xf0 = max(0, dy), max(0, -dy), max(0, dx), max(0, -dx)
        hh = min(ce.shape[0] - ys0, fe.shape[0] - yf0); ww = min(ce.shape[1] - xs0, fe.shape[1] - xf0)
        if hh < 200 or ww < 150: continue
        sc = (ce[ys0:ys0 + hh, xs0:xs0 + ww] * fe[yf0:yf0 + hh, xf0:xf0 + ww]).sum()
        if sc > best[0]: best = (sc, dy, dx)
dy, dx = best[1], best[2]
F2 = np.zeros_like(C)
ys0, yf0, xs0, xf0 = max(0, dy), max(0, -dy), max(0, dx), max(0, -dx)
hh = min(C.shape[0] - ys0, Fr.shape[0] - yf0); ww = min(C.shape[1] - xs0, Fr.shape[1] - xf0)
F2[ys0:ys0 + hh, xs0:xs0 + ww] = Fr[yf0:yf0 + hh, xf0:xf0 + ww]
dark = (C[..., :3].sum(2) < 200) & (C[..., 3] > 200)
rows = np.where(dark.sum(1) > 40)[0]; rows = rows[rows < C.shape[0] // 3]
F2[:int(rows.max()) + 3 if len(rows) else 60, :, 3] = 0

def shrink(arr, f):
    a = arr[..., 3:4] / 255.0
    pm = np.dstack([arr[..., :3] * a, a * 255])
    im = Image.fromarray(np.clip(pm, 0, 255).astype(np.uint8), 'RGBA')
    sm = np.asarray(im.resize((int(round(arr.shape[1] / f)), int(round(arr.shape[0] / f))), Image.BOX)).astype(float)
    al = sm[..., 3:4] / 255.0
    out = np.dstack([np.clip(np.where(al > 0.02, sm[..., :3] / np.maximum(al, 1e-3), 0), 0, 255), sm[..., 3:4]])
    out[..., 3] = np.where(out[..., 3] < 16, 0, out[..., 3])
    return out

top = np.where(C[..., 3].max(1) > 30)[0]; f = (top.max() - top.min()) / 100.0
Cs = shrink(C[top.min():top.max() + 1], f); Fs = shrink(F2[top.min():top.max() + 1], f)
a = Fs[..., 3] / 255; Fs[..., 3] = np.where(a > 0.8, Fs[..., 3], Fs[..., 3] * 0.5); Fs[..., 3] = np.where(Fs[..., 3] < 16, 0, Fs[..., 3])
Image.fromarray(Cs.astype(np.uint8), 'RGBA').save('assets/jar/jar_back.png')
Image.fromarray(Fs.astype(np.uint8), 'RGBA').save('assets/jar/jar_front.png')

def arr(name, im):
    im = im.astype(int); h, w = im.shape[:2]
    c565 = ((im[..., 0] & 0xF8) << 8) | ((im[..., 1] & 0xFC) << 3) | (im[..., 2] >> 3)
    out = [f"static const uint16_t {name}_565[{w * h}] = {{"]
    flat = c565.flatten(); al = im[..., 3].flatten()
    for i in range(0, len(flat), 16): out.append("    " + ", ".join("0x%04X" % v for v in flat[i:i + 16]) + ",")
    out += ["};", f"static const uint8_t {name}_a[{w * h}] = {{"]
    for i in range(0, len(al), 24): out.append("    " + ", ".join("%d" % v for v in al[i:i + 24]) + ",")
    out.append("};")
    return w, h, "\n".join(out)
w, h, b = arr('JAR_BACK', Cs); _, _, fr = arr('JAR_FRONT', Fs)
open('src/jar_art.h', 'w').write(f"""#pragma once
#include <stdint.h>
// The bug jar, from the artist's layered sprites (assets/jar/): matted from the same art on two backgrounds
// (true glass transparency), aligned, and shrunk to {w} x {h} sprite pixels (drawn 2x). Generated by
// tools/jar_export.py; don't edit by hand. BACK = the clear jar on its shelf (behind the bugs);
// FRONT = the glass highlights, the front of the wire handle and the rim (in front of the bugs).
constexpr int JAR_W = {w}, JAR_H = {h};
{b}
{fr}
""")
print(f'src/jar_art.h: {w} x {h}, front aligned by ({dy}, {dx})')
