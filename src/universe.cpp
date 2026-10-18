#include "universe.h"
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

float lakeField(float a, float D)   { return fbmP(angU(a, 6), D * 0.03f, 6, 3, 0x1A4Eu); }

float forestField(float a, float D) {
    float f = fbmP(angU(a, 5), D * 0.02f, 5, 2, 0xF0E5u);
    f = (f - 0.32f) * 2.8f;
    return f < 0.f ? 0.f : (f > 1.f ? 1.f : f);
}

float cloudCover(float D) {
    float c = fbmP(0.f, D * 0.004f, 1, 2, 0xC10Du);
    c = (c - 0.3f) * 2.5f;
    return c < 0.f ? 0.f : (c > 1.f ? 1.f : c);
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
