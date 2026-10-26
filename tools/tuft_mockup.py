import numpy as np, random
from PIL import Image
W, H, HOR = 160, 120, 70
PXR = W / 1.10; ZK = 1.4 * PXR
def depth(y): return ZK / (y - HOR + 0.5)
NEAR_Z = 12.0                                   # stamps nearer than this; beyond, the existing ground stays
SIZES = [0.45, 0.62, 0.82, 1.05, 1.30, 1.60]    # six pre-drawn sizes (scale of a 14 x 11 tuft)

def make_tuft(scale, seed):
    """One pre-drawn tuft: blades fanning from a dark root to a lit tip; back blades a shade darker.
       Returns {(x, y): shade 0..5} with y = 0 at the top, the foot on the last row."""
    r = random.Random(seed)
    w = max(3, int(round(14 * scale))) | 1
    h = max(3, int(round(11 * scale)))
    nb = max(4, int(round(8 * scale + 2)))
    img = {}
    blades = []
    for i in range(nb):
        x0 = w / 2 + (r.random() - 0.5) * w * 0.3
        fan = (i / (nb - 1) - 0.5) * 1.5 + (r.random() - 0.5) * 0.35
        L = h * (0.55 + 0.45 * r.random()) * (1.0 - 0.25 * abs(fan))      # the middle stands tallest
        back = r.random() < 0.45
        blades.append((x0, fan, L, back))
    blades.sort(key=lambda b: not b[3])
    for x0, fan, L, back in blades:
        n = int(L * 1.6) + 2
        for s in range(n):
            t = s / (n - 1)
            y = h - 1 - t * L
            x = x0 + fan * t * L * 0.55 + 0.15 * fan * t * t * L          # blades curve outward as they rise
            shade = 1 + int(t * 3.6)
            if t > 0.8: shade = 5 if not back else 4                      # lit tips
            if back: shade -= 1
            wid = 2 if (t < 0.45 and scale >= 0.85) else 1
            for k in range(wid):
                img[(int(round(x)) + k, int(round(y)))] = max(0, min(5, shade))
    for x in range(w):                                                   # the dark at its root
        if abs(x - w / 2) < w * 0.4: img[(x, h - 1)] = 0
    return img, w, h

ATLAS = {(si, v): make_tuft(s, 1000 + si * 17 + v * 131) for si, s in enumerate(SIZES) for v in range(6)}

def mock(name):
    raw = open('/tmp/cap_%s.bin' % name, 'rb').read()
    fb = np.frombuffer(raw[:W * H], np.uint8).reshape(H, W).copy()
    pal = np.frombuffer(raw[W * H:], np.uint8).reshape(256, 3).astype(float)
    before = pal[fb].astype(np.uint8)
    out = before.astype(float).copy()
    ground = (fb >= 96) & (fb < 112)
    # six shades from this biome's own near-ground ramp (slots 96..99), extended darker and warmer
    p0, p1, p2, p3 = pal[96], pal[97], pal[98], pal[99]
    six = [p0 * 0.62 + np.array([0, 2, 6]) * 0.3, p0, p1, (p1 + p2) / 2, p2, np.clip(p3 * 1.04 + np.array([14, 10, -6]), 0, 255)]
    six = [np.clip(c, 0, 255) for c in six]
    under = np.where(ground, (fb.astype(int) - 96) % 4, 2)               # the light already on the ground (dapples, shadows)
    BASE = [1, 2, 4, 5]                                                    # the ground's own light, as its own colour among the six
    def put(x, y, shade):
        if 0 <= x < W and HOR < y < H and ground[y, x] and depth(y) < NEAR_Z:
            s = int(max(0, min(5, BASE[under[y, x]] + (shade - 3))))       # tufts shade around the ground's own light
            out[y, x] = six[s]
    # the short grass between tufts: a dark base over the near ground
    for y in range(HOR + 1, H):
        if depth(y) >= NEAR_Z: continue
        for x in range(W):
            if ground[y, x]: put(x, y, 3 - ((x * 7 + y * 3) % 7 == 0))
    # rows of mounds, far to near, each row's spacing set by its depth
    rng = random.Random(11)
    y = HOR + 1
    while y < H + 6:
        z = depth(y) if y > HOR else 99
        if z < NEAR_Z:
            size = min(len(SIZES) - 1, max(0, int(round((7.0 / z - 0.55) / 0.22))))
            x = rng.random() * 12 - 12
            while x < W + 8:
                n = rng.randint(3, 6)                                     # a mound of n tufts
                for k in range(n):
                    edge = abs(k - (n - 1) / 2) / ((n - 1) / 2 + 1e-6)
                    si = max(0, size - (1 if edge > 0.6 else 0))           # smaller at the edges
                    img, w, h = ATLAS[(si, rng.randrange(6))]
                    oy = y + int(edge * 2)                                 # rounded: the middle stands up
                    for (px, py), sh in img.items():
                        put(int(x + px - w / 2), oy - h + py, sh)
                    x += w * 0.62
                x += rng.uniform(6, 14) * SIZES[size]                      # a gap of short grass
            step = max(3, int(round(11 * SIZES[size] * 0.55)))
        else:
            step = 1
        y += step
    return before, out.astype(np.uint8)

names = ['meadow', 'highland', 'autumn', 'lakes']
S = Image.new('RGB', (2 * 482, len(names) * 362), (25, 25, 25))
Z = Image.new('RGB', (2 * 642, len(names) * 260), (25, 25, 25))
for i, n in enumerate(names):
    b, a = mock(n)
    for j, im in enumerate([b, a]):
        I = Image.fromarray(im)
        S.paste(I.resize((480, 360), Image.NEAREST), (j * 482, i * 362))
        Z.paste(I.crop((0, 74, 160, 120)).resize((640, 184), Image.NEAREST), (j * 642, i * 260))
S.save('/tmp/tuft_full.png'); Z.save('/tmp/tuft_zoom.png')
print('ok')
