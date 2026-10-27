"""Mantis Sprite Studio: the core (no GUI). Everything the studio does to art, palettes, packs and planets.

Game pixels: sprites are stored at the game's own resolution as palette indices (0 = clear). The screen shows
every game pixel as 2 x 2. Templates are the same art blown up by an integer scale for drawing in Krita or
Photoshop; importing shrinks it back exactly (clean pixel art) or by area (painted art) and snaps every pixel
to the 32-colour creature palette.
"""
import json, os, struct, math, copy
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULTS = os.path.join(HERE, 'defaults')
TIERS = ['far', 'mid', 'near']
MAGENTA, CYAN = (255, 0, 255), (0, 255, 255)
TEMPLATE_TARGET = 560          # a template frame is drawn about this many pixels tall

# ----------------------------------------------------------------------------------------------- bestiary
class Asset:
    """One piece of art the game takes: a creature + part/animation + frame count, at one or more sizes."""
    def __init__(self, creature, stem, frames, sizes, markers, note):
        self.creature, self.stem, self.frames, self.sizes, self.markers, self.note = creature, stem, frames, sizes, markers, note
    def file(self, tier):            # the file the game reads: /mp-sprites/<id>/<file>.mps
        return '%s_%s' % (self.stem, tier)
    def key(self, tier):
        return '%s/%s' % (self.creature['id'], self.file(tier))
    @property
    def label(self):
        return self.stem.replace('_', ' ')

def load_bestiary():
    with open(os.path.join(HERE, 'bestiary.json')) as f:
        return json.load(f)

def assets_for(creature):
    """The art this creature takes (empty for recolours: they use the deer's art)."""
    out = []
    if creature.get('recolour_of'):
        return out
    if creature.get('parts') == 'deer':
        ps = creature['part_sizes']
        for a in creature['assets']:
            part = a['part']
            if part == 'body':
                stem, sizes = 'body_' + a['anim'], creature['sizes']
            else:
                stem = part
                sizes = ps['antlers'] if part == 'antlers' else ps['antlers_away'] if part == 'antlers_away' else ps['tail_away'] if part.startswith('tail_away') else ps['tail']
            out.append(Asset(creature, stem, a['frames'], sizes, a.get('markers', ''), a.get('note', '')))
        return out
    for a in creature['assets']:
        out.append(Asset(creature, a['anim'], a['frames'], creature['sizes'], '', a.get('note', '')))
    return out

def spec_text(creature, bestiary):
    """The full spec for one creature, as plain text."""
    L = ['%s  (%s)' % (creature['name'], creature['id']), 'Where: %s' % creature.get('where', '')]
    if creature.get('desc'): L.append(creature['desc'])
    if creature.get('recolour_of'):
        L.append('\nThis creature uses the %s art, recoloured by the game. Edit the deer to change it.' % creature['recolour_of'])
        return '\n'.join(L)
    L.append('\nDraw facing RIGHT (the game mirrors). Transparent background. Colours: the 32-colour creature palette only.')
    L.append('Each file: frames side by side, left to right, each frame exactly the size below (in game pixels).')
    for a in assets_for(creature):
        sz = ', '.join('%s %dx%d' % (t, a.sizes[t][0], a.sizes[t][1]) for t in TIERS if t in a.sizes)
        mk = ('   markers: ' + a.markers) if a.markers else ''
        L.append('\n- %s: %d frame%s  [%s]%s\n    %s' % (a.label, a.frames, '' if a.frames == 1 else 's', sz, mk, a.note))
    if creature.get('parts') == 'deer':
        L.append('\nMarkers (one pure pixel each, removed on import): magenta #FF00FF = the head (where antlers sit) on bodies, '
                 'and on parts the lowest pixel where the part meets the body (it attaches just below); cyan #00FFFF = the tail '
                 'root on bodies. Markers are optional: the studio keeps every anchor and you can set them in its anchor editor. '
                 'Bodies are drawn without a tail.')
    return '\n'.join(L)

