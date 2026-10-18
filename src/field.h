#pragma once
#include <stdint.h>

// The near field: everything you can walk up to. It lives in a small local plane centred on
// you (radius FIELD_R). Walking shifts it, turning just changes which part you look at.
// Whatever falls out behind you is forgotten and re-dreamt at the far edge ahead, seeded by
// the biome fields at that direction and distance, so the only thing that persists is what
// is around you right now.

namespace mantis {

enum ObjType : uint8_t {
    OBJ_NONE, OBJ_PINE, OBJ_BROAD, OBJ_BUSH, OBJ_FLOWERS, OBJ_TUFT, OBJ_BOULDER, OBJ_OUTCROP
};

struct FieldObj {
    float x, y;        // metres-ish, player at origin, +x = world angle 0
    float a, b;        // type sizes (height / crown, width / height ...)
    float radius;      // collision radius for solid objects
    uint32_t seed;
    uint8_t type;
    bool solid;
};

struct FieldCritter {
    bool on;
    uint8_t kind;      // 0 deer, 1 rabbit, 2 birds
    float x, y;
    float t0, idleT;
    int dir;           // flee direction: +1 / -1 across the line of sight
    bool faceRight;
    bool fleeing;
};

struct Firefly { float x, y, h, ph, drift; };
constexpr int FIREFLY_N = 40;

constexpr int   FIELD_N = 340;
constexpr float FIELD_R = 34.f;
constexpr float BODY_R  = 0.42f;
constexpr float LAKE_STOP = 3.2f;   // how far ahead the shore stops you

void fieldInit(float yaw, float distance);

// Try to walk `ds` along heading `yaw`. Solid things in the way are slid around; a shore stops
// you. Returns the distance actually covered; *bumped is set when something got in the way.
float fieldStep(float yaw, float ds, float distance, bool* bumped);

// Encounters (critters) and their timing. Standing still invites them closer; walking spooks them.
// Returns true on the frame a critter bolts (for its sound and the motor).
bool fieldTick(float yaw, float distance, float timeSec, float dt, float speed, float stillFor);

const FieldObj*     fieldObjects();
const FieldCritter& fieldCritter();
const Firefly*      fieldFireflies();
float               rockFieldAt(float angle, float D);

}
