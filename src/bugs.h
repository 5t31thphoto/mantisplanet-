#pragma once
#include <stdint.h>
#include "scene.h"
// Bait bugs. Little things living around you: worms after rain, crickets and grasshoppers in the grass,
// beetles in the woods, mayflies by the water, fireflies at night, and one special bug in each special
// biome. They glint now and then so you can spot them; tap one to catch it. With the rod out, the
// middle touch button picks your bait; a special bug used in its own biome's water draws the rare ones.
namespace mantis {

enum : uint8_t { BAIT_WORM, BAIT_CRICKET, BAIT_HOPPER, BAIT_BEETLE, BAIT_MAYFLY, BAIT_FIREFLY,
                 BAIT_JEWEL, BAIT_SLUG, BAIT_CRAB, BAIT_ICEWORM, BAIT_CATERPILLAR, BAIT_SILKMOTH, BAIT_DESERTBEETLE, BAIT_MOONMOTH,
                 BAIT_KINDS };
constexpr int BUG_N = 4;

struct Bug { bool on; uint8_t kind; float x, y, h; float t0, life, hopT, hopLeft; int dir; bool caught; float caughtT; int sx, sy; };

void bugsInit();
void bugsUpdate(float dt, float yaw, float distance, float speed, const Scene& sc);
void bugsShift(float sx, float sy);                // follow the field when you walk
const Bug* bugs();
void bugsNoteScreen(int i, int sx, int sy);         // the renderer says where each bug is on screen (320 x 240)
bool bugsTryCatch(int x, int y);                    // a tap: caught one?
const char* baitName(uint8_t kind);
int  baitCount(uint8_t kind);
int  baitSelected();                                // -1 = no bait
void baitCycle();                                   // the next bait you have
void baitSelect(int kind);                          // put this kind on the hook (-1: the plain lure)
bool baitFound(uint8_t kind);                       // ever caught one (the collection keeps its discoveries)
void baitAdd(uint8_t kind, int n);                  // tests
bool baitConsume();                                 // use one on the hook (false if none)
uint8_t baitSpecialBiome(uint8_t kind);             // the biome a special bait belongs to (255: everyday bait)

}
