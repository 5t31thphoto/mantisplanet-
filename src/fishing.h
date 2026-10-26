#pragma once
#include <stdint.h>
#include "scene.h"
#include "fishgen.h"

// Fishing. Walk up to water and tap: the rod comes up and you cast. Watch the bobber; nibbles, then a bite.
// Flick the device toward you to set the hook. Tilt back to reel, lean forward to give line, and feel the
// fight in your hands: tension as a rumble, the fish's kicks, a stutter when the line is about to go.
// Turn away from the water while waiting and the rod is put away. Walking is suspended while the rod is out.
namespace mantis {

enum : uint8_t { FS_OFF, FS_CAST, FS_WAIT, FS_FIGHT, FS_LANDED, FS_LOST, FS_STOW };
enum : uint8_t { WATER_LAKE, WATER_CREEK, WATER_SEA, WATER_PADDY, WATER_ICE };
enum : uint8_t { TIER_COMMON, TIER_UNCOMMON, TIER_RARE, TIER_LEGEND };

struct FishView {
    uint8_t state;
    float t;              // seconds in this state
    float rodOut;         // 0 stowed .. 1 up
    float castYaw;        // where the line went in (world azimuth)
    float bobZ;           // bobber distance (m)
    float nibble;         // 0..1 a twitch of the bobber
    float dip;            // 0..1 the bobber pulled under (a bite)
    bool biting;          // the strike window is open
    float lineOut;        // fight: metres of line out
    float fishAng;        // fight: where the fish is (world azimuth)
    float shoreZ;         // fight: where the water begins along the fish's bearing (m); it's landed there
    float tension;        // 0..1.5
    float breakRisk;      // 0..1
    float stamina;        // 0..1 fish strength left
    float reel, ease;     // what your hands are doing (0..1)
    float jump;           // >0 while the fish is in the air
    float lineSpeed;      // m/s, + = fish taking line
    uint8_t water, tier;
    uint32_t fishSeed;
    float lengthCm;
    uint8_t lostWhy;      // 1 line snapped, 2 hook pulled free, 3 missed the bite, 4 spooked it
};

bool fishingActive();                                     // the rod is out: walking is suspended
// A short tap. Returns true if it was used (cast, or put the rod away).
bool fishingTap(float yaw, float distance);
void fishingUpdate(float dt, float yaw, float distance, float pitchDeg, float flickDps, const Scene& sc);
const FishView& fishingView();
const FishSpec& fishingSpec();                            // the fish on the line (species, name, size)
void fishingForceBite();                                  // tests
void fishingTestFight(uint8_t tier, float lineOut, float yaw, uint32_t seed);
bool fishingWaterAt(float angle, float D);          // is there water here (lake, sea, creek, terrace, ice)
void fishingTune(int tier, float strength, float stamina, float runRate, float runPow, float ka, float kb, float kbreak);

}
