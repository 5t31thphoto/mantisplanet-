import numpy as np, random, struct, math
from PIL import Image
exec(open('/tmp/tuftmock.py').read().split("def mock(name):")[0])      # the same tuft atlas, sizes and constants
ROW, DA = 0.30, 0.028                                                  # world rows every 0.3 m; tufts every 0.028 rad
def h32(x):
    x &= 0xffffffff; x ^= x >> 16; x = (x * 0x7feb352d) & 0xffffffff; x ^= x >> 15; x = (x * 0x846ca68b) & 0xffffffff; x ^= x >> 16
    return x
def wrapPi(a): return (a + math.pi) % (2 * math.pi) - math.pi
def frame_with_tufts(fb, pal, yaw, dist):
    before = pal[fb].astype(np.uint8)
    out = before.astype(float).copy()
    ground = (fb >= 96) & (fb < 112)
    p0, p1, p2, p3 = pal[96], pal[97], pal[98], pal[99]
    six = [np.clip(c, 0, 255) for c in [p0 * 0.62 + np.array([0, 2, 6]) * 0.3, p0, p1, (p1 + p2) / 2, p2, np.clip(p3 * 1.04 + np.array([14, 10, -6]), 0, 255)]]
    under = np.where(ground, (fb.astype(int) - 96) % 4, 2)
    BASE = [1, 2, 4, 5]
    def put(x, y, shade):
        if 0 <= x < W and HOR < y < H and ground[y, x] and depth(y) < NEAR_Z:
            out[y, x] = six[int(max(0, min(5, BASE[under[y, x]] + (shade - 3))))]
    for y in range(HOR + 1, H):
        if depth(y) >= NEAR_Z: continue
        for x in range(W):
            if ground[y, x]: put(x, y, 3 - ((x * 7 + y * 3) % 7 == 0))
    k0 = int(math.floor((dist + 2.5) / ROW)); k1 = int(math.ceil((dist + NEAR_Z + 2) / ROW))
    jlo = int(math.floor((yaw - 0.75) / DA)); jhi = int(math.ceil((yaw + 0.75) / DA))
    for k in range(k1, k0 - 1, -1):                                    # far rows first
        z = k * ROW - dist
        if z < 2.5: continue
        yf = HOR + ZK / z - 0.5
        size = min(len(SIZES) - 1, max(0, int(round((7.0 / z - 0.55) / 0.22))))
        dens = min(1.0, 2.2 / z)                                       # far rows thinner; more fade in as you come close
        for j in range(jlo, jhi + 1):
            hh = h32((k * 0x9E3779B1) ^ (j * 0x85EBCA77) ^ 0x51ED)
            mound = h32((k // 2) * 0x27D4EB2D ^ (j // 5) * 0x165667B1) & 0xff     # mounds of ~5 tufts, with gaps
            if mound < 70: continue
            if (hh & 0xff) / 256.0 >= dens: continue
            jit = ((hh >> 8) & 0xff) / 256.0 - 0.5
            a = (j + jit * 0.8) * DA
            x = W / 2 + wrapPi(a - yaw) * PXR
            if x < -12 or x > W + 12: continue
            si = max(0, size - (1 if ((hh >> 16) & 3) == 0 else 0))
            img, w, h = ATLAS[(si, (hh >> 20) % 6)]
            for (px, py), sh in img.items():
                put(int(x + px - w / 2), int(yf) - h + 1 + py, sh)
    return before, out.astype(np.uint8)
def run(name, ms):
    d = open('/tmp/seq_%s.bin' % name, 'rb').read(); fs = 160 * 120 + 768 + 8; n = len(d) // fs
    frames = []
    for i in range(n):
        o = i * fs
        fb = np.frombuffer(d[o:o + 19200], np.uint8).reshape(120, 160)
        pal = np.frombuffer(d[o + 19200:o + 19968], np.uint8).reshape(256, 3).astype(float)
        yaw, dist = struct.unpack('ff', d[o + 19968:o + 19976])
        b, a = frame_with_tufts(fb, pal, yaw, dist)
        S = Image.new('RGB', (2 * 320 + 4, 240), (20, 20, 20))
        S.paste(Image.fromarray(b).resize((320, 240), Image.NEAREST), (0, 0))
        S.paste(Image.fromarray(a).resize((320, 240), Image.NEAREST), (324, 0))
        frames.append(S)
        if i == n // 2: S.save('/tmp/motion_%s_mid.png' % name)
    frames[0].save('/tmp/motion_%s.gif' % name, save_all=True, append_images=frames[1:], duration=ms, loop=0)
    print(name, n, 'frames')
run('walk', 66); run('turn', 100)
