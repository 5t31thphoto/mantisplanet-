#include "bugs.h"
#include "render.h"
#include "journey.h"
#include "universe.h"
#include "audio.h"
#include "haptics.h"
#include <math.h>
#include <stdio.h>
#ifndef HOST
#include <Preferences.h>
#endif

namespace mantis {

static Bug s_b[BUG_N];
static int s_count[BAIT_KINDS];
static int s_sel = -1;
static uint32_t s_found = 0;                    // one bit per kind ever caught
static float s_next = 6.f, s_t = 0.f;
static uint32_t s_rng = 0xB0C5u;
static float frand() { s_rng = s_rng * 1664525u + 1013904223u; return (s_rng >> 8) * (1.f / 16777216.f); }

static const char* NAMES[BAIT_KINDS] = {"Worm", "Cricket", "Grasshopper", "Beetle", "Mayfly", "Firefly",
                                        "Jewel Beetle", "Banana Slug", "Sand Crab", "Ice Worm", "Caterpillar", "Silk Moth", "Desert Beetle", "Moon Moth"};
static const uint8_t SPECIAL[BAIT_KINDS] = {255, 255, 255, 255, 255, 255, B_JAPAN, B_REDWOODS, B_COAST, B_TUNDRA, B_RAINFOREST, B_KARST, B_CANYON, B_GROVE};
const char* baitName(uint8_t k) { return k < BAIT_KINDS ? NAMES[k] : ""; }
uint8_t baitSpecialBiome(uint8_t k) { return k < BAIT_KINDS ? SPECIAL[k] : 255; }
int baitCount(uint8_t k) { return k < BAIT_KINDS ? s_count[k] : 0; }
int baitSelected() { return s_sel; }

static void save() {
#ifndef HOST
    Preferences p;
    if (p.begin("mantisbait", false)) { for (int k = 0; k < BAIT_KINDS; k++) { char key[8]; snprintf(key, sizeof key, "b%d", k); p.putUChar(key, (uint8_t)(s_count[k] > 255 ? 255 : s_count[k])); } p.putChar("sel", (int8_t)s_sel); p.putUInt("found", s_found); p.end(); }
#endif
}
void bugsInit() {
    for (int i = 0; i < BUG_N; i++) s_b[i].on = false;
    for (int k = 0; k < BAIT_KINDS; k++) s_count[k] = 0;
    s_sel = -1;
    s_found = 0;
#ifndef HOST
    Preferences p;
    if (p.begin("mantisbait", true)) { for (int k = 0; k < BAIT_KINDS; k++) { char key[8]; snprintf(key, sizeof key, "b%d", k); s_count[k] = p.getUChar(key, 0); } s_sel = p.getChar("sel", -1); s_found = p.getUInt("found", 0); p.end(); }
#endif
    if (s_sel >= 0 && (s_sel >= BAIT_KINDS || s_count[s_sel] <= 0)) s_sel = -1;
}

void baitSelect(int k) {
    if (k >= 0 && (k >= BAIT_KINDS || s_count[k] <= 0)) return;
    s_sel = k;
    save();
}
bool baitFound(uint8_t k) { return k < BAIT_KINDS && ((s_found >> k) & 1u); }
void baitAdd(uint8_t k, int n) { if (k < BAIT_KINDS) { s_count[k] += n; if (s_count[k] > 255) s_count[k] = 255; if (n > 0) s_found |= 1u << k; } }

void baitCycle() {                                          // none -> each bait you have -> none
    int k = s_sel;
    for (int step = 0; step <= BAIT_KINDS; step++) {
        k++;
        if (k >= BAIT_KINDS) k = -1;
        if (k < 0 || s_count[k] > 0) { s_sel = k; break; }
    }
    save();
}
bool baitConsume() {
    if (s_sel < 0 || s_count[s_sel] <= 0) return false;
    s_count[s_sel]--;
    if (s_count[s_sel] <= 0) s_sel = -1;
    save();
    return true;
}

const Bug* bugs() { return s_b; }
void bugsNoteScreen(int i, int sx, int sy) { if (i >= 0 && i < BUG_N) { s_b[i].sx = sx; s_b[i].sy = sy; } }
void bugsShift(float sx, float sy) { for (Bug& b : s_b) if (b.on) { b.x -= sx; b.y -= sy; } }

// what lives here, now
static int pickKind(const Scene& sc, float distance, float yaw) {
    const BiomeParams& bp = biomeAt(distance);
    bool night = sc.tod > 0.8f || sc.tod < 0.2f;
    bool nearWater = sc.lakeNear > 0.2f || sc.streamNear > 0.2f || sc.oceanNear > 0.2f;
    uint8_t cand[8]; int n = 0;
    auto add = [&](uint8_t k) { if (n < 8) cand[n++] = k; };
    uint8_t here = journeyBiome();
    for (int k = BAIT_JEWEL; k < BAIT_KINDS; k++) if (SPECIAL[k] == here && (k != BAIT_MOONMOTH || night)) { add((uint8_t)k); add((uint8_t)k); }
    if (bp.desert > 0.5f) { if (!n) add(BAIT_DESERTBEETLE); return n ? cand[(int)(frand() * n)] : -1; }
    if (bp.snow > 0.5f) { if (!n) add(BAIT_ICEWORM); return cand[(int)(frand() * n)]; }
    if (sc.wet > 0.4f || sc.rain > 0.2f) { add(BAIT_WORM); add(BAIT_WORM); }
    if (night && renderFlashlight() && sc.rain > 0.03f && sc.rain < 0.6f) { add(BAIT_WORM); add(BAIT_WORM); }   // a light rain by torchlight: worms up everywhere
    if (night) add(BAIT_FIREFLY);
    if (night && renderFlashlight()) add(BAIT_FIREFLY);                    // drawn to the light
    else { if (sc.forestNear < 0.5f) { add(BAIT_HOPPER); add(BAIT_CRICKET); } else add(BAIT_BEETLE); }
    if (nearWater && !night) add(BAIT_MAYFLY);
    if (sc.forestNear >= 0.5f) add(BAIT_BEETLE);
    if (!n) add(BAIT_CRICKET);
    (void)yaw;
    return cand[(int)(frand() * n)];
}
static bool flies(uint8_t k) { return k == BAIT_MAYFLY || k == BAIT_FIREFLY || k == BAIT_SILKMOTH || k == BAIT_MOONMOTH; }

void bugsUpdate(float dt, float yaw, float distance, float speed, const Scene& sc) {
    s_t += dt;
    int live = 0; for (Bug& b : s_b) if (b.on) live++;
    // The flashlight at night draws bugs in: more of them, sooner, into the beam (the usual rate isn't touched;
    // this is on top of it), and more again in a light rain.
    bool night = sc.tod > 0.8f || sc.tod < 0.2f;
    bool lit = night && renderFlashlight();
    float boost = lit ? (3.f + ((sc.rain > 0.03f && sc.rain < 0.6f) ? 2.f : 0.f)) : 1.f;
    int cap = lit ? 4 : 2;
    if (s_t > s_next && live < cap && speed < 0.6f) {
        s_next = s_t + (12.f + frand() * 18.f) / boost;
        int k = pickKind(sc, distance, yaw);
        for (Bug& b : s_b) if (!b.on && k >= 0) {
            float a = yaw + (frand() - 0.5f) * (lit ? 0.45f : 0.9f), r = lit ? 2.0f + frand() * 2.5f : 2.2f + frand() * 3.5f;
            if (lakeField(a, distance + r) > LAKE_T - 0.03f && k != BAIT_MAYFLY && k != BAIT_CRAB) break;   // not in the water
            b = Bug(); b.on = true; b.kind = (uint8_t)k; b.x = r * cosf(a); b.y = r * sinf(a);
            b.h = flies((uint8_t)k) ? 0.5f + frand() * 0.8f : 0.f; b.t0 = s_t; b.life = 25.f + frand() * 15.f; b.dir = frand() < 0.5f ? -1 : 1; b.sx = b.sy = -100;
            break;
        }
    }
    for (Bug& b : s_b) {
        if (!b.on) continue;
        float e = s_t - b.t0, d = sqrtf(b.x * b.x + b.y * b.y);
        if (b.caught) { if (s_t - b.caughtT > 0.6f) b.on = false; continue; }
        float px = -b.y / (d + 1e-3f) * b.dir, py = b.x / (d + 1e-3f) * b.dir;
        switch (b.kind) {
            case BAIT_CRICKET: case BAIT_HOPPER: {                       // sits still, then a hop (sooner if you come close)
                b.hopT -= dt;
                if (b.hopT <= 0.f) { b.hopT = (speed > 0.1f && d < 2.5f) ? 0.5f : 2.f + frand() * 4.f; b.hopLeft = 0.3f; b.dir = frand() < 0.5f ? -1 : 1; }
                if (b.hopLeft > 0.f) { b.hopLeft -= dt; b.x += px * 1.6f * dt; b.y += py * 1.6f * dt; b.h = 0.2f * sinf((0.3f - b.hopLeft) / 0.3f * 3.14159f); }
                else b.h = 0.f;
            } break;
            case BAIT_CRAB: b.x += px * 0.6f * dt * (sinf(e * 1.3f) > 0.f ? 1.f : -1.f); b.y += py * 0.6f * dt * (sinf(e * 1.3f) > 0.f ? 1.f : -1.f); break;   // sideways
            case BAIT_MAYFLY: case BAIT_FIREFLY: case BAIT_SILKMOTH: case BAIT_MOONMOTH:
                b.x += cosf(e * 1.7f + b.dir) * 0.35f * dt; b.y += sinf(e * 1.3f) * 0.35f * dt; b.h = fmaxf(0.3f, b.h + sinf(e * 3.f) * 0.2f * dt);
                if (lit) {                                                  // fluttering into the beam, about 3 m out
                    float tx = cosf(yaw) * 3.f, ty = sinf(yaw) * 3.f;
                    b.x += (tx - b.x) * 0.25f * dt; b.y += (ty - b.y) * 0.25f * dt;
                    b.h += (0.9f - b.h) * 0.3f * dt;
                }
                break;
            default: b.x += px * 0.08f * dt; b.y += py * 0.08f * dt; break;   // crawlers
        }
        if (e > b.life || d > 9.f || d < 0.6f) b.on = false;
    }
}

bool bugsTryCatch(int x, int y) {
    for (Bug& b : s_b) {
        if (!b.on || b.caught) continue;
        int dx = x - b.sx, dy = y - b.sy;
        if (dx * dx + dy * dy <= 18 * 18) {
            b.caught = true; b.caughtT = s_t;
            if (s_count[b.kind] < 255) s_count[b.kind]++;
            s_found |= 1u << b.kind;                         // a new kind for the collection
            if (s_sel < 0) s_sel = b.kind;                      // your first bait goes straight on the hook
            save();
            sfxFish(FISH_BUG, 0.f);
            hx::fishTick(0.6f);
            return true;
        }
    }
    return false;
}

}
