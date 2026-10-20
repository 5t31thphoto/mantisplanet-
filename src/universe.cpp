#include "universe.h"
#include "journey.h"
#include <math.h>

namespace mantis {

static uint32_t s_seed = 0x5EE49A3Fu;

void universeInit(uint32_t seed) { s_seed = seed ? hash32(seed) : 0x5EE49A3Fu; }
uint32_t worldSeed() { return s_seed; }

static inline float lattice(int ix, int iy, int P, uint32_t salt) {
    ix %= P;
    if (ix < 0) ix += P;
    uint32_t h = hash32((uint32_t)ix * 0x9E3779B1u ^ hash32((uint32_t)iy * 0x85EBCA77u ^ s_seed ^ salt));
    return (h >> 8) * (1.0f / 16777216.0f);
}

float vnoiseP(float u, float v, int P, uint32_t salt) {
    float fu = floorf(u), fv = floorf(v);
    int iu = (int)fu, iv = (int)fv;
    float tu = u - fu, tv = v - fv;
    tu = tu * tu * (3.f - 2.f * tu);
    tv = tv * tv * (3.f - 2.f * tv);
    float a = lattice(iu, iv, P, salt), b = lattice(iu + 1, iv, P, salt);
    float c = lattice(iu, iv + 1, P, salt), d = lattice(iu + 1, iv + 1, P, salt);
    float ab = a + (b - a) * tu, cd = c + (d - c) * tu;
    return ab + (cd - ab) * tv;
}

float fbmP(float u, float v, int P, int oct, uint32_t salt) {
    float sum = 0.f, amp = 0.5f, norm = 0.f;
    for (int i = 0; i < oct; i++) {
        sum += amp * vnoiseP(u, v, P, salt + (uint32_t)i * 0x632BE5ABu);
        norm += amp;
        u *= 2.f; v *= 2.f; P *= 2; amp *= 0.5f;
    }
    return sum / norm;
}

static float s_dayOffset = 0.30f;      // the dream begins in early morning light
float dayClock(float t, float distance) { return s_dayOffset + t * (1.f / 600.f) + distance * 0.0004f + 10.f; }
void setTimeOfDay(float tod, float t, float distance) {
    float c = dayClock(t, distance);
    s_dayOffset += tod - (c - floorf(c));
}

float oceanShore(float a, float D) {
    const BiomeParams& b = biomeAt(D);
    if (b.ocean < 0.5f) return 1e9f;
    float dw = fabsf(a - 3.14159265f - kTau * floorf((a - 3.14159265f + 3.14159265f) / kTau));   // angle away from due west
    if (dw > 1.25f) return 1e9f;                                       // north and south you walk along the shore
    float wob = (fbmP(angU(a, 8), 0.f, 8, 2, 0xC0A5u) - 0.5f) * 30.f;
    return biomeStart(D) + 260.f + dw * dw * 110.f + wob;
}

bool paddyAt(float a, float D) {
    if (biomeAt(D).paddies < 0.5f) return false;
    float terrace = D * 0.075f + fbmP(angU(a, 4), D * 0.004f, 4, 1, 0x7E44u) * 1.5f;   // terraces step across the land
    if (terrace - floorf(terrace) > 0.6f) return false;                               // the dike between two terraces: wide enough to walk
    return fbmP(angU(a, 6), D * 0.01f, 6, 2, 0x9A1Du) > 0.56f;                         // not every terrace is flooded
}

float lakeField(float a, float D) {
    float sh = oceanShore(a, D);
    if (sh < 1e8f) return D > sh ? LAKE_T + 0.3f : LAKE_T - 0.4f;     // the sea, or the dry land before it
    const BiomeParams& b = biomeAt(D);
    float sc = b.pondScale > 1.f ? b.pondScale : 1.f;                   // gardens: small, frequent ponds
    int P = (int)(6.f * sc + 0.5f);
    float l = fbmP(angU(a, P), D * 0.03f * sc, P, 3, 0x1A4Eu);
    return l - (1.f - b.lakes) * 0.2f;                                  // lake country has lakes; elsewhere ponds are rare
}

float forestField(float a, float D) {
    float f = fbmP(angU(a, 5), D * 0.02f, 5, 2, 0xF0E5u);
    const BiomeParams& b = biomeAt(D);
    f = b.forest + (f - 0.5f) * b.forestVar * 3.f;                      // the biome sets the cover; noise makes it patchy
    return f < 0.f ? 0.f : (f > 1.f ? 1.f : f);
}

float cloudCover(float D) {
    float c = fbmP(0.f, D * 0.004f, 1, 2, 0xC10Du);
    c = (c - 0.3f) * 2.5f;
    return c < 0.f ? 0.f : (c > 1.f ? 1.f : c);
}

float streamField(float a, float D) {
    if (lakeField(a, D) > LAKE_T - 0.02f) return 1.f;                 // lakes take priority
    float where = fbmP(0.f, D * 0.0035f, 1, 2, 0x5C1u);                 // some stretches have streams
    if (where < 0.75f - 0.5f * biomeAt(D).creeks) return 1.f;
    float n = fbmP(angU(a, 3), D * 0.011f, 3, 2, 0x57E4u);              // the 0.5 contour winds across the land
    float w = fabsf(n - 0.5f) / 0.009f;                                 // a creek, not a river
    return w < 1.f ? w : 1.f;
}

BiomeSample sampleWorld(float angle, float D) {
    BiomeSample b;
    b.lake = lakeField(angle, D);
    b.forest = forestField(angle, D);
    b.isWater = b.lake > LAKE_T;
    b.isSolid = b.isWater;
    return b;
}

bool shouldFootstep(float distance, float speed) {
    if (speed < 0.08f) return false;
    static float last = -100.f;
    float stepDist = 1.7f / (0.4f + speed);
    if (distance - last > stepDist) {
        last = distance;
        return true;
    }
    return false;
}

}
