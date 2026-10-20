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
#include "journey.h"
#include "fishing.h"
#include "trophy.h"
#include "storage.h"
#include "wall.h"
#include "bugs.h"
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
static bool tapPending = false;
static int tapX = 0, tapY = 0;
static uint8_t prevFishState = 0;
static uint32_t landedAt = 0;
static bool trophySaved = false;
static bool showFps = false;
static float fpsAvg = 30.f;

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
    if (td.wasReleased()) {
        if (touchDown && !longFired && now - touchDown < 350 && touchY0 < 240) { tapPending = true; tapX = touchX0; tapY = touchY0; }   // a short tap on the screen
        touchDown = 0;
    }
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
    // A new world every time you wake it, but never one that starts you in the water or boxed in by it.
    for (int tries = 0; tries < 24; tries++) {
        mantis::universeInit(esp_random() | 1u);
        mantis::journeyInit(mantis::worldSeed(), 0.f);
        int wet = 0, n = 0;
        bool underfoot = mantis::lakeField(0.f, 0.f) > mantis::LAKE_T - 0.05f;
        for (int k = 0; k < 16; k++)
            for (float z = 2.f; z < 30.f; z += 4.f, n++)
                if (mantis::lakeField(k * 0.3927f, z) > mantis::LAKE_T) wet++;
        if (!underfoot && wet * 4 < n) break;                   // dry feet, mostly open land around
    }
    mantis::renderInit();
    mantis::storageBegin();                       // the SD card, if there is one
    mantis::bugsInit();
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
    if (mantis::fishingActive() || mantis::wallActive()) { currentSpeed = 0.f; trying = false; }   // the rod is out / the wall is up: tilts don't walk
    serviceTouch();
    if (M5.BtnB.wasPressed()) { if (mantis::fishingActive()) mantis::baitCycle(); else showFps = !showFps; }   // middle button: bait (rod out) / frame rate
    if (M5.BtnC.wasPressed()) {                               // the right touch button: the catch wall
        if (mantis::wallActive()) mantis::wallClose();
        else if (!mantis::fishingActive()) mantis::wallOpen();
    }
    if (M5.BtnA.wasPressed()) {                               // the left touch button: a screenshot to SD
        if (mantis::storageScreenshot()) { mantis::sfxFish(mantis::FISH_SHUTTER, 0.f); hx::fishTick(0.5f); }
    }
    fpsAvg += (1.f / dt - fpsAvg) * 0.05f;

    // Walk. Head-on into something solid you stop with a bump and have to find another way;
    // glancing off the side of it you slide past. A shore stops you too.
    float want = currentSpeed > 0.02f ? currentSpeed * dt * 9.0f : 0.f;
    bool bumped = false;
    float moved = mantis::fieldStep(currentYaw, want, totalDistance, &bumped);
    totalDistance += moved;
    mantis::journeyUpdate(totalDistance, currentYaw, moved);
    bool stopped = bumped && moved < want * 0.35f;
    if (stopped && !wasBumped) { hx::bump(); mantis::sfxBump(bumpKind()); }
    wasBumped = stopped;

    float walkSpeed = moved / (dt * 9.0f);
    mantis::sceneUpdate(t, dt, currentYaw, totalDistance, walkSpeed, trying);
    const mantis::Scene& sc = mantis::g_scene;
    if (tapPending) {
        tapPending = false;
        if (mantis::wallActive()) mantis::wallTap(tapX, tapY);
        else if (!mantis::bugsTryCatch(tapX, tapY)) mantis::fishingTap(currentYaw, totalDistance);   // a bug first, then the water
    }
    mantis::fishingUpdate(dt, currentYaw, totalDistance, mantis::controlsPitchDeg(), mantis::controlsFlickDps(), sc);
    mantis::bugsUpdate(dt, currentYaw, totalDistance, walkSpeed, sc);
    {   // landed: hold it up on the trophy card; a moment later it's saved to the card
        uint8_t fs = mantis::fishingView().state;
        if (fs == mantis::FS_LANDED && prevFishState != mantis::FS_LANDED) { mantis::trophyBuild(mantis::fishingSpec()); mantis::overlayEnable(true); landedAt = millis(); trophySaved = false; }
        if (fs == mantis::FS_LANDED && !trophySaved && millis() - landedAt > 700) { mantis::storageSaveTrophy(mantis::fishingSpec()); trophySaved = true; }
        if (fs != mantis::FS_LANDED && prevFishState == mantis::FS_LANDED) mantis::overlayEnable(false);
        prevFishState = fs;
    }

    int bolts = mantis::fieldTick(currentYaw, totalDistance, t, dt, walkSpeed, sc.stillFor);
    for (int i = 0; i < bolts; i++) {
        uint8_t kind; float d;
        mantis::fieldBolt(i, kind, d);
        if (kind & 0x80) { mantis::sfxCritter(mantis::CRIT_SPLASH, d); continue; }
        static const uint8_t MAP[7] = {mantis::CRIT_DEER, mantis::CRIT_RABBIT, mantis::CRIT_BIRDS, mantis::CRIT_BUTTERFLY,
                                       mantis::CRIT_DUCK, mantis::CRIT_BEAVER, mantis::CRIT_SQUIRREL};
        uint8_t variant = kind >> 4;
        kind &= 15;
        mantis::sfxCritter(kind == mantis::CR_DUCK && variant == 1 ? (uint8_t)mantis::CRIT_HERON : MAP[kind < 7 ? kind : 0], d);
        if (kind == mantis::CR_DEER) hx::hooves(d);
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
    static float lastTodH = 0.f;
    if (sc.japan > 0.5f && lastTodH < 0.715f && sc.tod >= 0.715f) hx::swell();     // feel the temple bell
    lastTodH = sc.tod;
    lastAwe = sc.awe;
    hx::update(dt);

    mantis::renderFrame(currentYaw, totalDistance, walkSpeed, t);
    if (muteIconAt) {                                       // the icon shows for a moment, then fades
        float age = (millis() - muteIconAt) / 1000.f;
        float a = age < 1.2f ? 1.f : 1.f - (age - 1.2f) / 0.8f;
        if (a <= 0.f) muteIconAt = 0;
        else mantis::renderMuteIcon(mantis::audioMuted(), a);
    }
    if (showFps) mantis::renderFps((int)(fpsAvg + 0.5f));
    mantis::displayPresent();
}
