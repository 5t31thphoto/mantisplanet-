#pragma once
#include <stdint.h>

// One shared picture of "what the world is doing right now", written by the main loop and read by
// the renderer and the audio task. Floats only (32-bit stores are atomic on the ESP32).

namespace mantis {

enum : uint8_t { SURF_GRASS, SURF_FOREST, SURF_STONE, SURF_MUD, SURF_WET, SURF_SPLASH, SURF_SNOW, SURF_SAND, SURF_ICE };

struct Scene {
    float t, dt, tod, yaw, distance, speed;
    float walkFor, stillFor;                   // seconds of steady walking / of standing still
    float sunElev, sunAz;                      // the sun's real arc: rises east (az 0), south at noon, sets west

    // weather
    float cloud, rain, storm, fog, wind, gust, wet;
    float rainAgo;                             // seconds since it last rained properly
    float flash;                               // lightning flash 0..1
    float boltAz, boltAge;                     // last bolt: azimuth, seconds since
    uint32_t boltSeed;

    float forestNear, lakeNear, rockNear, open, streamNear, fallsNear;
    float snowy, frogBoost, cuckoo, crows, japan, desert, hush, giant, oceanNear, karst, tundra, tropical;
    uint8_t surface;

    // moments
    float rainbow, murmur, murmurAz, fireflies, meteors, aurora, beams, intro, awe;
    uint32_t daySeed;
};

extern Scene g_scene;

// Weather overrides for previews / tests (negative = automatic).
struct WeatherForce { float cloud = -1, rain = -1, storm = -1, fog = -1, wind = -1, wet = -1, rainbow = -1, murmur = -1, fireflies = -1, aurora = -1, meteors = -1; };
extern WeatherForce g_force;

void sceneInit();
void sceneUpdate(float t, float dt, float yaw, float distance, float speed, bool tryingToWalk);

// Scheduled things the main loop turns into haptics and sound.
struct Thunder { float at, power, close; bool pending; };
extern Thunder g_thunder;

}
