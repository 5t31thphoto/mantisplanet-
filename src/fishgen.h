#pragma once
#include <stdint.h>

// shape, colour and pattern are varied by seed. Fish are drawn straight into a full-resolution
// RGB565 overlay (0 = transparent), with light from above, hue-shifted shadows, translucent fins
// with rays, a gill plate, an eye with a highlight, and patterns that follow the body.
namespace mantis {

struct FCol { float r, g, b; };

struct FishSpec {
    uint32_t seed;
    uint8_t family, tier, water, biome;
    char name[40];
    char familyName[24];
    float lengthCm, weightKg;
    // shape
    float depth;          // body depth / body length
    float peakU;          // where the body is deepest (0 tail .. 1 snout)
    float pedun;          // peduncle thickness (fraction of max depth)
    float blunt;          // 0 pointed .. 1 blunt head
    float hump, belly;    // top / bottom asymmetry
    float tailLen;        // fraction of total length
    uint8_t tailType;     // 0 forked, 1 truncate, 2 rounded, 3 lunate
    float tailFork;       // notch depth
    float tailSpread;     // tail height vs body depth
    float d1s, d1e, d1h;  // first dorsal: start, end (u), height (x max depth)
    uint8_t d1spiny, d1sail;
    float d2s, d2e, d2h;  // second dorsal (0 height = none)
    bool adipose, barbels;
    float analH, finLong; // anal fin height; long trailing fins
    // colour
    FCol back, flank, belly2, fin, pat, pat2, eye;
    uint8_t pattern;      // see fishgen.cpp
    float patScale, patDensity;
    bool halo, shimmer, finEdge;
    float scales;         // scale texture strength
};

enum : uint8_t { FB_COUNT = 19 };

// water: WATER_* from fishing.h; biome: B_* from journey.h
void fishMake(FishSpec& f, uint32_t seed, uint8_t water, uint8_t biome, uint8_t tier);
// Draw into an RGB565 buffer (native, 0 = transparent), snout pointing right, centred at (cx, cy).
void fishRender(const FishSpec& f, uint16_t* buf, int bw, int bh, int cx, int cy, int lenPx);

}
