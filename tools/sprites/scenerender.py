"""The studio's own renderer: the game's scenes (exported as palette indices) recoloured by a Python port of the
game's palette code (render.cpp buildPalette, sampleKey, weatherKey, biomeRamps). Planet colours, time of day and
weather all apply exactly as in the game; creatures are placed into the scene as palette indices, so the scene
lights them the way the game does."""
import json, os, zlib, math

HERE = os.path.dirname(os.path.abspath(__file__))
R_SKY, R_CLOUD, R_SUN, R_MTN_FAR, R_MTN_NEAR, R_HILLS, R_GROUND, R_LEAF, R_BARK, R_ACCENT, R_WATER = 0, 16, 32, 48, 64, 80, 96, 112, 128, 144, 160
R_FAUNA, N_REFLECT, A_YEL_L, A_BLUE_D = 208, 96, 13, 9
HILL_R = [(16, 42, 50), (30, 66, 50), (58, 98, 58), (96, 128, 70)]
SKYF = ['zenith', 'horizon', 'glow', 'cloud_dark', 'cloud_lit', 'sun', 'ambient', 'key', 'fog']
# elevation, night and mist of each time-of-day key (not colours: not in planet.json)
KEY_EXTRA = [(-0.40, 1.0, 0.25), (-0.06, 0.55, 0.55), (0.07, 0.0, 0.60), (0.42, 0.0, 0.25), (0.80, 0.0, 0.10), (0.36, 0.0, 0.15),
             (0.06, 0.0, 0.35), (-0.08, 0.35, 0.45), (-0.35, 1.0, 0.30), (-0.40, 1.0, 0.25)]

def mix(a, b, t): return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t)
def scale(a, s): return (a[0] * s, a[1] * s, a[2] * s)
def add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def mul(a, b): return (a[0] * b[0] / 255.0, a[1] * b[1] / 255.0, a[2] * b[2] / 255.0)
def clampf(v, a, b): return a if v < a else b if v > b else v
def ramp_at(r, s):
    s = clampf(s, 0.0, 1.0) * 3.0; i = int(s)
    return r[3] if i >= 3 else mix(r[i], r[i + 1], s - i)
def is_water(i): return i >= 160 and not (R_FAUNA <= i < R_FAUNA + 16)

def sample_key(planet, tod):
    keys = planet['sky']
    k = 0
    while k < len(keys) - 2 and tod >= keys[k + 1]['time']: k += 1
    A, B = keys[k], keys[k + 1]
    t = clampf((tod - A['time']) / (B['time'] - A['time']), 0.0, 1.0); t = t * t * (3 - 2 * t)
    S = {f: mix(A[f], B[f], t) for f in SKYF}
    ea, eb = KEY_EXTRA[min(k, 9)], KEY_EXTRA[min(k + 1, 9)]
    S['elev'], S['night'], S['mist'] = (ea[i] + (eb[i] - ea[i]) * t for i in range(3))
    return S

def weather_key(S, sc):
    grey = clampf(sc['cloud'] * 0.6 - 0.15 + sc['rain'] * 0.45 + sc['storm'] * 0.25, 0.0, 0.9)
    dark = 1.0 - 0.28 * sc['rain'] - 0.22 * sc['storm']
    def desat(c, k, d):
        l = c[0] * 0.3 + c[1] * 0.55 + c[2] * 0.15
        return scale(mix(c, (l * 0.92, l * 0.96, l * 1.05), k), d)
    S['zenith'] = desat(S['zenith'], grey, dark); S['horizon'] = desat(S['horizon'], grey * 0.8, dark)
    S['glow'] = desat(S['glow'], grey * 0.9, dark); S['cloud_dark'] = desat(S['cloud_dark'], grey, dark * 0.85)
    S['cloud_lit'] = desat(S['cloud_lit'], grey * 0.8, dark); S['fog'] = desat(S['fog'], grey * 0.7, dark)
    S['ambient'] = scale(S['ambient'], 1 - 0.25 * grey); S['key'] = scale(S['key'], 1 - 0.75 * grey)

