import numpy as np, random, math
from PIL import Image
W, H, HOR = 160, 120, 70
def load(n):
    d = open('/tmp/prop_%s.bin' % n, 'rb').read()
    return np.frombuffer(d[:19200], np.uint8).reshape(120, 160).copy(), np.frombuffer(d[19200:], np.uint8).reshape(256, 3).astype(float)
clean, pal = load('clean')
def rgb(fb): return pal[fb].astype(float)
# ---------------- "before": the current props composited onto the clean frame ----------------
before_fb = clean.copy()
for n in ['boulder_near', 'boulder_mid', 'deer_near', 'deer_mid']:
    fb, _ = load(n); m = fb != clean; before_fb[m] = fb[m]
before = rgb(before_fb)
# ---------------- canopy: leaf clusters on the leaf ramp's own shades ----------------
def leaf_clusters(img, fb):
    out = img.copy()
    rng = random.Random(3)
    off = np.full((H, W), -3, int)                              # dark gaps between clusters
    for _ in range(900):
        cx, cy, r = rng.uniform(0, W), rng.uniform(0, H), rng.choice([2.2, 2.7, 3.2])
        for y in range(int(cy - r - 1), int(cy + r + 2)):
            for x in range(int(cx - r - 1), int(cx + r + 2)):
                if 0 <= x < W and 0 <= y < H:
                    dx, dy = x + 0.5 - cx, y + 0.5 - cy
                    if dx * dx + dy * dy <= r * r:
                        lit = -(dx + dy) / (r * 1.414)                  # light from the upper left
                        off[y, x] = 2 if lit > 0.45 else (0 if lit > -0.3 else -2)
    for y in range(H):
        for x in range(W):
            i = int(fb[y, x])
            if 112 <= i < 128:                                          # leaves (near 112-119, far 120-127)
                base, top = (112, 119) if i < 120 else (120, 127)
                k = int(max(base, min(top, i + off[y, x])))
                out[y, x] = pal[k]
    return out
