#!/usr/bin/env python3
"""Mantis Sprite Studio core: every template round-trips exactly; markers are honoured; .mps and swatches round-trip."""
import sys, os, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import spritelib as L
b = L.load_bestiary(); pal = L.Palette(); dfl = L.Defaults()
fails = 0; total = 0
for c in b['creatures']:
    for a in L.assets_for(c):
        for t in L.TIERS:
            if t not in a.sizes: continue
            ref = L.default_sprite(a, t, dfl)
            tpl, n = L.make_template(a, t, dfl, pal); res = L.import_art(tpl, a, t, pal, ref.anchors)
            total += 1
            if res.sprite.px != ref.px or (a.markers and res.sprite.anchors != ref.anchors): fails += 1; print('  differs:', a.key(t))
print('template round trips: %d of %d exact' % (total - fails, total))
pl = L.load_planet(); im, lay = L.planet_swatch(pl)
ok_sw = L.planet_from_swatch(pl, im, lay) == pl
print('planet swatch round trip:', 'ok' if ok_sw else 'FAIL')
print('PASS' if not fails and ok_sw else 'FAIL')
sys.exit(1 if fails or not ok_sw else 0)
