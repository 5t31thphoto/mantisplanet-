#pragma once
#include <stdint.h>

// The near field: everything you can walk up to. It lives in a small local plane centred on
// you (radius FIELD_R). Walking shifts it, turning just changes which part you look at.
// Whatever falls out behind you is forgotten and re-dreamt at the far edge ahead, seeded by
// the biome fields at that direction and distance, so the only thing that persists is what
// is around you right now.

namespace mantis {

enum ObjType : uint8_t {
    OBJ_NONE, OBJ_PINE, OBJ_BROAD, OBJ_BUSH, OBJ_FLOWERS, OBJ_TUFT, OBJ_BOULDER, OBJ_OUTCROP, OBJ_REEDS, OBJ_LANTERN, OBJ_BAMBOO, OBJ_TORII, OBJ_SAGUARO, OBJ_PEAR, OBJ_LOG, OBJ_TUNNELTREE, OBJ_HOLLOWLOG, OBJ_BIGLEAF
};

struct FieldObj {
    float x, y;        // metres-ish, player at origin, +x = world angle 0
    float a, b;        // type sizes (height / crown, width / height ...)
    float radius;      // collision radius for solid objects
    uint32_t seed;
    uint8_t type;
    bool solid;
    uint8_t var;       // outcrops: 1 waterfall, 2 ruin walls, 4 mesa, 8 hoodoo, 16 arch, 32 sea stack, 64 karst pillar; trees: 3 cherry, 4 maple, 5 black pine, 6 redwood, 7 rainforest giant
    bool wet;          // drifted into water: hidden and walk-through until it's dry again
};

enum : uint8_t { CR_DEER, CR_RABBIT, CR_BIRDS, CR_BUTTERFLY, CR_DUCK, CR_BEAVER, CR_SQUIRREL, CR_KOI, CR_LIZARD, CR_KINDS };
struct FieldCritter {
    bool on;
    uint8_t kind;
    float x, y, h;     // position in the local plane; height above ground (butterflies, ducks taking off, squirrels climbing)
    float t0, idleT;
    int dir;           // flee direction: +1 / -1 across the line of sight
    bool faceRight;
    bool fleeing;
    float tx, ty;      // a target: the water for a beaver, the tree for a squirrel
    float fleeT;       // seconds since it started fleeing
    uint32_t seed;
    float splashT;     // beaver: time of the plunge (for the ring on the water)
    uint8_t variant;   // 1: the north / marsh version; 2: the garden's; 3: the canyon's (mule deer, jackrabbit, roadrunner)
};
constexpr int CRIT_N = 6;

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
// Returns how many critters bolted this frame; fieldBolt(i) says which kind and how far (for sound and motor).
int fieldTick(float yaw, float distance, float timeSec, float dt, float speed, float stillFor);
void fieldBolt(int i, uint8_t& kind, float& dist);

const FieldObj*     fieldObjects();
const FieldCritter* fieldCritters();     // CRIT_N slots
const Firefly*      fieldFireflies();
float               rockFieldAt(float angle, float D);
float               fieldNearestFalls();      // metres to the nearest waterfall (999 if none)

}
