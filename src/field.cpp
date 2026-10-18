#include "field.h"
#include "universe.h"
#include <math.h>

namespace mantis {

static FieldObj s_obj[FIELD_N];
static FieldCritter s_crit = {false, 0, 0, 0, 0, 0, 1, true, false};
static Firefly s_ff[FIREFLY_N];
static float s_nextSpawn = 3.f;
static uint32_t s_rng = 0x9E3779B9u;

static float frand() { s_rng = hash32(s_rng + 0x6D2B79F5u); return (s_rng >> 8) * (1.f / 16777216.f); }
static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline float wrapPi(float a) { return a - kTau * floorf((a + 3.14159265f) / kTau); }

static float rockField(float a, float D);
float rockFieldAt(float a, float D) { return rockField(a, D); }
static float rockField(float a, float D) {
    float r = fbmP(angU(a, 5), D * 0.015f, 5, 2, 0x50C4u);
    return clampf((r - 0.38f) * 3.f, 0.f, 1.f);
}

static void spawn(FieldObj& o, float angle, float range, float distance) {
    o.x = range * cosf(angle);
    o.y = range * sinf(angle);
    o.seed = hash32(s_rng ^ (uint32_t)(range * 977.f) ^ (uint32_t)(angle * 7919.f));
    o.type = OBJ_NONE;
    o.solid = false;
    o.radius = 0.f;
    o.a = o.b = 0.f;
    const float D = distance + range;
    if (lakeField(angle, D) > LAKE_T - 0.03f) return;     // nothing stands in the water
    const float forest = forestField(angle, D);
    const float rock = rockField(angle, D);
    const float p = frand();
    const float treeP = 0.07f + forest * 0.62f;               // a real forest: find your way through it
    const float stoneP = 0.015f + rock * 0.11f;
    if (p < treeP) {
        if (frand() < 0.25f + 0.4f * forest) {
            o.type = OBJ_PINE;
            o.a = 4.f + frand() * 4.5f;                 // height
            o.radius = 0.28f;
        } else {
            o.type = OBJ_BROAD;
            o.a = 4.5f + frand() * 3.5f;                // height
            o.b = 1.3f + frand() * 1.2f;                // crown radius
            o.radius = 0.22f + o.b * 0.06f;             // trunk
        }
        o.solid = true;
    } else if (p < treeP + stoneP) {
        if (rock > 0.55f && frand() < 0.3f) {
            o.type = OBJ_OUTCROP;
            o.a = 1.8f + frand() * 2.4f;                // half width
            o.b = 2.2f + frand() * 3.8f;                // height
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
        o.type = OBJ_FLOWERS;
    } else if (p < treeP + stoneP + 0.40f) {
        o.type = OBJ_TUFT;
    }
}

void fieldInit(float yaw, float distance) {
    s_rng = hash32(worldSeed() ^ 0xF1E1Du);
    for (int i = 0; i < FIELD_N; i++) {
        float ang = frand() * kTau;
        float rng = FIELD_R * sqrtf(frand());
        if (rng < 3.f) rng = 3.f + frand() * 4.f;
        spawn(s_obj[i], ang, rng, distance);
        // don't start with something standing on top of you or right in your face
        FieldObj& o = s_obj[i];
        if (o.solid) {
            float d = sqrtf(o.x * o.x + o.y * o.y);
            float rel = fabsf(wrapPi(atan2f(o.y, o.x) - yaw));
            if (d < o.radius + 2.f || (rel < 0.5f && d < 9.f)) o.type = OBJ_NONE, o.solid = false;
        }
    }
    s_crit.on = false;
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
            if (lakeField(yaw + k * 0.07f, distance + ds + 0.3f + (LAKE_STOP - 0.3f) * j / 4.f) > LAKE_T) { *bumped = true; return 0.f; }

    const float hx = cosf(yaw), hy = sinf(yaw);
    float sx = hx * ds, sy = hy * ds;

    // Something solid in the way: head-on you stop dead (turn and find another way); glancing off
    // the side of it you slide past. Nothing ever steers you.
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < FIELD_N; i++) {
            const FieldObj& o = s_obj[i];
            if (!o.solid) continue;
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
        if (!o.solid) continue;
        float minD = o.radius + BODY_R;
        float dx = o.x - sx, dy = o.y - sy;
        float before = o.x * o.x + o.y * o.y;
        float after = dx * dx + dy * dy;
        if (after < minD * minD && after < before) { sx = sy = 0.f; *bumped = true; break; }
    }

    float moved = sqrtf(sx * sx + sy * sy);
    if (moved <= 0.f) return 0.f;
    for (int i = 0; i < FIELD_N; i++) { s_obj[i].x -= sx; s_obj[i].y -= sy; }
    if (s_crit.on) { s_crit.x -= sx; s_crit.y -= sy; }
    for (int i = 0; i < FIREFLY_N; i++) { s_ff[i].x -= sx; s_ff[i].y -= sy; }

    // forget what fell out behind, dream it again at the far edge ahead
    const float D = distance + moved;
    for (int i = 0; i < FIELD_N; i++) {
        FieldObj& o = s_obj[i];
        if (o.x * o.x + o.y * o.y <= FIELD_R * FIELD_R) continue;
        float ang = yaw + asinf(2.f * frand() - 1.f);   // cos-weighted across the front half
        spawn(o, ang, FIELD_R * (0.94f + 0.06f * frand()), D);
    }
    return moved;
}

bool fieldTick(float yaw, float distance, float t, float dt, float speed, float stillFor) {
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

    FieldCritter& c = s_crit;
    bool still = stillFor > 8.f;
    if (!c.on && t > s_nextSpawn) {
        float a = yaw + (frand() - 0.5f) * 0.7f;
        float r = still ? 5.5f + frand() * 4.f : 9.f + frand() * 7.f;   // be still and they come closer
        if (lakeField(a, distance + r) > LAKE_T - 0.04f) { s_nextSpawn = t + 1.f; return false; }
        float k = frand();
        c.kind = k < 0.45f ? 0 : (k < 0.8f ? 1 : 2);
        c.x = r * cosf(a);
        c.y = r * sinf(a);
        c.t0 = t;
        c.idleT = still ? 10.f + frand() * 14.f : 1.8f + frand() * 2.5f;
        c.dir = frand() < 0.5f ? -1 : 1;
        c.faceRight = frand() < 0.5f;
        c.fleeing = false;
        c.on = true;
    }
    if (!c.on) return false;
    float e = t - c.t0;
    float d = sqrtf(c.x * c.x + c.y * c.y);
    bool spooked = speed > 0.05f && d < 7.f;                // walking up on them
    if (!c.fleeing && (spooked || e > c.idleT)) {
        if (e < c.idleT) c.idleT = e;
        c.fleeing = true;
        return true;
    }
    if (c.fleeing && d > 0.01f) {
        // skitter across the line of sight, a little away from you, accelerating
        float fe = e - c.idleT;
        float sp = (c.kind == 2 ? 1.5f : 2.2f) + fe * (c.kind == 0 ? 4.f : 3.f);
        float px = -c.y / d * c.dir, py = c.x / d * c.dir;
        c.x += (px * 0.9f + c.x / d * 0.35f) * sp * dt;
        c.y += (py * 0.9f + c.y / d * 0.35f) * sp * dt;
    }
    float rel = wrapPi(atan2f(c.y, c.x) - yaw);
    if (d < 1.5f || d > FIELD_R || (c.fleeing && fabsf(rel) > 0.95f) || e > c.idleT + 12.f) {
        c.on = false;
        s_nextSpawn = t + (still ? 2.f + frand() * 4.f : 4.f + frand() * 9.f);
    }
    return false;
}

const FieldObj*     fieldObjects() { return s_obj; }
const FieldCritter& fieldCritter() { return s_crit; }
const Firefly*      fieldFireflies() { return s_ff; }

}
