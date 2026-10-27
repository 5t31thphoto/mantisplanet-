#pragma once
#include <stdint.h>

namespace mantis {

constexpr float kTau = 6.28318531f;

// ---------- hashing / periodic noise ----------
inline uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}
inline float hashf(uint32_t x) { return (hash32(x) >> 8) * (1.0f / 16777216.0f); }

// Map a world angle (radians, unbounded) onto a lattice that wraps every `period` cells,
// so anything sampled this way closes seamlessly into a 360° panorama.
inline float angU(float angle, int period) { return angle * ((float)period / kTau); }

// 2D value noise, periodic in u with the given period. v is unbounded (distance / time).
float vnoiseP(float u, float v, int period, uint32_t salt);
float fbmP(float u, float v, int period, int octaves, uint32_t salt);

void universeInit(uint32_t seed);
uint32_t worldSeed();

float dayClock(float timeSec, float distance);
inline float timeOfDayAt(float timeSec, float distance) { float c = dayClock(timeSec, distance); return c - (float)(int)c; }
void setTimeOfDay(float tod, float timeSec, float distance);    // previews / tests

constexpr float LAKE_T = 0.665f;          // fewer, smaller lakes
float lakeField(float angle, float D);    // > LAKE_T is water
// The coast: where the sea begins along this direction (a curved bay open to the west), or 1e9 if no sea that way.
float oceanShore(float angle, float D);
constexpr float AZ_EAST = 0.f, AZ_SOUTH = 1.5707963f, AZ_WEST = 3.14159265f, AZ_NORTH = -1.5707963f;
// The sun's true height above the horizon (radians) along its arc: east at dawn, high through the south, west at dusk.
float sunPathElevation(float tod);
bool paddyAt(float angle, float D);
float forestField(float angle, float D);  // 0 meadow .. 1 dense forest
float cloudCover(float D);                // 0 clear .. 1 overcast
float streamField(float angle, float D);
inline bool inStream(float angle, float D) { return streamField(angle, D) < 1.f; }

struct BiomeSample {
    float forest;
    float lake;
    bool isWater;
    bool isSolid;
};
BiomeSample sampleWorld(float angle, float D);

bool shouldFootstep(float distance, float speed);

}
