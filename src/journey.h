#pragma once
#include <stdint.h>

// The journey: biomes are a sequence along your walk, not a map. Each lasts ~1-1.5 km and blends into
// the next. The director averages your heading through each biome; hold roughly one compass direction
// through three ordinary biomes and the next is that direction's special biome. After a special, it
// loops back to a random ordinary one. Nothing is stored beyond the journey you're on.
//
// Compass (world azimuth): east = 0, south = +pi/2, west = pi, north = -pi/2 (bearings grow to your right). The sun rises in the east.

namespace mantis {

enum : uint8_t {
    B_MEADOW, B_FOREST, B_LAKES, B_HIGHLAND, B_FALLS,       // ordinary biomes
    B_AUTUMN, B_MARSH, B_BIRCH, B_TAIGA,
    B_ORDINARY_N,
    B_JAPAN = B_ORDINARY_N,                                 // special biomes: one per compass direction
    B_CANYON,
    B_REDWOODS,
    B_COAST,
    B_KARST,
    B_TUNDRA,
    B_RAINFOREST,
    B_GROVE,
    B_ALL_N
};

struct Ramp4C { uint8_t k[4][3]; };

struct BiomeParams {
    float forest, forestVar;  // tree cover and how patchy it is
    float pine;               // share of conifers
    float tall;               // extra conifer height (old-growth)
    float lakes;              // 0 dry country .. 1 lake country
    float creeks;             // how often a stretch has creeks
    float rocks;              // boulders and outcrops
    float falls;              // share of natural cliffs with a waterfall
    float ruins;              // share of outcrops as old walls
    float mist;               // standing haze
    float wet;                // leans the weather toward rain
    float flowers;            // ground flower density
    float heather;            // 1: flowers are heather (lavender / pink)
    float wind;               // extra wind
    float snow;               // snowy ground, snow on the trees, ice on the water, snowfall
    float fallLeaves;         // autumn leaves drifting down
    float reeds;              // reeds and cattails at the water's edge
    float lily;               // lily pads on still water
    float frogs;              // frog chorus boost
    float cuckoo, crows;      // birdsong flavours
    float hare;               // white hares and caribou instead of rabbits and deer
    float heron, dragonfly;   // marsh critters instead of ducks and butterflies
    float squirrels;          // squirrel abundance
    float japan;              // the Japanese garden: lanterns, bamboo, gravel, koi, torii, bells
    float petals;             // cherry petals drifting
    float desert;             // dry country: sand, cacti, scrub, no lakes, lizards, hawks, coyotes
    float mesa;               // flat-topped, benched ridges on the horizon
    float starBoost;          // clearer, darker skies
    float giant;              // ancient redwoods: colossal trunks
    float fern;               // a fern floor
    float glowMoss;           // moss that glows faintly at night
    float hush;               // a hushed soundscape and quieter music
    float shafts;             // stronger light shafts
    float ocean;              // the sea to the west: you can't go past it
    float karst;              // pillar mountains in layered mist
    float paddies;            // flooded terraces: still, shallow, wadeable
    float auroraBoost;        // the north: more aurora, reaching higher
    float sundogs;            // halos beside the low sun
    float tundra;             // treeless snow country: herds, foxes, owls, singing ice
    float tropical;           // the rainforest: vines, big leaves, emerald water, parrots, morphos, howlers
    float pondScale;          // >1: smaller, more frequent ponds (gardens)
    float moonlight;          // nights bright enough to see by
    float stag;               // the white stag may appear
    float leaf[4][3], grass[4][3], stone[4][3], bark[4][3], rock[4][3];   // hue-shifted ramps (shadow -> highlight); rock = far ranges
};

void journeyInit(uint32_t seed, float distance);
// Call every frame with what you actually covered and the heading you covered it in.
void journeyUpdate(float distance, float yaw, float moved);
// Blended biome parameters at a distance along the journey (cheap; called a lot).
const BiomeParams& biomeAt(float D);
// The biome you're in and the one you're blending toward (for display / music).
uint8_t journeyBiome();
uint8_t biomeIdAt(float D);    // which biome a distance falls in (previews / tests)
float biomeStart(float D);     // where the biome containing D begins
void journeyForce(float D, uint8_t b);   // previews / tests
uint32_t journeyGeneration();          // bumps whenever the biome plan changes (for caches)
int journeyStreak();          // how many biomes in a row you've held the current heading
int journeyOctant();          // 0 = E, 1 = SE, 2 = S, 3 = SW, 4 = W, 5 = NW, 6 = N, 7 = NE
// planet mods: the address of one biome ramp colour (3 floats, 0..255); call ColoursChanged after editing
float* journeyRampSlot(int biome, const char* ramp, int shade);
void journeyColoursChanged();

}