# ---------------- bark: vertical furrows and ridges ----------------
def bark(img, fb):
    out = img.copy()
    for y in range(H):
        for x in range(W):
            i = int(fb[y, x])
            if 128 <= i < 136:                                          # bark, near ramp
                # vertical furrows every 3 px, broken now and then; the ridge beside each catches the light
                ph = x % 3
                broken = ((y // 4 + x * 7) % 5) == 0
                o = -3 if (ph == 0 and not broken) else (2 if ph == 1 else 0)
                out[y, x] = pal[int(max(128, min(135, i + o)))]
    return out
# ---------------- rock: natural boulder ----------------
def vnoise(x, y, seed):
    xi, yi = math.floor(x), math.floor(y); tx, ty = x - xi, y - yi
    def h(i, j): return ((i * 73856093) ^ (j * 19349663) ^ (seed * 83492791)) % 1000 / 1000.0
    tx, ty = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
    a, b, c, d = h(xi, yi), h(xi + 1, yi), h(xi, yi + 1), h(xi + 1, yi + 1)
    return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty
def draw_rock(img, cx, base, Rx, Ry, seed, stone):
    r = random.Random(seed)
    harm = [(r.uniform(0.05, 0.1), r.uniform(0, 6.28), k) for k in (2, 3, 5)]
    cy = base - Ry * 0.8
    moss = np.array([96, 124, 58]); moss2 = np.array([128, 150, 72])
    for y in range(int(cy - Ry - 2), int(base + 1)):
        for x in range(int(cx - Rx - 2), int(cx + Rx + 3)):
            if not (0 <= x < W and 0 <= y < H): continue
            u, v = (x + 0.5 - cx) / Rx, (y + 0.5 - cy) / Ry
            ang = math.atan2(v, u)
            rad = 1.0 + sum(a * math.sin(k * ang + p) for a, p, k in harm)
            if v > 0.7: rad *= 1.0 + (v - 0.7) * 1.2
            d2 = u * u + v * v
            if d2 > rad * rad or y > base: continue
            nz = math.sqrt(max(0.0, 1 - min(1, d2 / (rad * rad))))
            nx, ny = u / rad, v / rad
            mott = vnoise(x / (Rx * 0.35), y / (Ry * 0.35), seed) - 0.5         # weathering: broad mottling
            grain = vnoise(x / 1.7, y / 1.7, seed + 9) - 0.5                       # and fine grain
            L = 0.25 * nx - 0.8 * ny + 0.5 * nz                                    # sun above, a little to the right
            L = 0.28 + 0.80 * max(0.0, L) + 0.22 * mott + 0.10 * grain
            k = int(min(7, max(0, L * 7.4)))
            img[y, x] = stone[k]
            top = -ny > 0.35 and nz > 0.3
            if top and vnoise(x / 3.0, y / 2.2, seed + 3) > 0.62:                   # moss in patches on the top
                img[y, x] = moss2 if L > 0.6 else moss
    for c in range(2):                                                          # cracks: irregular, darker, a lit lip below
        x = cx + r.uniform(-0.5, 0.2) * Rx; y = cy - r.uniform(-0.1, 0.45) * Ry
        for st in range(int(Rx * r.uniform(0.35, 0.6))):
            xi, yi = int(x), int(y)
            if 0 <= xi < W and 1 <= yi < H - 1:
                u, v = (xi + 0.5 - cx) / Rx, (yi + 0.5 - cy) / Ry
                if u * u + v * v < 0.8:
                    img[yi, xi] = stone[1]
            x += 1; y += r.choice([0, 0, 1, -1, 1])
    for x in range(int(cx - Rx * 0.9), int(cx + Rx * 0.9)):                     # the dark crevice where it meets the ground
        if 0 <= x < W and int(base) < H: img[int(base), x] = stone[0] * 0.7
# ---------------- deer: native resolution, anatomy ----------------
def draw_deer(img, x0, y0, w, h, face_right=True):
    """Side-view deer at native resolution (designed 46 x 44, scaled to the box): legs ~half its height, body
       a little longer than tall, neck up at ~45 deg into a long head with tapered muzzle, big leaf ears, black
       nose and eye, pale throat, white tail; lit along the back, darker belly and far legs."""
    C = {'hi': np.array([188, 134, 82]), 'mid': np.array([156, 102, 60]), 'lo': np.array([116, 74, 44]),
         'dk': np.array([74, 48, 30]), 'pale': np.array([228, 214, 186]), 'blk': np.array([28, 22, 18])}
    sx, sy = w / 46.0, h / 44.0
    def P(px, py, c):
        X = x0 + int(round((px if face_right else 45 - px) * sx)); Y = y0 + int(round(py * sy))
        for ddx in range(max(1, int(round(sx)))):
            for ddy in range(max(1, int(round(sy)))):
                if 0 <= X + ddx < W and 0 <= Y + ddy < H: img[Y + ddy, X + ddx] = C[c]
    def body_ell(cx, cy, rx, ry):
        for y in range(int(cy - ry), int(cy + ry) + 1):
            for x in range(int(cx - rx), int(cx + rx) + 1):
                u, v = (x + 0.5 - cx) / rx, (y + 0.5 - cy) / ry
                if u * u + v * v <= 1: P(x, y, 'hi' if v < -0.55 else ('lo' if v > 0.5 else 'mid'))
    def leg(x, top, near, hind):
        c = 'mid' if near else 'dk'
        for y in range(top, 44):
            t = (y - top) / (43 - top)
            if hind: dx = 1 if t < 0.45 else (2 if t < 0.6 else 1)    # the hock bends back, then the cannon drops straight
            else: dx = 0 if t < 0.5 else (1 if t < 0.55 else 0)       # a slight knee
            wdt = 3 if t < 0.25 else (2 if t < 0.7 else 1)            # thick at the top, thinning to the hoof
            cc = 'blk' if t > 0.93 else c
            for k in range(wdt): P(x + dx + k - (1 if wdt == 3 else 0), y, cc)
    leg(9, 27, False, True); leg(32, 29, False, False)           # far legs first, darker
    body_ell(22, 23, 14, 6.5); body_ell(32, 23, 6, 7); body_ell(10, 22, 7, 7)
    leg(12, 27, True, True); leg(35, 29, True, False)
    for y in range(9, 21):                                        # neck, up and forward at ~45 deg
        t = (20 - y) / 11.0
        xa, xb = 30 + t * 5.5, 37 + t * 3.0
        for x in range(int(xa), int(xb) + 1): P(x, y, 'hi' if x < xa + 1.5 else 'mid')
    for y in range(4, 12):                                        # head: a long wedge to the muzzle
        for x in range(36, 47):
            if 6 <= y <= 10 and x <= 44 - (abs(y - 8) * 1.2) + (2 if y >= 8 else 0): P(x, y, 'mid' if y > 6 else 'hi')
    P(45, 9, 'blk'); P(46, 9, 'blk')                               # nose
    P(41, 7, 'blk')                                               # eye
    for (ex, lean) in [(37, -0.6), (39, -0.2)]:                   # ears: big, leaf-shaped, up and back
        for i in range(5):
            P(int(ex + lean * i), 6 - i, 'lo' if i else 'mid'); P(int(ex + lean * i) + 1, 6 - i, 'mid' if i < 4 else 'lo')
    for y in range(10, 15): P(37, y, 'pale'); P(38, y, 'pale')    # pale throat
    for y in range(17, 21): P(3, y, 'pale'); P(4, y, 'pale')      # the white tail
    P(3, 16, 'dk'); P(4, 16, 'dk')
# ---------------- tufts (the approved mockup, on this frame) ----------------
exec(open('/tmp/tuftmock.py').read().split("def mock(name):")[0])
def tufts(img, fb):
    out = img.copy()
    ground = (fb >= 96) & (fb < 112)
    p0, p1, p2, p3 = pal[96], pal[97], pal[98], pal[99]
    six = [np.clip(c, 0, 255) for c in [p0 * 0.62 + np.array([0, 2, 6]) * 0.3, p0, p1, (p1 + p2) / 2, p2, np.clip(p3 * 1.04 + np.array([14, 10, -6]), 0, 255)]]
    under = np.where(ground, (fb.astype(int) - 96) % 4, 2); BASE = [1, 2, 4, 5]
    def put(x, y, shade):
        if 0 <= x < W and HOR < y < H and ground[y, x] and depth(y) < NEAR_Z:
            out[y, x] = six[int(max(0, min(5, BASE[under[y, x]] + (shade - 3))))]
    for y in range(HOR + 1, H):
        if depth(y) < NEAR_Z:
            for x in range(W):
                if ground[y, x]: put(x, y, 3 - ((x * 7 + y * 3) % 7 == 0))
    rng = random.Random(11); y = HOR + 1
    while y < H + 6:
        z = depth(y) if y > HOR else 99
        if z < NEAR_Z:
            size = min(len(SIZES) - 1, max(0, int(round((7.0 / z - 0.55) / 0.22)))); x = rng.random() * 12 - 12
            while x < W + 8:
                n = rng.randint(3, 6)
                for k in range(n):
                    edge = abs(k - (n - 1) / 2) / ((n - 1) / 2 + 1e-6); si = max(0, size - (1 if edge > 0.6 else 0))
                    img_, w, h = ATLAS[(si, rng.randrange(6))]; oy = y + int(edge * 2)
                    for (px, py), sh in img_.items(): put(int(x + px - w / 2), oy - h + py, sh)
                    x += w * 0.62
                x += rng.uniform(6, 14) * SIZES[size]
            y += max(3, int(round(11 * SIZES[size] * 0.55)))
        else: y += 1
    return out
# ---------------- compose ----------------
after = tufts(rgb(clean), clean)
after = leaf_clusters(after, clean)
after = bark(after, clean)
stone = [pal[136 + k] for k in range(8)]
draw_rock(after, 42, 113, 26, 17, 7, stone)
draw_rock(after, 120, 95, 10.5, 7, 9, stone)
draw_deer(after, 30, 66, 46, 44, True)
draw_deer(after, 110, 70, 23, 22, False)
S = Image.new('RGB', (2 * 642, 482), (25, 25, 25))
S.paste(Image.fromarray(before.astype(np.uint8)).resize((640, 480), Image.NEAREST), (0, 0))
S.paste(Image.fromarray(np.clip(after, 0, 255).astype(np.uint8)).resize((640, 480), Image.NEAREST), (642, 0))
S.save('/tmp/suite_meadow.png')
crops = []
for (x0, y0, x1, y1) in [(16, 82, 68, 118), (28, 62, 80, 112)]:
    for im in (before, after):
        crops.append(Image.fromarray(np.clip(im[y0:y1, x0:x1], 0, 255).astype(np.uint8)).resize(((x1 - x0) * 6, (y1 - y0) * 6), Image.NEAREST))
Z = Image.new('RGB', (crops[0].width * 2 + 10, crops[0].height + crops[2].height + 10), (25, 25, 25))
Z.paste(crops[0], (0, 0)); Z.paste(crops[1], (crops[0].width + 10, 0))
Z.paste(crops[2], (0, crops[0].height + 10)); Z.paste(crops[3], (crops[2].width + 10, crops[0].height + 10))
Z.save('/tmp/suite_closeups.png')
print('ok')
