#!/usr/bin/env python3
"""bestiary.json -> src/bestiary_gen.h (the game's table of moddable creatures; deer are handled by the parts
system in creatures.cpp). Run from the repo root after editing the bestiary:  python3 tools/sprites/gen_bestiary.py"""
import json, os
here = os.path.dirname(os.path.abspath(__file__))
b = json.load(open(os.path.join(here, 'bestiary.json')))
T = ['far', 'mid', 'near']
rows = []
for c in b['creatures']:
    if c.get('parts') == 'deer' or c.get('recolour_of'): continue
    tw = [c['sizes'].get(t, [0, 0])[0] for t in T]; th = [c['sizes'].get(t, [0, 0])[1] for t in T]
    an = c['assets']
    names = [a['anim'] for a in an][:2] + [''] * (2 - min(2, len(an)))
    par = -1 if 'seed_odd' not in c else (1 if c['seed_odd'] else 0)
    rows.append('    {"%s", %d, %d, %d, %.3ff, %s, {%s}, {%s}, {"%s", "%s"}},' % (
        c['id'], c['kind'], c['variant'], par, c.get('h_m', 0.3), 'true' if c.get('flock') else 'false',
        ', '.join(map(str, tw)), ', '.join(map(str, th)), names[0], names[1]))
out = '''#pragma once
#include <stdint.h>
// GENERATED from tools/sprites/bestiary.json by tools/sprites/gen_bestiary.py. Don't edit by hand.
// Moddable creatures (other than deer): their art on the card is /mp-sprites/<id>/<anim>_<tier>.mps
struct BestiaryEntry {
    const char* id; uint8_t kind, variant; int8_t seedParity;   // parity: -1 any, 0 even seed, 1 odd seed
    float heightM; bool flock;                                  // flock: one bird, the game draws the flock
    uint8_t tierW[3], tierH[3];                                 // far, mid, near (0: no art at that size)
    const char* anim[2];                                        // [0] at rest, [1] moving ("" if none)
};
static const BestiaryEntry BESTIARY[] = {
%s
};
static constexpr int BESTIARY_N = sizeof(BESTIARY) / sizeof(BESTIARY[0]);
''' % '\n'.join(rows)
open(os.path.join(here, '..', '..', 'src', 'bestiary_gen.h'), 'w').write(out)
print('src/bestiary_gen.h: %d creatures' % len(rows))