def build_palette(planet, sc):
    """The game's 256-colour palette for one scene (scene dict from scenes.json), from these planet colours."""
    S = sample_key(planet, sc['tod']); weather_key(S, sc)
    bio = planet['biomes'][sc['biome']]
    leafR = [tuple(c) for c in bio['leaf']]; grassR = [tuple(c) for c in bio['grass']]; stoneR = [tuple(c) for c in bio['stone']]
    barkR = [tuple(c) for c in bio['bark']]; rockR = [tuple(c) for c in bio['rock']]
    wetR = [mix(g, (16, 50, 52), 0.35) for g in grassR]; rock2R = [scale(r, 0.8) for r in rockR]
    if sc['moonlight'] > 0: S['ambient'] = mix(S['ambient'], scale(add(S['ambient'], (0.10, 0.12, 0.18)), 1.6), sc['moonlight'] * S['night'])
    snow = sc['snow']
    if snow > 0:
        SN = (236, 242, 252)
        leafR[3] = mix(leafR[3], SN, 0.85 * snow); leafR[2] = mix(leafR[2], SN, 0.6 * snow); stoneR[3] = mix(stoneR[3], SN, 0.8 * snow); wetR = list(grassR)
    base = {k: tuple(v) for k, v in planet['base'].items()}
    ACC = [tuple(c) for c in planet['accent']]; FAU = [tuple(c) for c in planet['creature']]
    amb, key, fog = S['ambient'], S['key'], S['fog']
    def lit(m, s): return add(scale(mul(m, amb), 0.28 + 0.82 * s), scale(mul(m, key), s * s * 0.55))
    shade = 1.0 - 0.3 * sc['forestNear']
    def fogc(c, f): return mix(c, fog, f)
    def litR(r, s):
        b = ramp_at(r, s); return add(scale(mul(b, amb), 0.6 + 0.45 * s), scale(mul(b, key), s * s * 0.4))
    def litG(s):
        b = mix(ramp_at(grassR, s), ramp_at(wetR, s), sc['wet'] * 0.7); return add(scale(mul(b, amb), 0.6 + 0.45 * s), scale(mul(b, key), s * s * 0.4))
    desert, karst = sc['desert'], sc['karst']
    P = [(0.0, 0.0, 0.0)] * 256
    for i in range(16):
        t = i / 15.0
        P[R_SKY + i] = mix(S['zenith'], S['horizon'], (i / 9.0) ** 1.15) if i < 10 else mix(S['horizon'], S['glow'], (i - 9) / 6.0)
        c = mix(S['cloud_dark'], S['cloud_lit'], t); P[R_CLOUD + i] = mix(c, S['horizon'], 0.22 * math.sin(t * math.pi))
        P[R_SUN + i] = mix(S['sun'], (255, 255, 255), 0.4) if i == 15 else mix(S['glow'], S['sun'], t)
        mf = mix(ramp_at(rockR, t), base['snow'], clampf((t - 0.74) / 0.18, 0, 1) * (1 - desert) * (1 - karst))
        P[R_MTN_FAR + i] = fogc(add(scale(mul(mf, amb), 0.55 + 0.5 * t), scale(mul(mf, key), t * t * 0.45)), 0.5)
        mn = mix(ramp_at(rock2R, t), base['snow'], clampf((t - 0.84) / 0.14, 0, 1) * (1 - desert) * (1 - karst))
        P[R_MTN_NEAR + i] = fogc(add(scale(mul(mn, amb), 0.55 + 0.5 * t), scale(mul(mn, key), t * t * 0.45)), 0.28)
        s = (i & 7) / 7.0; far = i >= 8
        hl = litR(HILL_R, s)
        if desert > 0: hl = mix(hl, add(scale(mul(ramp_at(rock2R, s), amb), 0.6 + 0.45 * s), scale(mul(ramp_at(rock2R, s), key), s * s * 0.4)), desert)
        P[R_HILLS + i] = fogc(hl, 0.44 if far else 0.16)
        lf = scale(litR(leafR, s), 1.0 if far else shade)
        if (i & 7) == 7: lf = add(lf, scale(key, 0.08))
        P[R_LEAF + i] = fogc(lf, 0.36 if far else 0.03)
        if not far:
            bk = scale(litR(barkR, s), shade)
            if i == 7: bk = add(bk, scale(key, 0.1))
            P[R_BARK + i] = fogc(bk, 0.04)
        else:
            st = scale(litR(stoneR, s), shade)
            if i == 15: st = add(st, scale(key, 0.08))
            P[R_BARK + i] = fogc(st, 0.04)
        band, sh = i >> 2, i & 3
        g = litG((0.1, 0.3, 0.5, 0.74)[sh])
        if band < 2: g = scale(g, shade)
        P[R_GROUND + i] = fogc(g, (band / 3.0) ** 1.5 * 0.7)
        P[R_ACCENT + i] = mix(lit(ACC[i], 0.7), mul(ACC[i], amb), 0.35)
    P[R_ACCENT + A_YEL_L] = mix((215, 255, 120), (255, 196, 104), max(sc['japan'], karst))
    if sc['tropical'] > 0.01:
        for i in range(R_WATER, min(256, R_WATER + N_REFLECT + 16)):
            if is_water(i): P[i] = mix(P[i], mul((40, 170, 130), amb), 0.3 * sc['tropical'])
    if sc['japan'] > 0.01: P[R_ACCENT + A_BLUE_D] = mix(P[R_ACCENT + A_BLUE_D], mix(lit((226, 70, 44), 0.75), mul((226, 70, 44), amb), 0.35), sc['japan'])
    if sc['aurora'] > 0.01 and S['night'] > 0.2:
        k = sc['aurora'] * S['night']
        for i in range(16):
            t = i / 15.0
            a = mix((8, 30, 44), (40, 225, 140), t * 2) if t < 0.5 else mix((40, 225, 140), (195, 110, 255), (t - 0.5) * 2)
            P[R_CLOUD + i] = mix(P[R_CLOUD + i], a, k)
    water = base['water']
    for i in range(N_REFLECT):
        P[R_WATER + i] = scale(P[i], 0.9) if R_SUN <= i < R_SUN + 16 else add(scale(P[i], 0.56), scale(mul(water, amb), 0.44))
    for i in range(16): P[R_FAUNA + i] = mix(lit(FAU[i], 0.7), mul(FAU[i], amb), 0.35)
    if sc.get('flash', 0) > 0.01:
        P = [mix(c, (225, 230, 255), sc['flash'] * (0.6 if (i < R_GROUND or is_water(i)) else 0.3)) for i, c in enumerate(P)]
    if sc.get('intro', 0) > 0:
        k = sc['intro'] ** 1.5; P = [mix(c, (238, 234, 244), k) for c in P]
    return [(max(0, min(255, int(c[0]))), max(0, min(255, int(c[1]))), max(0, min(255, int(c[2])))) for c in P]

class Scenes:
    """The bundled scenes: palette indices per scene, recoloured on demand."""
    def __init__(self, folder):
        with open(os.path.join(folder, 'scenes.json')) as f: self.meta = json.load(f)
        raw = zlib.decompress(open(os.path.join(folder, 'scenes.bin.z'), 'rb').read())
        self.fb, self.gamepal = [], []
        for i in range(len(self.meta['scenes'])):
            o = i * (19200 + 768)
            self.fb.append(raw[o:o + 19200]); self.gamepal.append(raw[o + 19200:o + 19968])
        self.horizon, self.zk = self.meta['horizon'], self.meta['zk']
    def find(self, biome, time):
        for i, s in enumerate(self.meta['scenes']):
            if s['biome'] == biome and s['time'] == time: return i
        return 0
    def biomes(self): return list(dict.fromkeys(s['biome'] for s in self.meta['scenes']))
    def times(self): return list(dict.fromkeys(s['time'] for s in self.meta['scenes']))