# ----------------------------------------------------------------------------------------------- palette
class Palette:
    def __init__(self, path=None):
        with open(path or os.path.join(DEFAULTS, 'palette.json')) as f:
            d = json.load(f)
        self.colours = [(c['index'], c['name'], tuple(c['rgb'])) for c in d['colours']]
        self.by_index = {i: rgb for i, _, rgb in self.colours}
        self._cache = {}
    def snap(self, rgb):
        """The nearest creature colour (perceptual weights) -> palette index."""
        rgb = tuple(int(v) for v in rgb[:3])
        hit = self._cache.get(rgb)
        if hit is not None: return hit
        r, g, b = rgb
        best, bi = 1e18, self.colours[0][0]
        for i, _, (R, G, B) in self.colours:
            rm = (r + R) / 2
            d = (2 + rm / 256) * (r - R) ** 2 + 4 * (g - G) ** 2 + (2 + (255 - rm) / 256) * (b - B) ** 2   # "redmean"
            if d < best: best, bi = d, i
        self._cache[rgb] = bi
        return bi
    def rgb(self, index):
        return self.by_index.get(index, (0, 0, 0))
    def write_gpl(self, path, name='MantisPlanet creatures'):
        with open(path, 'w') as f:
            f.write('GIMP Palette\nName: %s\nColumns: 8\n#\n' % name)
            for i, n, (r, g, b) in self.colours: f.write('%3d %3d %3d\t%s (%d)\n' % (r, g, b, n, i))
    def write_aco(self, path):
        write_aco(path, [(n, rgb) for _, n, rgb in self.colours])
    def swatch_png(self, path, cell=48):
        cols = 8; rows = (len(self.colours) + cols - 1) // cols
        im = Image.new('RGB', (cols * cell, rows * cell), (40, 40, 40)); d = ImageDraw.Draw(im)
        for k, (_, _, rgb) in enumerate(self.colours):
            x, y = (k % cols) * cell, (k // cols) * cell
            d.rectangle([x + 2, y + 2, x + cell - 3, y + cell - 3], fill=rgb)
        im.save(path)

def write_aco(path, named):
    """Photoshop swatches (.aco, versions 1 + 2: colours with names)."""
    with open(path, 'wb') as f:
        f.write(struct.pack('>HH', 1, len(named)))
        for _, (r, g, b) in named: f.write(struct.pack('>HHHHH', 0, r * 257, g * 257, b * 257, 0))
        f.write(struct.pack('>HH', 2, len(named)))
        for n, (r, g, b) in named:
            f.write(struct.pack('>HHHHH', 0, r * 257, g * 257, b * 257, 0))
            u = n.encode('utf-16-be')
            f.write(struct.pack('>I', len(n) + 1) + u + b'\x00\x00')

# ----------------------------------------------------------------------------------------------- sprites
class Sprite:
    """Game-pixel art: frames of palette indices (0 = clear) and per-frame anchors."""
    def __init__(self, w, h, frames, px, anchors=None):
        self.w, self.h, self.frames = w, h, frames
        self.px = list(px)                                   # w*h*frames
        self.anchors = anchors or [[] for _ in range(frames)]   # per frame: [hx, hy, tx, ty] or [ax, ay]
    def frame(self, f):
        n = self.w * self.h
        return self.px[f * n:(f + 1) * n]
    def to_image(self, pal, f=0, scale=1, markers=False):
        im = Image.new('RGBA', (self.w, self.h), (0, 0, 0, 0))
        fr = self.frame(f)
        im.putdata([(pal.rgb(v) + (255,)) if v else (0, 0, 0, 0) for v in fr])
        if markers and self.anchors[f]:
            a = list(self.anchors[f])
            if len(a) == 2: a[1] -= 1                          # a part's marker: the pixel just above its attach point
            if len(a) >= 2 and 0 <= a[0] < self.w and 0 <= a[1] < self.h: im.putpixel((a[0], a[1]), MAGENTA + (255,))
            if len(a) >= 4 and 0 <= a[2] < self.w and 0 <= a[3] < self.h: im.putpixel((a[2], a[3]), CYAN + (255,))
        return im.resize((self.w * scale, self.h * scale), Image.NEAREST) if scale != 1 else im
    def strip(self, pal, scale=1, markers=False):
        im = Image.new('RGBA', (self.w * self.frames * scale, self.h * scale), (0, 0, 0, 0))
        for f in range(self.frames): im.paste(self.to_image(pal, f, scale, markers), (f * self.w * scale, 0))
        return im

def write_mps(path, spr, anchors_per_frame):
    """The game's file: 'MPS', w, h, frames, then per frame its anchors (signed bytes), then the indices."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(b'MPS' + bytes([spr.w, spr.h, spr.frames]))
        for fr in range(spr.frames):
            a = list(spr.anchors[fr]) + [0] * (2 * anchors_per_frame)
            for v in a[:2 * anchors_per_frame]: f.write(struct.pack('b', max(-128, min(127, int(v)))))
        f.write(bytes(spr.px))

def read_mps(path, anchors_per_frame):
    d = open(path, 'rb').read()
    if len(d) < 6 or d[:3] != b'MPS': raise ValueError('not an MPS file')
    w, h, fr = d[3], d[4], d[5]
    hdr = 6 + fr * 2 * anchors_per_frame
    if len(d) != hdr + w * h * fr: raise ValueError('wrong size')
    anchors = [list(struct.unpack('%db' % (2 * anchors_per_frame), d[6 + i * 2 * anchors_per_frame:6 + (i + 1) * 2 * anchors_per_frame])) for i in range(fr)]
    return Sprite(w, h, fr, d[hdr:], anchors)

def anchors_per_frame(asset):
    return 2 if asset.stem.startswith('body_') else 1

class Defaults:
    """Today's game art (exported from the game itself)."""
    def __init__(self):
        with open(os.path.join(DEFAULTS, 'sprites.json')) as f:
            self.items = {(s['id'], s['asset'], s['tier']): s for s in json.load(f)['sprites']}
    def sprite(self, asset, tier):
        s = self.items.get((asset.creature['id'], asset.stem, tier))
        if not s: return None
        px = bytes.fromhex(s['px'])
        a = s['anchors']; k = 4 if asset.stem.startswith('body_') else 2
        anchors = [a[i * k:(i + 1) * k] for i in range(s['frames'])] if len(a) >= k * s['frames'] else [a[:k]] * s['frames']
        return Sprite(s['w'], s['h'], s['frames'], px, anchors)

def fit_to(spr, w, h, frames):
    """Today's art placed in the spec's frame size (bottom-centred), with the spec's frame count."""
    out = Sprite(w, h, frames, [0] * (w * h * frames))
    out.anchors = []
    for f in range(frames):
        src = spr.frame(f % spr.frames)
        ox, oy = (w - spr.w) // 2, h - spr.h
        for y in range(spr.h):
            for x in range(spr.w):
                v = src[y * spr.w + x]
                X, Y = x + ox, y + oy
                if v and 0 <= X < w and 0 <= Y < h: out.px[f * w * h + Y * w + X] = v
        a = list(spr.anchors[f % spr.frames]) if spr.anchors and spr.anchors[f % spr.frames] else []
        out.anchors.append([a[i] + (ox if i % 2 == 0 else oy) for i in range(len(a))])
    return out

def template_scale(h):
    return max(4, min(48, int(round(TEMPLATE_TARGET / max(1, h)))))

def make_template(asset, tier, defaults, pal, current=None, markers=False):
    """A PNG to draw over: the current art (yours, else today's game art) at drawing scale, frames side by side.
    Clean art only by default: anchors are kept by the studio (and set in its anchor editor)."""
    w, h = asset.sizes[tier]
    spr = current if current is not None else default_sprite(asset, tier, defaults)
    n = template_scale(h)
    return spr.strip(pal, n, markers=markers and bool(asset.markers)), n

def default_sprite(asset, tier, defaults):
    """Today's game art at the spec's size and frame count (blank if the game draws nothing at this size)."""
    w, h = asset.sizes[tier]
    d = defaults.sprite(asset, tier)
    return fit_to(d, w, h, asset.frames) if d else Sprite(w, h, asset.frames, [0] * (w * h * asset.frames), [[] for _ in range(asset.frames)])

# ----------------------------------------------------------------------------------------------- import
class ImportResult:
    def __init__(self, sprite, scale, warnings):
        self.sprite, self.scale, self.warnings = sprite, scale, warnings

def _is(c, ref, tol=40):
    return abs(c[0] - ref[0]) <= tol and abs(c[1] - ref[1]) <= tol and abs(c[2] - ref[2]) <= tol

def import_art(path_or_image, asset, tier, pal, fallback_anchors=None):
    """Artwork (any scale, PNG with alpha or JPG on a plain background) -> a Sprite at the spec's size."""
    im = path_or_image if isinstance(path_or_image, Image.Image) else Image.open(path_or_image)
    warnings = []
    w, h = asset.sizes[tier]; fr = asset.frames
    if im.mode != 'RGBA':
        rgb = im.convert('RGB')
        # no alpha (JPG): the background is the most common colour around the edge
        edge = [rgb.getpixel((x, 0)) for x in range(rgb.width)] + [rgb.getpixel((x, rgb.height - 1)) for x in range(rgb.width)]
        bg = max(set(edge), key=edge.count)
        im = rgb.convert('RGBA')
        im.putdata([(r, g, b, 0) if _is((r, g, b), bg, 28) else (r, g, b, 255) for (r, g, b, _) in im.getdata()])
        warnings.append('No transparency in the file: treated %s as the background.' % ('#%02x%02x%02x' % bg))
    else:
        im = im.convert('RGBA')
    sx, sy = im.width / float(w * fr), im.height / float(h)
    n = max(1, int(round(sy)))
    if abs(sx - sy) > 0.05 * sy or abs(sy - n) > 0.02 * n:
        warnings.append('The image is %dx%d; expected %d frames of %dx%d at a whole-number scale. Resampled to fit.' % (im.width, im.height, fr, w, h))
        im = im.resize((w * fr * n, h * n), Image.LANCZOS)
    elif im.width != w * fr * n or im.height != h * n:
        im = im.crop((0, 0, w * fr * n, h * n)) if im.width >= w * fr * n and im.height >= h * n else im.resize((w * fr * n, h * n), Image.NEAREST)
    data = im.load()
    # anchor markers: record their positions, then clear them from the image
    anchors = []
    marked = set()                                         # game pixels that held a marker: refilled from their neighbours
    for f in range(fr):
        mag, cy = [], []
        for y in range(h * n):
            for x in range(f * w * n, (f + 1) * w * n):
                r, g, b, a = data[x, y]
                if a < 128: continue
                if _is((r, g, b), MAGENTA, 50): mag.append((x, y)); data[x, y] = (0, 0, 0, 0); marked.add((f, x // n - f * w, y // n))
                elif _is((r, g, b), CYAN, 50): cy.append((x, y)); data[x, y] = (0, 0, 0, 0); marked.add((f, x // n - f * w, y // n))
        def centre(pts): return (int(sum(p[0] for p in pts) / len(pts) / n) - f * w, int(sum(p[1] for p in pts) / len(pts) / n)) if pts else None
        m, c = centre(mag), centre(cy)
        need = anchors_per_frame(asset) if asset.markers else 0
        have = fallback_anchors[f % len(fallback_anchors)] if fallback_anchors and fallback_anchors[f % len(fallback_anchors)] else None
        fb = have or ([w // 2, 0, 0, h // 2] if need == 2 else [w // 2, h])
        a = []
        if need >= 1:
            if m is not None: a += [m[0], m[1] + (1 if need == 1 else 0)]   # a part attaches just below its marker
            else:
                a += fb[:2]
                if not have: warnings.append('Frame %d: no %s point yet; set it in the anchor editor.' % (f + 1, 'head' if need == 2 else 'attach'))
        if need >= 2:
            if c is not None: a += list(c)
            else:
                a += fb[2:4] if len(fb) >= 4 else [0, h // 2]
                if not have: warnings.append('Frame %d: no tail-root point yet; set it in the anchor editor.' % (f + 1))
        anchors.append(a)
    # each game pixel: covered (by at least half its block)? its colour, averaged, snapped to the palette
    px = []
    for f in range(fr):
        for y in range(h):
            for x in range(w):
                rs = gs = bs = cov = 0.0
                for yy in range(y * n, (y + 1) * n):
                    for xx in range((f * w + x) * n, (f * w + x + 1) * n):
                        r, g, b, a = data[xx, yy]
                        if a: k = a / 255.0; rs += r * k; gs += g * k; bs += b * k; cov += k
                frac = cov / (n * n)
                px.append(pal.snap((rs / cov, gs / cov, bs / cov)) if frac >= 0.5 else 0)
    # a marker sits on the art: its pixel takes the commonest colour around it (if it's inside the art at all)
    for (f, x, y) in marked:
        if not (0 <= x < w and 0 <= y < h): continue
        i = f * w * h + y * w + x
        if px[i]: continue
        nb = [px[f * w * h + yy * w + xx] for xx, yy in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)) if 0 <= xx < w and 0 <= yy < h]
        nb = [v for v in nb if v]
        if len(nb) >= 2: px[i] = max(set(nb), key=nb.count)
    spr = Sprite(w, h, fr, px, anchors)
    if not any(px): warnings.append('Nothing opaque came through: is the art on a transparent background?')
    return ImportResult(spr, n, warnings)

# ----------------------------------------------------------------------------------------------- the deer, as the game builds it
def _top(px, w, h, x):
    for y in range(h):
        if px[y * w + x]: return y
    return h

def _raise(px, w, h, front, k):
    if k <= 0: return
    tops = [_top(px, w, h, x) for x in range(w)]
    body = [t < h and t >= int(h * 0.42) for t in tops]
    xs = [x for x in range(w) if body[x]]
    if not xs: return
    x0, x1 = xs[0], xs[-1]; span = max(1, (x1 - x0 + 1) // 3)
    a, b = (x1 - span + 1, x1) if front else (x0, x0 + span - 1)
    for x in range(a, b + 1):
        if not body[x]: continue
        m = (x - a) / float(b - a + 1)
        kk = int(round(k * (1 - abs(m - 0.5) * 1.2)))
        c = px[tops[x] * w + x]
        for j in range(1, kk + 1):
            if tops[x] - j >= 0 and not px[(tops[x] - j) * w + x]: px[(tops[x] - j) * w + x] = c

def _stamp(px, w, h, part, at):
    if not part: return
    pw, ph = part.w, part.h; ax, ay = part.anchors[0][:2] if part.anchors and part.anchors[0] else (pw // 2, ph)
    fr = part.frame(0)
    for y in range(ph):
        for x in range(pw):
            v = fr[y * pw + x]
            X, Y = at[0] - ax + x, at[1] - ay + y
            if v and 0 <= X < w and 0 <= Y < h: px[Y * w + X] = v

def _scaled(px, w, h, f):
    W, H = max(1, int(round(w * f))), max(1, int(round(h * f)))
    out = [px[int((y + 0.5) * h / H) * w + int((x + 0.5) * w / W)] for y in range(H) for x in range(W)]
    return out, W, H

CREAM = 213
def deer_variant(body, frame, sex, antlers=None, tail=None, tier='mid', side=True):
    """One frame of the deer as the game draws it: 'doe', 'buck', 'fawn' or 'cow'; tail = a part Sprite or None.
    Returns (pixels, w, h)."""
    w, h = body.w, body.h
    px = list(body.frame(frame))
    a = body.anchors[frame] if body.anchors and body.anchors[frame] else [w // 2, 0, 0, h // 2]
    rise = {'far': 0, 'mid': 1, 'near': 2}[tier]
    away = False
    if side and sex in ('doe', 'fawn', 'cow'): _raise(px, w, h, False, rise)   # side views only, as in the game
    if side and sex == 'buck': _raise(px, w, h, True, rise)
    if sex in ('buck', 'cow') and antlers: _stamp(px, w, h, antlers, (a[0], a[1]))
    if tail and len(a) >= 4: _stamp(px, w, h, tail, (a[2], a[3]))
    if sex == 'fawn':
        px, w, h = _scaled(px, w, h, 0.6)
        if h >= 8:
            for x in range(w):
                t = _top(px, w, h, x)
                if t < h and t >= int(h * 0.42):
                    for j in (1, 2):
                        y = t + j
                        if y < h and px[y * w + x] and (x * 3 + j * 2) % 5 == 0: px[y * w + x] = CREAM
    return px, w, h

# ----------------------------------------------------------------------------------------------- projects and packs
class Project:
    """A working folder: the converted art (project/<id>/<file>.mps + the source image path) and the planet."""
    def __init__(self, folder):
        self.folder = folder
        os.makedirs(folder, exist_ok=True)
        self.meta_path = os.path.join(folder, 'project.json')
        self.meta = json.load(open(self.meta_path)) if os.path.exists(self.meta_path) else {'sources': {}, 'planet_enabled': False}
    def save(self):
        with open(self.meta_path, 'w') as f: json.dump(self.meta, f, indent=1)
    def mps_path(self, asset, tier):
        return os.path.join(self.folder, asset.creature['id'], asset.file(tier) + '.mps')
    def has(self, asset, tier):
        return os.path.exists(self.mps_path(asset, tier))
    def load(self, asset, tier):
        return read_mps(self.mps_path(asset, tier), anchors_per_frame(asset)) if self.has(asset, tier) else None
    def store(self, asset, tier, spr, source):
        write_mps(self.mps_path(asset, tier), spr, anchors_per_frame(asset))
        self.meta['sources'][asset.key(tier)] = source; self.save()
    def remove(self, asset, tier):
        p = self.mps_path(asset, tier)
        if os.path.exists(p): os.remove(p)
        self.meta['sources'].pop(asset.key(tier), None); self.save()
    def planet_path(self):
        return os.path.join(self.folder, 'planet.json')
    def export(self, card_root, include_planet=True):
        """Copy everything into <card>/mp-sprites/ (the game's folder). Returns the number of files written."""
        dst = os.path.join(card_root, 'mp-sprites'); n = 0
        for d in os.listdir(self.folder):
            src = os.path.join(self.folder, d)
            if not os.path.isdir(src): continue
            for f in os.listdir(src):
                if f.endswith('.mps'):
                    os.makedirs(os.path.join(dst, d), exist_ok=True)
                    with open(os.path.join(src, f), 'rb') as a, open(os.path.join(dst, d, f), 'wb') as b: b.write(a.read())
                    n += 1
        if include_planet and self.meta.get('planet_enabled') and os.path.exists(self.planet_path()):
            os.makedirs(dst, exist_ok=True)
            with open(self.planet_path()) as a, open(os.path.join(dst, 'planet.json'), 'w') as b: b.write(a.read())
            n += 1
        elif os.path.exists(os.path.join(dst, 'planet.json')) and not self.meta.get('planet_enabled'):
            os.remove(os.path.join(dst, 'planet.json'))
        return n

# ----------------------------------------------------------------------------------------------- planets
def load_planet(path=None):
    with open(path or os.path.join(DEFAULTS, 'planet_default.json')) as f:
        return json.load(f)

def planet_slots(planet):
    """Every colour in a planet, as (group label, name, path) where path addresses it inside the JSON."""
    out = []
    for k, key in enumerate(planet['sky']):
        for f in ['zenith', 'horizon', 'glow', 'cloud_dark', 'cloud_lit', 'sun', 'ambient', 'key', 'fog']:
            out.append(('Sky at %02d:%02d' % (int(key['time'] * 24), int(key['time'] * 24 % 1 * 60)), f.replace('_', ' '), ('sky', k, f)))
    for f in planet['base']: out.append(('Base materials', f, ('base', f)))
    for g, n in (('accent', 'Creature colours 1-16'), ('creature', 'Creature colours 17-32'), ('aurora', 'Aurora')):
        for i in range(len(planet[g])): out.append((n, '%d' % (i + 1), (g, i)))
    for b, ramps in planet['biomes'].items():
        for r in ramps:
            for s in range(4): out.append(('Biome: %s' % b, '%s %d' % (r, s + 1), ('biomes', b, r, s)))
    return out

def planet_get(planet, path):
    v = planet
    for p in path: v = v[p]
    return tuple(v)

def planet_set(planet, path, rgb):
    v = planet
    for p in path[:-1]: v = v[p]
    v[path[-1]] = [int(c) for c in rgb]

def planet_swatch(planet, cell=20, cols=36):
    """The planet as one swatch image (edit it in Krita, import it back): returns (image, layout)."""
    slots = planet_slots(planet)
    rows = (len(slots) + cols - 1) // cols
    im = Image.new('RGB', (cols * cell, rows * cell), (0, 0, 0))
    d = ImageDraw.Draw(im); layout = []
    for k, (_, _, path) in enumerate(slots):
        x, y = (k % cols) * cell, (k // cols) * cell
        d.rectangle([x, y, x + cell - 1, y + cell - 1], fill=planet_get(planet, path))
        layout.append({'x': x + cell // 2, 'y': y + cell // 2, 'path': list(path)})
    return im, layout

def planet_from_swatch(planet, image, layout):
    p = copy.deepcopy(planet); im = image.convert('RGB')
    for e in layout:
        planet_set(p, [int(v) if isinstance(v, int) else v for v in e['path']], im.getpixel((e['x'], e['y'])))
    return p

# ----------------------------------------------------------------------------------------------- the Windows clipboard
def copy_image_to_clipboard(img):
    """Put an image on the Windows clipboard (as a DIB, with its alpha flattened onto light grey)."""
    import io, ctypes
    from ctypes import wintypes
    flat = Image.new('RGB', img.size, (200, 200, 200)); flat.paste(img, mask=img.split()[-1] if img.mode == 'RGBA' else None)
    out = io.BytesIO(); flat.save(out, 'BMP'); data = out.getvalue()[14:]
    CF_DIB, GMEM_MOVEABLE = 8, 0x0002
    k32, u32 = ctypes.windll.kernel32, ctypes.windll.user32
    k32.GlobalAlloc.restype = wintypes.HGLOBAL; k32.GlobalLock.restype = ctypes.c_void_p
    k32.GlobalLock.argtypes = [wintypes.HGLOBAL]; k32.GlobalUnlock.argtypes = [wintypes.HGLOBAL]
    u32.SetClipboardData.argtypes = [wintypes.UINT, wintypes.HANDLE]
    h = k32.GlobalAlloc(GMEM_MOVEABLE, len(data))
    p = k32.GlobalLock(h); ctypes.memmove(p, data, len(data)); k32.GlobalUnlock(h)
    if not u32.OpenClipboard(None): raise RuntimeError('the clipboard is busy')
    try:
        u32.EmptyClipboard(); u32.SetClipboardData(CF_DIB, h)
    finally:
        u32.CloseClipboard()
