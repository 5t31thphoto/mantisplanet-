// MantisPlanet — a dream you walk through on an M5Stack Core2.
// The far world is a panorama sampled from (direction, distance walked); the near world is a small
// transient field around you, forgotten behind you and re-dreamt ahead. Sound runs on core 0, the
// LCD push on core 0, everything else here on core 1.
#include <M5Unified.h>
#include "sim.h"
#include "universe.h"
#include "field.h"
#include "scene.h"
#include "render.h"
#include "display.h"
#include "haptics.h"
#include "audio.h"
#ifndef HOST
#include <Preferences.h>
#endif

static float totalDistance = 0.0f;
static float currentYaw = 0.0f;
static float currentSpeed = 0.0f;
static bool wasBumped = false;
static bool introBell = false;
static float stepPhase = 0.65f;
static int lastBeat = -1;

// Long press anywhere on the screen: sound off / on. The motor keeps going either way.
static uint32_t touchDown = 0;
static int touchX0 = 0, touchY0 = 0;
static bool longFired = false;
static uint32_t muteIconAt = 0;

static void saveMute(bool m) {
#ifndef HOST
    Preferences p;
    if (p.begin("mantisplanet", false)) { p.putBool("mute", m); p.end(); }
#else
    (void)m;
#endif
}
static bool loadMute() {
#ifndef HOST
    Preferences p;
    bool m = false;
    if (p.begin("mantisplanet", true)) { m = p.getBool("mute", false); p.end(); }
    return m;
#else
    return false;
#endif
}

static void serviceTouch() {
    auto td = M5.Touch.getDetail();
    uint32_t now = millis();
    if (td.wasPressed()) { touchDown = now; touchX0 = td.x; touchY0 = td.y; longFired = false; }
    else if (td.isPressed() && touchDown) {
        if (abs(td.x - touchX0) + abs(td.y - touchY0) > 14) touchDown = 0;          // a drag, not a hold
        else if (!longFired && now - touchDown > 650) {
            longFired = true;
            bool m = !mantis::audioMuted();
            mantis::audioSetMuted(m);
            saveMute(m);
            hx::confirmMute(m);
            muteIconAt = now;
        }
    }
    if (td.wasReleased()) touchDown = 0;
}

static uint8_t bumpKind() {
    const mantis::FieldObj* o = mantis::fieldObjects();
    float best = 1e9f;
    uint8_t k = mantis::BUMP_WATER;
    for (int i = 0; i < mantis::FIELD_N; i++) {
        if (!o[i].solid) continue;
        float d = sqrtf(o[i].x * o[i].x + o[i].y * o[i].y) - o[i].radius;
        if (d < best) { best = d; k = (o[i].type == mantis::OBJ_BOULDER || o[i].type == mantis::OBJ_OUTCROP) ? (uint8_t)mantis::BUMP_ROCK : (uint8_t)mantis::BUMP_TREE; }
    }
    return best < 1.0f ? k : (uint8_t)mantis::BUMP_WATER;
}

static void step(uint8_t surf, float str) {
    hx::footstep(surf, str);
    mantis::sfxFootstep(surf, str);
}

void setup() {
    auto cfg = M5.config();
    cfg.internal_imu = true;
    cfg.internal_spk = true;
    cfg.internal_mic = false;                 // the Core2 shares one I2S port: the speaker keeps it
    M5.begin(cfg);
    M5.Display.setRotation(1);
    M5.Display.setBrightness(110);
    if (M5.Imu.isEnabled()) {
        M5.Imu.loadOffsetFromNVS();
        for (int i = 0; i < 15; i++) { M5.Imu.update(); delay(4); }
    }

    mantis::initControls();
    mantis::universeInit(1337);
    mantis::renderInit();
    mantis::fieldInit(currentYaw, totalDistance);
    mantis::sceneInit();
    mantis::displayInit();
    hx::begin();
    mantis::audioBegin();
    if (loadMute()) { mantis::audioSetMuted(true); muteIconAt = millis(); }
}

void loop() {
    static uint32_t prev = micros();
    uint32_t now = micros();
    float dt = (now - prev) / 1e6f;
    if (dt > 0.05f) dt = 0.05f;
    if (dt < 0.001f) dt = 0.001f;
    prev = now;
    const float t = millis() / 1000.0f;

    bool trying = false;
    mantis::updateControls(dt, currentYaw, currentSpeed, trying);   // calls M5.update()
    serviceTouch();

    // Walk. Head-on into something solid you stop with a bump and have to find another way;
    // glancing off the side of it you slide past. A shore stops you too.
    float want = currentSpeed > 0.02f ? currentSpeed * dt * 9.0f : 0.f;
    bool bumped = false;
    float moved = mantis::fieldStep(currentYaw, want, totalDistance, &bumped);
    totalDistance += moved;
    bool stopped = bumped && moved < want * 0.35f;
    if (stopped && !wasBumped) { hx::bump(); mantis::sfxBump(bumpKind()); }
    wasBumped = stopped;

    float walkSpeed = moved / (dt * 9.0f);
    mantis::sceneUpdate(t, dt, currentYaw, totalDistance, walkSpeed, trying);
    const mantis::Scene& sc = mantis::g_scene;

    if (mantis::fieldTick(currentYaw, totalDistance, t, dt, walkSpeed, sc.stillFor)) {
        const mantis::FieldCritter& c = mantis::fieldCritter();
        float d = sqrtf(c.x * c.x + c.y * c.y);
        uint8_t k = c.kind == 0 ? mantis::CRIT_DEER : c.kind == 1 ? mantis::CRIT_RABBIT : mantis::CRIT_BIRDS;
        mantis::sfxCritter(k, d);
        if (c.kind == 0) hx::hooves(d);
    }

    // Footsteps follow your pace: a slow walk is ~1.3 steps a second, a brisk one ~2.2. Stop and they stop.
    if (walkSpeed > 0.05f) {
        float cadence = 1.3f + 0.9f * fminf(1.f, walkSpeed / 0.8f);
        stepPhase += dt * cadence;
        if (stepPhase >= 1.f) { stepPhase -= 1.f; step(sc.surface, walkSpeed); }
    } else stepPhase = 0.65f;                     // the first step lands soon after you set off
    float beats = mantis::audioBeats();
    // The kick is felt, not heard.
    int beat = (int)floorf(beats);
    if (beat != lastBeat) { lastBeat = beat; float k = mantis::audioKick(); if (k > 0.1f) hx::kick(k); }

    if (mantis::g_thunder.pending && t >= mantis::g_thunder.at) { hx::thunder(mantis::g_thunder.power, mantis::g_thunder.close); mantis::g_thunder.pending = false; }
    hx::rain(sc.rain);
    hx::wind(sc.storm, sc.gust);
    if (!introBell && t > 0.8f) { introBell = true; mantis::sfxMoment(mantis::MOMENT_INTRO); hx::heartbeat(); }
    static float lastAwe = 0.f;
    if (sc.awe > 0.6f && lastAwe <= 0.6f) hx::swell();
    lastAwe = sc.awe;
    hx::update(dt);

    mantis::renderFrame(currentYaw, totalDistance, walkSpeed, t);
    if (muteIconAt) {                                       // the icon shows for a moment, then fades
        float age = (millis() - muteIconAt) / 1000.f;
        float a = age < 1.2f ? 1.f : 1.f - (age - 1.2f) / 0.8f;
        if (a <= 0.f) muteIconAt = 0;
        else mantis::renderMuteIcon(mantis::audioMuted(), a);
    }
    mantis::displayPresent();
}
