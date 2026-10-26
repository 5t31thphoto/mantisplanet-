#include "field.h"
#include "universe.h"
#include "scene.h"
#include "journey.h"
#include "bugs.h"
#include <math.h>

namespace mantis {

static FieldObj s_obj[FIELD_N];
static FieldCritter s_cr[CRIT_N];
static float s_nextKind[CR_KINDS] = {3.f, 6.f, 10.f, 4.f, 5.f, 12.f, 8.f, 2.f, 3.f};
static struct { uint8_t kind; float dist; } s_bolts[CRIT_N];
static int s_nBolts = 0;
static int s_wetI = 0;
static Firefly s_ff[FIREFLY_N];
static uint32_t s_rng = 0x9E3779B9u;

static float timeOfDay01() { return g_scene.tod; }
static float frand() { s_rng = hash32(s_rng + 0x6D2B79F5u); return (s_rng >> 8) * (1.f / 16777216.f); }
static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline float wrapPi(float a) { return a - kTau * floorf((a + 3.14159265f) / kTau); }

static float rockField(float a, float D);
float rockFieldAt(float a, float D) { return rockField(a, D); }
static float rockField(float a, float D) {
    float r = fbmP(angU(a, 5), D * 0.015f, 5, 2, 0x50C4u);
    return clampf((r - 0.38f) * 3.f + (biomeAt(D).rocks - 0.3f) * 1.5f, 0.f, 1.f);
}

static void spawn(FieldObj& o, float angle, float range, float distance) {
    o.x = range * cosf(angle);
    o.y = range * sinf(angle);
    o.seed = hash32(s_rng ^ (uint32_t)(range * 977.f) ^ (uint32_t)(angle * 7919.f));
    o.type = OBJ_NONE;
    o.solid = false;
    o.radius = 0.f;
    o.a = o.b = 0.f;
    o.var = 0;
    o.wet = false;
    const float D = distance + range;
    float lk = lakeField(angle, D);
    const BiomeParams& bq = biomeAt(D);
    if (bq.reeds > 0.3f && lk > LAKE_T - 0.1f && lk < LAKE_T + 0.01f && frand() < 0.75f * bq.reeds) {   // reeds at the water's edge
        o.type = OBJ_REEDS; o.a = 0.9f + frand() * 0.7f; return;
    }
    if (bq.ocean > 0.5f && lk > LAKE_T && frand() < 0.03f) {          // sea stacks out in the surf
        o.type = OBJ_OUTCROP; o.var = 32; o.a = 1.4f + frand() * 1.8f; o.b = 4.f + frand() * 6.f; o.radius = o.a * 0.85f; o.solid = true; return;
    }
    if (lk > LAKE_T - 0.03f || streamField(angle, D) < 1.f || paddyAt(angle, D)) return;   // nothing stands in the water
    const float forest = forestField(angle, D);
    const float rock = rockField(angle, D);
    const float p = frand();
    const float treeP = 0.07f + forest * 0.62f;               // a real forest: find your way through it
    const float stoneP = 0.015f + rock * 0.11f;
    const BiomeParams& bp = biomeAt(D);
    if (bp.karst > 0.5f) {                                      // karst: limestone pillars, bamboo
        float q = frand();
        if (q < 0.02f + 0.04f * rock) { o.type = OBJ_OUTCROP; o.var = 64; o.a = 0.9f + frand() * 0.8f; o.b = 5.f + frand() * 6.f; o.radius = o.a * 0.85f; o.solid = true; return; }
        if (forest > 0.4f && q < 0.25f) { o.type = OBJ_BAMBOO; o.a = 5.f + frand() * 3.f; o.b = 1.f + frand() * 0.8f; o.radius = 0.6f; o.solid = true; return; }
    }
    if (bp.tropical > 0.5f && frand() < 0.2f) { o.type = OBJ_BIGLEAF; o.a = 0.9f + frand() * 0.9f; return; }   // elephant ears
    if (bp.giant > 0.5f) {                                      // ancient redwoods: giants, ferns, fallen logs
        float q = frand();
        if (q < 0.006f) { o.type = OBJ_TUNNELTREE; o.a = 40.f + frand() * 15.f; o.radius = 2.8f + frand() * 0.6f; return; }   // walk through it
        if (q < 0.014f) { o.type = OBJ_HOLLOWLOG; o.a = 3.2f + frand() * 1.2f; return; }                                     // walk through it
        if (q < 0.04f) { o.type = OBJ_LOG; o.a = 4.f + frand() * 5.f; o.b = 0.7f + frand() * 0.5f; o.radius = 1.f; o.solid = true; return; }
        if (q < 0.35f) { o.type = OBJ_TUFT; return; }                                                 // ferns
    }
    if (bp.desert > 0.5f) {                                     // dry country: cacti, sage, red rock
        float q = frand();
        if (q < 0.05f) { o.type = OBJ_SAGUARO; o.a = 3.f + frand() * 3.5f; o.b = frand(); o.radius = 0.3f; o.solid = true; return; }
        if (q < 0.1f) { o.type = OBJ_PEAR; o.a = 0.6f + frand() * 0.5f; return; }
        if (q < 0.26f) { o.type = OBJ_BUSH; o.a = 0.35f + frand() * 0.35f; return; }                // sagebrush
        if (q < 0.26f + 0.06f * rock) {
            o.type = OBJ_OUTCROP;
            float v = frand();
            if (v < 0.3f) { o.var = 8; o.a = 0.5f + frand() * 0.4f; o.b = 3.f + frand() * 3.5f; }      // hoodoo
            else if (v < 0.45f) { o.var = 16; o.a = 3.f + frand() * 1.5f; o.b = 4.f + frand() * 2.f; o.radius = 0.f; o.solid = false; return; }   // an arch: walk under it
            else { o.var = 4; o.a = 2.5f + frand() * 3.f; o.b = 2.5f + frand() * 3.f; }                 // a little mesa
            o.radius = o.a * 0.85f; o.solid = true; return;
        }
        if (q < 0.36f) { o.type = OBJ_BOULDER; o.a = 0.35f + frand() * 0.7f; o.radius = o.a * 0.9f; o.solid = true; return; }
        if (q < 0.36f + 0.1f * bp.flowers) { o.type = bp.snow > 0.5f ? OBJ_NONE : OBJ_FLOWERS; return; }
        return;                                                // open sand
    }
    if (bp.japan > 0.5f) {                                      // the garden's own furniture
        float q = frand();
        if (q < 0.035f) { o.type = OBJ_LANTERN; o.a = 1.3f + frand() * 0.5f; o.radius = 0.35f; o.solid = true; return; }
        if (q < 0.042f) { o.type = OBJ_TORII; o.a = 4.2f; o.b = 3.4f; return; }           // walk through it
        if (forest > 0.6f && q < 0.2f) { o.type = OBJ_BAMBOO; o.a = 5.f + frand() * 3.f; o.b = 1.f + frand() * 0.8f; o.radius = 0.6f; o.solid = true; return; }
    }
    if (p < treeP) {
        if (frand() < bp.pine) {
            o.type = OBJ_PINE;
            o.a = 4.f + frand() * 4.5f + bp.tall * (1.5f + frand() * 3.f);   // height (old growth stands taller)
            o.radius = 0.28f;
            if (bp.japan > 0.5f) { o.var = 5; o.a *= 0.7f; }                 // black pines: lower, wider, deep green
            if (bp.giant > 0.5f) { o.var = 6; o.a = 28.f + frand() * 16.f; o.radius = 1.f + frand() * 0.6f; }   // a redwood
        } else {
            o.type = OBJ_BROAD;
            o.a = 4.5f + frand() * 3.5f;                // height
            o.b = 1.3f + frand() * 1.2f;                // crown radius
            o.radius = 0.22f + o.b * 0.06f;             // trunk
            if (bp.japan > 0.5f) { o.var = frand() < 0.5f ? 3 : 4; o.a *= 0.75f; }   // cherry or maple
            if (bp.tropical > 0.5f) { o.var = 7; o.a *= 1.25f; o.b *= 1.9f; o.radius *= 1.3f; }  // rainforest giants: broad umbrella crowns, hung with vines
        }
        o.solid = true;
    } else if (p < treeP + stoneP) {
        if (rock > 0.55f && bp.japan < 0.5f && frand() < 0.3f + 0.4f * fmaxf(bp.ruins, bp.falls)) {   // the garden keeps to garden stones
            o.type = OBJ_OUTCROP;
            o.a = 1.8f + frand() * 2.4f;                // half width
            o.b = 2.2f + frand() * 3.8f;                // height
            float v = frand();
            if (v < bp.falls) { o.var = 1; o.b += 1.5f; }            // waterfall country: falls off the cliffs
            else if (v < bp.falls + bp.ruins) o.var = 2;             // the highlands: old walls
            o.radius = o.a * 0.85f;
        } else {
            o.type = OBJ_BOULDER;
            o.a = 0.35f + frand() * 0.75f;              // size
            o.radius = o.a * 0.9f;
        }
        o.solid = true;
    } else if (p < treeP + stoneP + 0.10f) {
        o.type = OBJ_BUSH;
        o.a = 0.5f + frand() * 0.5f;
    } else if (p < treeP + stoneP + 0.24f) {
        o.type = bp.snow > 0.5f ? OBJ_NONE : OBJ_FLOWERS;
    } else if (p < treeP + stoneP + 0.40f) {
        o.type = OBJ_TUFT;
    }
}

static void spawnClear(FieldObj& o, float ang, float r, float D) {
    spawn(o, ang, r, D);
    if (!o.solid || o.type == OBJ_NONE) return;
    for (int i = 0; i < FIELD_N; i++) {
        const FieldObj& q = s_obj[i];
        if (&q == &o || !q.solid || q.type == OBJ_NONE) continue;
        float dx = q.x - o.x, dy = q.y - o.y, m = (q.radius + o.radius) * 0.75f + 0.2f;
        if (dx * dx + dy * dy < m * m) { o.type = OBJ_NONE; o.solid = false; return; }   // it would grow out of its neighbour
    }
}

void fieldInit(float yaw, float distance) {
    s_rng = hash32(worldSeed() ^ 0xF1E1Du);
    for (int i = 0; i < FIELD_N; i++) { s_obj[i].type = OBJ_NONE; s_obj[i].solid = false; }   // a clean slate (no ghosts to collide with)
    for (int i = 0; i < FIELD_N; i++) {
        float ang = frand() * kTau;
        float rng = FIELD_R * sqrtf(frand());
        if (rng < 3.f) rng = 3.f + frand() * 4.f;
        spawnClear(s_obj[i], ang, rng, distance);
        // don't start with something standing on top of you or right in your face
        FieldObj& o = s_obj[i];
        if (o.solid) {
            float d = sqrtf(o.x * o.x + o.y * o.y);
            float rel = fabsf(wrapPi(atan2f(o.y, o.x) - yaw));
            if (d < o.radius + 2.f || (rel < 0.5f && d < 9.f)) o.type = OBJ_NONE, o.solid = false;
        }
    }
    for (int i = 0; i < CRIT_N; i++) s_cr[i].on = false;
    for (int i = 0; i < FIREFLY_N; i++) {
        float a = yaw + (frand() - 0.5f) * 2.6f, r = 2.5f + frand() * 11.f;      // they gather where you look
        s_ff[i] = {r * cosf(a), r * sinf(a), 0.2f + frand() * 1.6f, frand(), frand() * kTau};
    }
}

float fieldStep(float yaw, float ds, float distance, bool* bumped) {
    *bumped = false;
    if (ds <= 0.f) return 0.f;

    // shore: the lake field is part of the panorama, so probe it along the heading
    for (int k = -2; k <= 2; k++)
        for (int j = 0; j <= 4; j++)
            if (lakeField(yaw + k * 0.07f, distance + ds + 0.3f + (LAKE_STOP - 0.3f) * j / 4.f) > LAKE_T && biomeAt(distance + ds).snow < 0.5f) { *bumped = true; return 0.f; }   // frozen water bears you

    const float hx = cosf(yaw), hy = sinf(yaw);
    float sx = hx * ds, sy = hy * ds;

    // Something solid in the way: head-on you stop dead (turn and find another way); glancing off
    // the side of it you slide past. Nothing ever steers you.
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < FIELD_N; i++) {
            const FieldObj& o = s_obj[i];
            if (!o.solid || o.wet) continue;
            float minD = o.radius + BODY_R;
            float dx = o.x - sx, dy = o.y - sy;
            if (dx * dx + dy * dy >= minD * minD) continue;
            float len = sqrtf(o.x * o.x + o.y * o.y);
            if (len < 1e-4f) continue;
            float nx = o.x / len, ny = o.y / len;
            float facing = hx * nx + hy * ny;              // cos of the angle between heading and the obstacle
            *bumped = true;
            if (facing > 0.82f) { sx = sy = 0.f; break; }   // within ~35 deg: a wall in front of you
            float dot = sx * nx + sy * ny;
            if (dot > 0.f) { sx -= dot * nx; sy -= dot * ny; }
        }
        if (sx == 0.f && sy == 0.f) break;
    }
    // never end up inside something (two obstacles can pinch the slide)
    for (int i = 0; i < FIELD_N; i++) {
        const FieldObj& o = s_obj[i];
        if (!o.solid || o.wet) continue;
        float minD = o.radius + BODY_R;
        float dx = o.x - sx, dy = o.y - sy;
        float before = o.x * o.x + o.y * o.y;
        float after = dx * dx + dy * dy;
        if (after < minD * minD && after < before) { sx = sy = 0.f; *bumped = true; break; }
    }

    float moved = sqrtf(sx * sx + sy * sy);
    if (moved <= 0.f) return 0.f;
    for (int i = 0; i < FIELD_N; i++) { s_obj[i].x -= sx; s_obj[i].y -= sy; }
    for (int i = 0; i < CRIT_N; i++) if (s_cr[i].on) { s_cr[i].x -= sx; s_cr[i].y -= sy; s_cr[i].tx -= sx; s_cr[i].ty -= sy; }
    bugsShift(sx, sy);
    for (int i = 0; i < FIREFLY_N; i++) { s_ff[i].x -= sx; s_ff[i].y -= sy; }

    // forget what fell out behind, dream it again at the far edge ahead
    const float D = distance + moved;
    for (int i = 0; i < FIELD_N; i++) {
        FieldObj& o = s_obj[i];
        if (o.x * o.x + o.y * o.y <= FIELD_R * FIELD_R) continue;
        float ang = yaw + asinf(2.f * frand() - 1.f);   // cos-weighted across the front half
        spawnClear(o, ang, FIELD_R * (0.94f + 0.06f * frand()), D);
    }
    return moved;
}

