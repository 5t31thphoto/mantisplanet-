#include "haptics.h"
#include "scene.h"
#include <M5Unified.h>
#include <math.h>

namespace hx {

struct Key { uint8_t level; uint16_t ms; };

static const Key G_GRASS[]  = {{45, 20}, {60, 25}, {25, 35}, {0, 45}};
static const Key G_FOREST[] = {{55, 18}, {75, 22}, {45, 25}, {0, 45}};            // a softer crunch
static const Key G_STONE[]  = {{95, 14}, {40, 30}, {0, 40}};                       // firm, not sharp
static const Key G_MUD[]    = {{40, 35}, {70, 35}, {25, 45}, {0, 55}};             // squish swells
static const Key G_WET[]    = {{45, 22}, {55, 30}, {0, 45}};
static const Key G_BUMP[]   = {{230, 40}, {130, 60}, {50, 80}, {0, 110}};
static const Key G_THUNDER[]= {{50, 160}, {150, 120}, {80, 170}, {210, 90}, {120, 260}, {70, 320}, {0, 480}};
static const Key G_CRACK[]  = {{255, 16}, {150, 26}, {235, 20}, {90, 40}, {0, 50}};
static const Key G_HOOVES[] = {{120, 14}, {0, 90}, {105, 14}, {0, 95}, {85, 13}, {0, 100}, {65, 12}, {0, 110}, {45, 12}, {0, 60}};
static const Key G_SWELL[]  = {{20, 500}, {85, 900}, {30, 900}, {0, 900}};
static const Key G_LUBDUB[] = {{175, 45}, {30, 90}, {125, 40}, {0, 220}};
static const Key G_MUTE[]   = {{120, 30}, {0, 70}, {120, 30}, {0, 40}};
static const Key G_UNMUTE[] = {{40, 60}, {150, 50}, {0, 60}};
static const Key* const STEP[] = {G_GRASS, G_FOREST, G_STONE, G_MUD, G_WET};
static const int STEP_N[] = {3, 4, 2, 4, 3};

struct Gesture { const Key* k; int n, i; uint32_t t0; uint8_t from; float scale; bool on; };
static Gesture s_g[3];            // a few gestures can overlap (a footstep over a thunder roll)
static uint32_t s_tapUntil = 0;
static uint8_t s_tap = 0;
static float s_rum = 0.f, s_rumT = 0.f, s_rumRate = 6.f, s_rumGrit = 0.f, s_rumPh = 0.f, s_rumJit = 0.f;
static uint32_t s_rumJitAt = 0, s_lastWrite = 0, s_thunderAt = 0;
static uint8_t s_now = 255;
static float s_thunderPow = 0.f, s_thunderClose = 0.f;
static bool s_thunderPending = false;

static void vib(uint8_t v) {
    uint32_t now = millis();
    if (v == s_now) return;
    if (v != 0 && s_now != 0 && (v > s_now ? v - s_now : s_now - v) < 4) return;
    if (v != 0 && now - s_lastWrite < 4) return;
    s_now = v;
    s_lastWrite = now;
    M5.Power.setVibration(v);
}

static void gesture(const Key* k, int n, float scale) {
    Gesture* slot = &s_g[0];
    for (Gesture& g : s_g) if (!g.on) { slot = &g; break; }
    *slot = {k, n, 0, millis(), 0, scale, true};
}
static void tap(uint8_t level, uint16_t ms) {
    uint32_t now = millis();
    if (level >= s_tap || now >= s_tapUntil) { s_tap = level; s_tapUntil = now + ms; }
}

void begin() { s_now = 255; vib(0); for (Gesture& g : s_g) g.on = false; }
void footstep(uint8_t surface, float strength) {
    if (surface > 4) surface = 0;
    gesture(STEP[surface], STEP_N[surface], 0.6f + 0.5f * strength);
}
void bump() { gesture(G_BUMP, 4, 1.f); }
void kick(float level) { tap((uint8_t)(70 + 90 * level), 38); }
void thunder(float power, float close) {
    if (close > 0.45f) gesture(G_CRACK, 5, 0.6f + 0.4f * power);
    s_thunderPow = power; s_thunderClose = close; s_thunderAt = millis() + (close > 0.45f ? 120 : 0); s_thunderPending = true;
}
void hooves(float dist) { gesture(G_HOOVES, 10, fmaxf(0.3f, 1.f - dist * 0.05f)); }
void swell() { gesture(G_SWELL, 4, 1.f); }
void heartbeat() { gesture(G_LUBDUB, 4, 1.f); }
void confirmMute(bool muted) { if (muted) gesture(G_MUTE, 4, 1.f); else gesture(G_UNMUTE, 3, 1.f); }
void rain(float amount) {
    if (amount < 0.05f) return;
    if ((esp_random() & 1023u) < (uint32_t)(amount * 140.f)) tap((uint8_t)(55 + (esp_random() & 31u)), 9);   // drops on your hands
}
void wind(float storm, float gust) { s_rumT = storm * gust * 0.35f; s_rumRate = 3.f + gust * 5.f; s_rumGrit = 0.6f; }

void update(float dt) {
    uint32_t now = millis();
    if (s_thunderPending && now >= s_thunderAt) { gesture(G_THUNDER, 7, 0.35f + 0.65f * s_thunderPow * (0.5f + 0.5f * s_thunderClose)); s_thunderPending = false; }
    float out = 0.f;
    if (now < s_tapUntil) out = s_tap / 255.f;
    // continuous rumble with pulse rate and grit
    s_rum += (s_rumT - s_rum) * fminf(1.f, dt * 3.f);
    if (s_rum > 0.01f) {
        s_rumPh += dt * s_rumRate;
        if (now >= s_rumJitAt) { s_rumJit = (esp_random() & 255) / 255.f; s_rumJitAt = now + 40; }
        float p = 0.5f + 0.5f * sinf(s_rumPh * 6.2831853f);
        out = fmaxf(out, s_rum * (0.6f + 0.4f * p) * (1.f - s_rumGrit * 0.5f + s_rumGrit * s_rumJit));
    }
    for (Gesture& g : s_g) {
        if (!g.on) continue;
        uint32_t e = now - g.t0;
        while (g.i < g.n && e >= g.k[g.i].ms) { e -= g.k[g.i].ms; g.from = g.k[g.i].level; g.t0 += g.k[g.i].ms; g.i++; }
        if (g.i >= g.n) { g.on = false; continue; }
        float u = (float)e / (float)(g.k[g.i].ms + 1);
        out = fmaxf(out, (g.from + (g.k[g.i].level - g.from) * u) / 255.f * g.scale);
    }
    // above the dead zone the motor responds; below it, silence
    vib(out < 0.03f ? 0 : (uint8_t)fminf(255.f, 55.f + out * 200.f));
}
}
