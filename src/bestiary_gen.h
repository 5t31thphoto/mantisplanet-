#pragma once
#include <stdint.h>
// GENERATED from tools/sprites/bestiary.json by tools/sprites/gen_bestiary.py. Don't edit by hand.
// Moddable creatures (other than deer): their art on the card is /mp-sprites/<id>/<anim>_<tier>.mps
struct BestiaryEntry {
    const char* id; uint8_t kind, variant; int8_t seedParity;   // parity: -1 any, 0 even seed, 1 odd seed
    float heightM; bool flock;                                  // flock: one bird, the game draws the flock
    uint8_t tierW[3], tierH[3];                                 // far, mid, near (0: no art at that size)
    const char* anim[2];                                        // [0] at rest, [1] moving ("" if none)
};
static const BestiaryEntry BESTIARY[] = {
    {"rabbit", 1, 0, -1, 0.500f, false, {5, 10, 20}, {4, 8, 16}, {"idle", "move"}},
    {"white_hare", 1, 1, -1, 0.500f, false, {5, 10, 20}, {4, 8, 16}, {"idle", "move"}},
    {"jackrabbit", 1, 3, -1, 0.500f, false, {5, 10, 20}, {4, 8, 16}, {"idle", "move"}},
    {"arctic_fox", 1, 5, -1, 0.310f, false, {5, 10, 20}, {3, 5, 10}, {"idle", "move"}},
    {"songbirds", 2, 0, -1, 0.100f, true, {0, 5, 0}, {0, 3, 0}, {"idle", "move"}},
    {"roadrunner", 2, 3, -1, 0.370f, false, {4, 8, 16}, {3, 6, 12}, {"idle", "move"}},
    {"snowy_owl", 2, 5, -1, 0.310f, false, {3, 4, 8}, {3, 5, 10}, {"idle", "move"}},
    {"parrots", 2, 6, -1, 0.100f, true, {0, 6, 0}, {0, 4, 0}, {"idle", "move"}},
    {"butterfly", 3, 0, -1, 0.090f, false, {2, 3, 5}, {1, 2, 3}, {"idle", "move"}},
    {"dragonfly", 3, 1, -1, 0.090f, false, {0, 3, 0}, {0, 3, 0}, {"idle", "move"}},
    {"kingfisher", 3, 2, -1, 0.060f, false, {0, 4, 0}, {0, 3, 0}, {"idle", "move"}},
    {"blue_morpho", 3, 3, -1, 0.090f, false, {0, 5, 0}, {0, 3, 0}, {"idle", "move"}},
    {"duck", 4, 0, -1, 0.370f, false, {4, 8, 14}, {4, 7, 12}, {"idle", "move"}},
    {"heron", 4, 1, -1, 0.620f, false, {3, 6, 12}, {6, 11, 22}, {"idle", "move"}},
    {"white_crane", 4, 2, 1, 0.620f, false, {3, 6, 12}, {6, 11, 22}, {"idle", "move"}},
    {"mandarin_duck", 4, 2, 0, 0.370f, false, {4, 8, 14}, {4, 7, 12}, {"idle", "move"}},
    {"beaver", 5, 0, -1, 0.250f, false, {5, 9, 18}, {2, 4, 8}, {"idle", "move"}},
    {"squirrel", 6, 0, -1, 0.310f, false, {3, 6, 12}, {3, 5, 10}, {"idle", "move"}},
    {"koi", 7, 0, -1, 0.060f, false, {0, 5, 0}, {0, 2, 0}, {"idle", ""}},
    {"lizard", 8, 0, -1, 0.120f, false, {4, 7, 14}, {1, 2, 4}, {"idle", "move"}},
};
static constexpr int BESTIARY_N = sizeof(BESTIARY) / sizeof(BESTIARY[0]);