static float s_petYaw = 0.f;
int fieldTick(float yaw, float distance, float t, float dt, float speed, float stillFor) {
    s_petYaw = yaw;
    // fireflies drift a little on their own
    for (int i = 0; i < FIREFLY_N; i++) {
        Firefly& f = s_ff[i];
        f.drift += dt * (0.2f + 0.1f * (i & 3));
        f.x += cosf(f.drift) * 0.25f * dt;
        f.y += sinf(f.drift * 1.3f) * 0.25f * dt;
    }
    for (int i = 0; i < FIREFLY_N; i++) {
        Firefly& f = s_ff[i];
        float rel = fabsf(wrapPi(atan2f(f.y, f.x) - yaw));
        if (f.x * f.x + f.y * f.y < 14.f * 14.f && rel < 1.6f) continue;
        float a = yaw + (frand() - 0.5f) * 2.4f, r = 6.f + frand() * 8.f;
        f.x = r * cosf(a); f.y = r * sinf(a);
    }

    // trees and rocks drift relative to the lakes as you move: re-check a slice of them each frame
    for (int k = 0; k < 24; k++) {
        FieldObj& o = s_obj[s_wetI];
        s_wetI = (s_wetI + 1) % FIELD_N;
        if (o.type == OBJ_NONE || (o.type == OBJ_OUTCROP && (o.var & 32))) continue;
        float r = sqrtf(o.x * o.x + o.y * o.y);
        float oa = atan2f(o.y, o.x);
        bool wet = lakeField(oa, distance + r) > LAKE_T - 0.02f || paddyAt(oa, distance + r);
        if (!wet && o.wet && r < o.radius + BODY_R + 0.3f) continue;     // don't turn solid around you
        o.wet = wet;
    }

    s_nBolts = 0;
    bool still = stillFor > 8.f;
    float tod = timeOfDay01();
    bool day = tod > 0.27f && tod < 0.76f;
    // ----- spawning: each kind on its own clock, in the places it belongs -----
    auto freeSlot = [&]() -> FieldCritter* { for (int i = 0; i < CRIT_N; i++) if (!s_cr[i].on) return &s_cr[i]; return nullptr; };
    auto count = [&](uint8_t k) { int n = 0; for (int i = 0; i < CRIT_N; i++) if (s_cr[i].on && s_cr[i].kind == k) n++; return n; };
    auto begin = [&](FieldCritter* c, uint8_t kind, float a, float r) {
        *c = FieldCritter();
        c->on = true; c->kind = kind; c->x = r * cosf(a); c->y = r * sinf(a); c->h = 0.f;
        c->t0 = t; c->dir = frand() < 0.5f ? -1 : 1; c->faceRight = frand() < 0.5f; c->fleeing = false;
        c->seed = hash32(s_rng + kind * 977u); c->splashT = -1.f;
        const BiomeParams& bv = biomeAt(distance + r);
        c->variant = (kind <= CR_RABBIT && bv.hare > 0.5f) || (kind == CR_DUCK && bv.heron > 0.5f) || (kind == CR_BUTTERFLY && bv.dragonfly > 0.5f) ? 1 : 0;
        if (bv.japan > 0.5f && (kind == CR_DEER || kind == CR_DUCK)) c->variant = 2;     // sika deer; cranes and mandarin ducks
        if (bv.desert > 0.5f && kind <= CR_BIRDS) c->variant = 3;                         // mule deer, jackrabbit, roadrunner
        if (bv.giant > 0.5f && kind == CR_DEER) c->variant = 4;                           // elk
        if (bv.tundra > 0.5f && kind <= CR_BIRDS) c->variant = 5;                         // a caribou herd, arctic fox, snowy owl
        if (bv.karst > 0.5f && kind == CR_DUCK) c->variant = 2;                           // cranes on the terraces
        if (bv.karst > 0.5f && kind == CR_BUTTERFLY) c->variant = 2;                      // kingfishers
        if (bv.tropical > 0.5f && kind == CR_BUTTERFLY) c->variant = 3;                   // blue morphos
        if (bv.tropical > 0.5f && kind == CR_BIRDS) c->variant = 6;                       // parrots
        if (bv.stag > 0.5f && kind == CR_DEER && (g_scene.tod > 0.7f || g_scene.tod < 0.25f) && frand() < 0.3f) c->variant = 7;   // the white stag
        c->mom = -1; c->mood = DEER_GRAZE; c->moodT = 2.f + frand() * 5.f; c->awayFlee = false;
        c->sex = kind != CR_DEER ? 0 : c->variant == 7 ? 1 : frand() < 0.15f ? 1 : 0;   // mostly does; bucks less often
    };
    for (uint8_t kind = 0; kind < CR_KINDS; kind++) {
        if (t < s_nextKind[kind]) continue;
        FieldCritter* c = freeSlot();
        float a = yaw + (frand() - 0.5f) * 0.8f;
        s_nextKind[kind] = t + 3.f + frand() * 6.f;
        if (!c) break;
        if (kind <= CR_BIRDS) {                                   // deer, rabbits, a flock on the ground
            if (count(CR_DEER) + count(CR_RABBIT) + count(CR_BIRDS) >= 2) continue;
            float r = still ? 5.5f + frand() * 4.f : 9.f + frand() * 7.f;
            if (lakeField(a, distance + r) > LAKE_T - 0.04f) continue;
            float k = frand();
            bool trop = biomeAt(distance + r).tropical > 0.5f;    // no deer in the rainforest: parrots and small things
            begin(c, trop ? (k < 0.55f ? CR_BIRDS : CR_RABBIT) : k < 0.45f ? CR_DEER : (k < 0.8f ? CR_RABBIT : CR_BIRDS), a, r);
            c->idleT = still ? 10.f + frand() * 14.f : 1.8f + frand() * 2.5f;
            s_nextKind[CR_DEER] = s_nextKind[CR_RABBIT] = s_nextKind[CR_BIRDS] = t + 4.f + frand() * 8.f;
            if (c->kind == CR_DEER && c->sex == 0 && c->variant != 5 && frand() < 0.45f) {   // a doe, often with her fawn beside her
                if (FieldCritter* f = freeSlot()) {
                    *f = *c;
                    f->sex = 2; f->mom = (int8_t)(c - s_cr);
                    float ox = -sinf(a) * 0.9f, oy = cosf(a) * 0.9f;              // beside her, across the line of sight
                    float side = frand() < 0.5f ? -1.f : 1.f;
                    f->x += ox * side; f->y += oy * side;
                    f->seed = hash32(c->seed ^ 0xFA3Du);
                }
            }
        } else if (kind == CR_LIZARD) {                            // sun-warmed rock and sand
            if (biomeAt(distance).desert < 0.5f || count(CR_LIZARD) >= 2 || !day) continue;
            float r = still ? 3.f + frand() * 3.f : 4.f + frand() * 6.f;
            begin(c, CR_LIZARD, a, r);
            c->idleT = 2.f + frand() * 6.f;
            c->tx = 0.f;                                       // next dart time (relative)
        } else if (kind == CR_BUTTERFLY) {                         // meadows on a fair day
            if (!day || count(CR_BUTTERFLY) >= 2 || (forestField(a, distance + 5.f) > 0.65f && biomeAt(distance).tropical < 0.5f) || biomeAt(distance).snow > 0.5f) continue;
            float r = 3.f + frand() * 6.f;
            if (lakeField(a, distance + r) > LAKE_T - 0.04f) continue;
            begin(c, CR_BUTTERFLY, a, r);
            c->h = 0.4f + frand() * 0.8f;
            c->idleT = 6.f + frand() * 10.f;
        } else if (kind == CR_DUCK || kind == CR_BEAVER || kind == CR_KOI) {   // on the water / on its edge / under it
            if (kind == CR_KOI && biomeAt(distance).japan < 0.5f) continue;
            if (kind == CR_BEAVER && biomeAt(distance).japan > 0.5f) continue;
            if (count(kind) >= (kind == CR_DUCK ? 2 : kind == CR_KOI ? 3 : 1) || (kind == CR_BEAVER && frand() < 0.6f)) continue;
            bool found = false;
            for (int tries = 0; tries < 12 && !found; tries++) {
                float aa = yaw + (frand() - 0.5f) * 1.0f, rr = (kind == CR_KOI ? 5.f : 8.f) + frand() * 14.f;
                float l = lakeField(aa, distance + rr);
                bool ok = kind != CR_BEAVER ? l > LAKE_T + 0.02f : (l > LAKE_T - 0.03f && l < LAKE_T - 0.005f);
                if (!ok) continue;
                begin(c, kind, aa, rr);
                if (kind == CR_BEAVER) {                           // the water is a little further along the same line
                    float rw = rr;
                    for (float z = rr; z < rr + 6.f; z += 0.5f) if (lakeField(aa, distance + z) > LAKE_T + 0.01f) { rw = z; break; }
                    c->tx = (rw + 0.6f) * cosf(aa); c->ty = (rw + 0.6f) * sinf(aa);
                }
                c->idleT = 6.f + frand() * 12.f;
                found = true;
            }
        } else if (kind == CR_SQUIRREL) {                          // at the foot of a tree
            if (count(CR_SQUIRREL) >= 1 || frand() > 0.2f + 0.5f * biomeAt(distance).squirrels) continue;
            int best = -1;
            for (int i = 0; i < FIELD_N; i++) {
                const FieldObj& o = s_obj[i];
                if ((o.type != OBJ_BROAD && o.type != OBJ_PINE) || o.wet) continue;
                float d = sqrtf(o.x * o.x + o.y * o.y);
                float rel = fabsf(wrapPi(atan2f(o.y, o.x) - yaw));
                if (d > 5.f && d < 14.f && rel < 0.45f) { best = i; break; }
            }
            if (best < 0) continue;
            const FieldObj& tr = s_obj[best];
            float d = sqrtf(tr.x * tr.x + tr.y * tr.y), ta = atan2f(tr.y, tr.x);
            float sa = ta + (frand() - 0.5f) * 0.25f, sr = d - 1.5f - frand() * 1.5f;
            begin(c, CR_SQUIRREL, sa, sr);
            c->tx = tr.x; c->ty = tr.y;
            c->idleT = still ? 6.f + frand() * 8.f : 1.5f + frand() * 2.f;
        }
    }

    // ----- behaviour -----
    for (int i = 0; i < CRIT_N; i++) {
        FieldCritter& c = s_cr[i];
        if (!c.on) continue;
        float e = t - c.t0;
        float d = sqrtf(c.x * c.x + c.y * c.y);
        float spookR = c.kind == CR_DUCK ? 9.f : c.kind == CR_BUTTERFLY ? 2.5f : c.kind == CR_BEAVER ? 8.f : c.kind == CR_LIZARD ? 3.5f : 7.f;
        bool spooked = speed > 0.05f && d < spookR && c.kind != CR_KOI;   // (a fawn also bolts with its mother)
        if (c.kind == CR_DEER && c.mom >= 0) {                     // a fawn does what its mother does
            const FieldCritter& m = s_cr[c.mom];
            if (!m.on || m.kind != CR_DEER) c.mom = -1;
            else {
                c.mood = (m.mood == DEER_PET || m.mood == DEER_SNIFF || m.mood == DEER_SHOW) ? (uint8_t)DEER_GRAZE : m.mood; c.moodT = m.moodT;
                if (m.fleeing && !c.fleeing) { spooked = true; c.dir = m.dir; c.awayFlee = m.awayFlee; }
                if (!c.fleeing) {                                   // keep beside her; under her belly when nursing
                    float tx = m.x, ty = m.y, md = sqrtf(m.x * m.x + m.y * m.y) + 1e-3f;
                    if (m.mood == DEER_NURSE) { tx += m.x / md * 0.35f; ty += m.y / md * 0.35f; }
                    else { tx += -m.y / md * 0.9f; ty += m.x / md * 0.9f; }
                    float dx = tx - c.x, dy = ty - c.y, dl = sqrtf(dx * dx + dy * dy);
                    if (dl > 0.15f) { float sp = fminf(dl, 0.9f * dt); c.x += dx / dl * sp; c.y += dy / dl * sp; }
                    c.faceRight = m.faceRight;
                }
            }
        }
        if (c.kind == CR_DEER && !c.fleeing && c.variant != 7 && c.mom < 0) {
            bool buck = c.sex == 1;
            bool hasFawn = false;
            for (int j = 0; j < CRIT_N; j++) if (s_cr[j].on && s_cr[j].mom == i) hasFawn = true;
            bool calm = stillFor > 4.f && speed < 0.05f;
            float comfort = buck ? 6.f : hasFawn ? 3.5f : 2.0f;           // how close she'll come: a lone doe, closest
            c.moodT -= dt;
            if (speed > 0.05f && d < 15.f) {                              // you're moving nearby
                if (c.mood != DEER_ALERT && c.mood != DEER_STOMP) {
                    c.mood = (!buck && d < 11.f && frand() < 0.55f) ? DEER_STOMP : DEER_ALERT;   // a doe stamps and bobs, tail up
                    c.moodT = 1.5f + frand() * 2.f;
                }
            } else if (c.mood == DEER_APPROACH) {
                if (!calm) { c.mood = DEER_ALERT; c.moodT = 2.f; }
                else if (d > comfort && d > 0.01f) {                         // unhurried steps toward you
                    c.x -= c.x / d * 0.45f * dt; c.y -= c.y / d * 0.45f * dt;
                    float side = -c.y * cosf(yaw) + c.x * sinf(yaw);
                    c.faceRight = side < 0.f;
                } else { c.mood = (!buck && !hasFawn) ? DEER_SNIFF : DEER_GRAZE; c.moodT = 4.f + frand() * 4.f; }
            } else if (calm && d > comfort + 0.6f && frand() < dt * (buck ? 0.08f : 0.35f)) {
                c.mood = DEER_APPROACH; c.moodT = 8.f;                    // you've been still: she comes closer
            } else if (c.mood == DEER_PET) {
                if (c.moodT <= 0.f) { c.mood = DEER_SNIFF; c.moodT = 3.f; }
            } else if (c.moodT <= 0.f) {
                float k = frand();
                if (!buck && calm && d < comfort + 1.5f) {                    // a doe at ease near you
                    c.mood = k < 0.3f ? DEER_SNIFF : k < 0.48f ? DEER_SHOW : k < 0.78f ? DEER_GRAZE : DEER_ALERT;
                    if (hasFawn && k > 0.85f) c.mood = DEER_NURSE;
                } else if (!buck && hasFawn && k < 0.15f) c.mood = DEER_NURSE;
                else if (!buck && k < 0.1f) c.mood = DEER_SHOW;
                else c.mood = k < 0.55f ? DEER_GRAZE : (k < 0.8f ? DEER_ALERT : DEER_WALK);
                c.moodT = c.mood == DEER_GRAZE ? 3.f + frand() * 5.f : c.mood == DEER_NURSE ? 5.f + frand() * 4.f : c.mood == DEER_SHOW ? 3.f + frand() * 2.f : 1.5f + frand() * 2.f;
                if (c.mood == DEER_WALK) c.faceRight = frand() < 0.5f;
            }
            if (c.mood == DEER_WALK && d > 0.01f) {                       // a few steps across your view
                float px = -c.y / d * (c.faceRight ? 1.f : -1.f), py = c.x / d * (c.faceRight ? 1.f : -1.f);
                c.x += px * 0.6f * dt; c.y += py * 0.6f * dt;
            }
            if (calm && !buck && d < 9.f) c.idleT = fmaxf(c.idleT, e + 6.f);   // while you're still, a doe near you stays
        }
        if (!c.fleeing && (spooked || e > c.idleT)) {
            c.fleeing = true;
            c.fleeT = 0.f;
            if (c.kind == CR_DEER && c.mom < 0) c.awayFlee = frand() < 0.5f;   // often they turn and run straight away
            if (e < c.idleT) c.idleT = e;
            if (c.kind != CR_BUTTERFLY && c.kind != CR_KOI && c.kind != CR_LIZARD && s_nBolts < CRIT_N) { s_bolts[s_nBolts].kind = (uint8_t)(c.kind | (c.variant << 4)); s_bolts[s_nBolts].dist = d; s_nBolts++; }
        }
        if (c.fleeing) c.fleeT += dt;
        float fe = c.fleeT;
        switch (c.kind) {
            case CR_BUTTERFLY: {                                     // a wandering, bobbing flight; off and up when disturbed
                float w = e * 2.3f + (c.seed & 255) * 0.1f;
                float sp = c.fleeing ? 1.6f + fe : 0.45f;
                c.x += cosf(w * 0.7f + c.dir) * sp * dt;
                c.y += sinf(w * 0.9f) * sp * dt;
                c.h = fmaxf(0.25f, c.h + (sinf(w * 3.1f) * 0.6f + (c.fleeing ? 0.9f : 0.f)) * dt);
            } break;
            case CR_DUCK:                                            // bob on the water; take off and climb away
                if (c.fleeing) {
                    float sp = 2.f + fe * 5.f;
                    c.x += c.x / (d + 1e-3f) * sp * dt + (-c.y / (d + 1e-3f)) * c.dir * sp * 0.5f * dt;
                    c.y += c.y / (d + 1e-3f) * sp * dt + (c.x / (d + 1e-3f)) * c.dir * sp * 0.5f * dt;
                    c.h += (0.8f + fe * 2.5f) * dt;
                }
                break;
            case CR_LIZARD: {                                        // dart, freeze, dart; gone in a blur when startled
                float sp = 0.f;
                if (c.fleeing) sp = 7.f;
                else if (fmodf(e + (c.seed & 15) * 0.3f, 2.4f) < 0.25f) sp = 3.5f;   // a quick dash now and then
                if (sp > 0.f) {
                    float px = -c.y / (d + 1e-3f) * c.dir, py = c.x / (d + 1e-3f) * c.dir;
                    c.x += px * sp * dt; c.y += py * sp * dt;
                    c.faceRight = c.dir > 0;
                }
            } break;
            case CR_KOI: {                                           // slow arcs just under the surface; they never flee
                float w = e * 0.35f + (c.seed & 255) * 0.05f;
                c.x += cosf(w) * 0.25f * dt; c.y += sinf(w) * 0.25f * dt;
                c.faceRight = cosf(w - yaw - 1.5708f) > 0.f;
                c.fleeing = false; c.idleT = e + 10.f;
                if (e > 60.f) c.on = false;
            } break;
            case CR_BEAVER:                                          // waddle to the water and plunge
                if (c.fleeing && c.splashT < 0.f) {
                    float dx = c.tx - c.x, dy = c.ty - c.y, dl = sqrtf(dx * dx + dy * dy);
                    if (dl < 0.3f) { c.splashT = t; if (s_nBolts < CRIT_N) { s_bolts[s_nBolts].kind = 0x80 | CR_BEAVER; s_bolts[s_nBolts].dist = d; s_nBolts++; } }
                    else { c.x += dx / dl * 2.2f * dt; c.y += dy / dl * 2.2f * dt; c.faceRight = (-dx * sinf(yaw) + dy * cosf(yaw)) > 0.f; }
                }
                break;
            case CR_SQUIRREL:                                        // dash to the tree and run up the trunk
                if (c.fleeing) {
                    float dx = c.tx - c.x, dy = c.ty - c.y, dl = sqrtf(dx * dx + dy * dy);
                    if (dl > 0.35f) { c.x += dx / dl * 4.5f * dt; c.y += dy / dl * 4.5f * dt; c.faceRight = (-dx * sinf(yaw) + dy * cosf(yaw)) > 0.f; }
                    else c.h += 3.2f * dt;
                }
                break;
            case CR_DEER:
                if (c.variant == 7) {                                    // the white stag: turns and walks slowly away
                    if (c.fleeing && d > 0.01f) { c.x += c.x / d * 0.9f * dt; c.y += c.y / d * 0.9f * dt; }
                    if (c.fleeing && c.fleeT > 6.f) c.on = false;
                    break;
                }
                // fall through
            default:                                                 // deer, rabbits, birds: skitter across the line of sight
                if (c.fleeing && d > 0.01f && c.kind == CR_DEER && c.awayFlee) {   // bounding straight away, rump and flag toward you
                    float sp = 2.2f + fe * 4.f;
                    c.x += c.x / d * sp * dt; c.y += c.y / d * sp * dt;
                } else if (c.fleeing && d > 0.01f) {
                    float sp = (c.kind == CR_BIRDS ? 1.5f : 2.2f) + fe * (c.kind == CR_DEER ? 4.f : 3.f);
                    float px = -c.y / d * c.dir, py = c.x / d * c.dir;
                    c.x += (px * 0.9f + c.x / d * 0.35f) * sp * dt;
                    c.y += (py * 0.9f + c.y / d * 0.35f) * sp * dt;
                }
                break;
        }
        float rel = wrapPi(atan2f(c.y, c.x) - yaw);
        bool gone = d < 1.2f || d > FIELD_R || e > c.idleT + 14.f
                 || (c.fleeing && (c.kind <= CR_BIRDS || c.kind == CR_LIZARD) && fabsf(rel) > 0.95f)
                 || (c.kind == CR_DUCK && c.h > 12.f) || (c.kind == CR_SQUIRREL && c.h > 5.f)
                 || (c.kind == CR_BUTTERFLY && (c.h > 6.f || d > 16.f))
                 || (c.kind == CR_BEAVER && c.splashT > 0.f && t - c.splashT > 1.2f);
        if (gone) c.on = false;
    }
    return s_nBolts;
}

