#include "scene.h"
#include "universe.h"
#include "audio.h"
#include "field.h"
#include "journey.h"
#include <math.h>

namespace mantis {

Scene g_scene;
WeatherForce g_force;
Thunder g_thunder = {0, 0, 0, false};

float sunElevation(float tod);   // render.cpp (shares the sky keyframes)
float rockFieldAt(float a, float D);   // field.cpp

static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline float smoothstep(float a, float b, float x) { float t = clampf((x - a) / (b - a), 0.f, 1.f); return t * t * (3.f - 2.f * t); }
static inline float follow(float cur, float target, float dt, float tau) { return cur + (target - cur) * clampf(dt / tau, 0.f, 1.f); }
static inline float bump(float x, float a, float b, float c, float d) { return smoothstep(a, b, x) * (1.f - smoothstep(c, d, x)); }
static uint32_t s_rng = 0xA511E9B3u;
static float frand() { s_rng = hash32(s_rng + 0x9E3779B9u); return (s_rng >> 8) * (1.f / 16777216.f); }

void sceneInit() {
    Scene& s = g_scene;
    s = Scene();
    s.cloud = 0.3f; s.wind = 0.25f; s.rainAgo = 999.f; s.boltAge = 99.f; s.intro = 1.f;
    s_rng = hash32(worldSeed() ^ 0x5CE7Eu);
}

void sceneUpdate(float t, float dt, float yaw, float distance, float speed, bool tryingToWalk) {
    Scene& s = g_scene;
    s.t = t; s.dt = dt; s.yaw = yaw; s.distance = distance; s.speed = speed;
    float clock = dayClock(t, distance);
    s.tod = clock - floorf(clock);
    s.sunElev = sunElevation(s.tod);
    s.sunAz = AZ_EAST + (s.tod - 0.245f) * kTau;              // east at sunrise, through the south at noon, west at sunset; left to right as you face it
    const BiomeParams& here = biomeAt(distance);
    s.daySeed = hash32((uint32_t)floorf(clock) ^ worldSeed());

    // ---------- you ----------
    // walking momentum survives pausing to pick a way round a tree; it fades only once you're still
    if (speed > 0.05f) { s.walkFor = fminf(s.walkFor + dt, 90.f); s.stillFor = 0.f; }
    else {
        if (!tryingToWalk) s.stillFor += dt; else s.stillFor = 0.f;
        s.walkFor = fmaxf(0.f, s.walkFor - dt * (s.stillFor > 4.f ? 2.5f : 0.25f));
    }

    // ---------- weather: a slow field over distance walked and time ----------
    float u = distance * 0.0016f + t * 0.0011f;
    float v = clampf((fbmP(0.f, u, 1, 3, 0x7EA7u) - 0.28f) / 0.46f + here.wet, 0.f, 1.f);
    float cloudT = clampf(0.12f + v * 1.05f, 0.f, 1.f);
    if (t < 300.f) cloudT = fminf(cloudT, 0.45f + 0.55f * smoothstep(150.f, 300.f, t));
    float settle = smoothstep(150.f, 300.f, t);             // the dream always begins in fair weather
    float rainT = smoothstep(0.62f, 0.78f, v) * settle;
    float stormT = smoothstep(0.84f, 0.97f, v) * settle;
    float windT = 0.16f + 0.34f * fbmP(0.f, u * 3.f + 5.f, 1, 2, 0x3C1Du) + rainT * 0.2f + stormT * 0.5f + here.wind;
    float dawn = bump(s.tod, 0.17f, 0.24f, 0.33f, 0.42f);
    float humid = fbmP(0.f, u * 2.f + 11.f, 1, 2, 0x6F06u);
    float fogT = dawn * (0.35f + 0.9f * humid) + rainT * 0.25f + stormT * 0.15f + here.mist;
    if (g_force.cloud >= 0) cloudT = g_force.cloud;
    if (g_force.rain >= 0) rainT = g_force.rain;
    if (g_force.storm >= 0) stormT = g_force.storm;
    if (g_force.wind >= 0) windT = g_force.wind;
    if (g_force.fog >= 0) fogT = g_force.fog;
    s.cloud = follow(s.cloud, cloudT, dt, 12.f);
    s.rain = follow(s.rain, rainT, dt, 8.f);
    s.storm = follow(s.storm, stormT, dt, 10.f);
    s.wind = follow(s.wind, clampf(windT, 0.f, 1.f), dt, 6.f);
    s.fog = follow(s.fog, clampf(fogT, 0.f, 1.f), dt, 15.f);
    s.wet = s.rain > s.wet ? follow(s.wet, s.rain, dt, 25.f) : follow(s.wet, 0.f, dt, 120.f);
    if (g_force.wet >= 0) s.wet = g_force.wet;
    s.rainAgo = s.rain > 0.3f ? 0.f : s.rainAgo + dt;
    if (g_force.cloud >= 0) s.cloud = g_force.cloud;
    if (g_force.rain >= 0) s.rain = g_force.rain;
    if (g_force.storm >= 0) s.storm = g_force.storm;
    if (g_force.fog >= 0) s.fog = g_force.fog;
    float g = fbmP(0.f, t * 0.35f, 1, 3, 0x6057u);
    g = clampf((g - 0.35f) * 2.2f, 0.f, 1.f);
    s.gust = s.wind * (0.45f + 0.55f * g * g);

    // ---------- lightning ----------
    s.boltAge += dt;
    if (s.storm > 0.25f && frand() < s.storm * 0.2f * dt) {
        s.boltSeed = hash32(s_rng ^ (uint32_t)(t * 1000.f));
        s.boltAz = yaw + (frand() - 0.5f) * 2.6f;
        s.boltAge = 0.f;
        float close = frand();
        close *= close;
        float delay = 0.25f + (1.f - close) * 4.5f;
        float power = 0.45f + 0.55f * s.storm;
        g_thunder = {t + delay, power, close, true};
        sfxThunder(delay, power, close);
    }
    {
        float e = s.boltAge;
        float f = e < 0.05f ? 1.f : e < 0.1f ? 0.25f : e < 0.16f ? 0.85f : expf(-(e - 0.16f) * 10.f);
        s.flash = e < 2.f ? f : 0.f;
    }

    // ---------- around you ----------
    float fsum = 0.f, lake = 0.f;
    for (int k = -2; k <= 2; k++) fsum += forestField(yaw + k * 0.6f, distance + 2.f);
    s.forestNear = follow(s.forestNear, fsum / 5.f, dt, 1.5f);
    for (int k = -3; k <= 3; k++)
        for (int j = 0; j < 3; j++) {
            float z = j == 0 ? 2.f : (j == 1 ? 5.f : 10.f);
            float l = clampf((lakeField(yaw + k * 0.45f, distance + z) - (LAKE_T - 0.12f)) / 0.12f, 0.f, 1.f) * (1.f - z / 14.f);
            if (l > lake) lake = l;
        }
    s.lakeNear = follow(s.lakeNear, lake, dt, 1.f);
    s.rockNear = follow(s.rockNear, rockFieldAt(yaw, distance + 2.f), dt, 1.5f);
    s.open = 1.f - s.forestNear;
    float sn = 0.f;
    for (int k = -2; k <= 2; k++) for (float z = 0.5f; z < 9.f; z += 2.f) { float f = 1.f - streamField(yaw + k * 0.5f, distance + z); if (f > 0.f) sn = fmaxf(sn, 1.f - z / 10.f); }
    s.streamNear = follow(s.streamNear, sn, dt, 1.f);
    float fd = fieldNearestFalls();
    s.fallsNear = follow(s.fallsNear, fd < 60.f ? 1.f / (1.f + fd / 7.f) : 0.f, dt, 1.f);
    bool wading = inStream(yaw, distance + 0.4f) || paddyAt(yaw, distance + 0.4f);
    bool onIce = biomeAt(distance).snow > 0.5f && lakeField(yaw, distance + 0.4f) > LAKE_T;
    bool shore = lakeField(yaw, distance + 2.5f) > LAKE_T - 0.05f;
    s.snowy = here.snow; s.frogBoost = here.frogs; s.cuckoo = here.cuckoo; s.crows = here.crows; s.japan = here.japan; s.desert = here.desert; s.hush = here.hush; s.giant = here.giant; s.karst = here.karst; s.tundra = here.tundra; s.tropical = here.tropical;
    float near = 1e9f;
    if (here.ocean > 0.5f) for (int k = -3; k <= 3; k++) { float sh = oceanShore(yaw + k * 0.4f, distance); if (sh < 1e8f) near = fminf(near, fmaxf(0.f, sh - distance)); }
    s.oceanNear = follow(s.oceanNear, near < 1e8f ? clampf(1.f - near / 140.f, 0.f, 1.f) : 0.f, dt, 1.f);
    static float lastTod = 0.f;                                  // the temple bell rings once as the sun goes down
    if (s.japan > 0.5f && lastTod < 0.715f && s.tod >= 0.715f) sfxMoment(MOMENT_TEMPLE);
    lastTod = s.tod;
    s.surface = onIce ? SURF_ICE : wading ? SURF_SPLASH : here.snow > 0.5f ? SURF_SNOW : (here.desert > 0.5f || s.oceanNear > 0.75f) ? SURF_SAND : shore ? SURF_MUD : s.rockNear > 0.65f ? SURF_STONE : s.forestNear > 0.5f ? SURF_FOREST : s.wet > 0.45f ? SURF_WET : SURF_GRASS;

    // ---------- moments ----------
    uint32_t ds = s.daySeed;
    float clearSky = clampf(1.f - (s.cloud - 0.45f) * 2.5f, 0.f, 1.f) * (1.f - s.rain);
    float rbT = (s.rainAgo > 2.f && s.rainAgo < 150.f && s.wet > 0.25f && s.rain < 0.15f && s.cloud < 0.85f &&
                 s.sunElev > 0.0f && s.sunElev < 0.45f) ? 1.f : 0.f;
    if (g_force.rainbow >= 0) rbT = g_force.rainbow;
    float prevRb = s.rainbow;
    s.rainbow = follow(s.rainbow, rbT, dt, 4.f);
    if (prevRb < 0.3f && s.rainbow >= 0.3f) sfxMoment(MOMENT_RAINBOW);

    float mT = ((ds & 7u) < 6u ? 1.f : 0.f) * bump(s.tod, 0.69f, 0.715f, 0.79f, 0.82f) * clearSky;
    if (g_force.murmur >= 0) mT = g_force.murmur;
    float prevM = s.murmur;
    s.murmur = follow(s.murmur, mT, dt, 3.f);
    s.murmurAz = ((ds >> 8) & 255) / 255.f * 0.9f - 0.45f;      // against the sunset glow
    if (prevM < 0.25f && s.murmur >= 0.25f) sfxMoment(MOMENT_MURMUR);

    float ffT = (bump(s.tod, 0.73f, 0.78f, 0.97f, 1.01f) + bump(s.tod, -0.1f, -0.05f, 0.04f, 0.09f)) * (1.f - s.rain) * (1.f - s.storm) * (0.35f + 0.65f * clampf(1.f - fabsf(s.forestNear - 0.45f) * 2.f, 0.f, 1.f));
    ffT *= 1.f - biomeAt(distance).desert;                    // no fireflies in the desert
    if (g_force.fireflies >= 0) ffT = g_force.fireflies;
    s.fireflies = follow(s.fireflies, ffT, dt, 3.f);

    bool night = s.tod > 0.86f || s.tod < 0.15f;
    float nightK = night ? clampf(fminf(fabsf(s.tod - 0.86f), fabsf(s.tod - 0.15f)) * 20.f, 0.f, 1.f) : 0.f;
    float auB = biomeAt(distance).auroraBoost;                 // the far north: aurora most nights
    float auT = ((ds >> 3) % 10u < (uint32_t)(3 + 5 * auB) ? 1.f : 0.f) * nightK * clampf(1.f - (s.cloud - 0.4f) * 3.f, 0.f, 1.f);
    if (g_force.aurora >= 0) auT = g_force.aurora;
    float prevA = s.aurora;
    s.aurora = follow(s.aurora, auT, dt, 6.f);
    if (prevA < 0.3f && s.aurora >= 0.3f) sfxMoment(MOMENT_AURORA);
    float meT = nightK * clampf(1.f - (s.cloud - 0.4f) * 3.f, 0.f, 1.f) * ((ds >> 5) % 5u < 2u ? 1.f : 0.12f);
    if (g_force.meteors >= 0) meT = g_force.meteors;
    s.meteors = follow(s.meteors, meT, dt, 4.f);

    float low = s.sunElev > -0.05f ? clampf(1.f - s.sunElev / 0.4f, 0.f, 1.f) : 0.f;
    s.beams = low * clearSky;
    s.intro = clampf(1.f - t / 7.f, 0.f, 1.f);

    float sunsetLake = low * s.lakeNear * clearSky;
    float cathedral = s.beams * clampf((s.forestNear - 0.35f) * 3.f, 0.f, 1.f);
    s.awe = fmaxf(fmaxf(sunsetLake, s.rainbow), fmaxf(fmaxf(s.murmur, s.aurora * 0.85f), fmaxf(cathedral * 0.7f, s.meteors * 0.4f)));
}

}