void fieldBolt(int i, uint8_t& kind, float& dist) { kind = s_bolts[i].kind; dist = s_bolts[i].dist; }

const FieldObj*     fieldObjects() { return s_obj; }
bool fieldTryPet() {
    for (int i = 0; i < CRIT_N; i++) {
        FieldCritter& c = s_cr[i];
        if (!c.on || c.kind != CR_DEER || c.fleeing || c.sex == 1 || c.variant == 7) continue;
        float d = sqrtf(c.x * c.x + c.y * c.y);
        float rel = wrapPi(atan2f(c.y, c.x) - s_petYaw);
        if (d < 3.2f && fabsf(rel) < 0.4f) { c.mood = DEER_PET; c.moodT = 3.f; c.idleT += 8.f; return true; }
    }
    return false;
}
const FieldCritter* fieldCritters() { return s_cr; }
const Firefly*      fieldFireflies() { return s_ff; }
float fieldNearestFalls() {
    float best = 999.f;
    for (int i = 0; i < FIELD_N; i++) {
        const FieldObj& o = s_obj[i];
        if (o.type != OBJ_OUTCROP || !(o.var & 1)) continue;
        float d = sqrtf(o.x * o.x + o.y * o.y);
        if (d < best) best = d;
    }
    return best;
}

}
