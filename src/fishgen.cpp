#include "fishgen.h"
#include "fishing.h"
#include "journey.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

namespace mantis {

static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline FCol C(float r, float g, float b) { return FCol{r, g, b}; }
static inline FCol mixc(FCol a, FCol b, float t) { return FCol{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
static inline FCol scalec(FCol a, float k) { return FCol{a.r * k, a.g * k, a.b * k}; }
static inline uint32_t hsh(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
static inline float h01(uint32_t x) { return (hsh(x) >> 8) * (1.f / 16777216.f); }

// light falling on a surface: shadows lean cool and violet, highlights warm (like the rest of the world)
static FCol shade(FCol c, float l) {
    if (l < 1.f) { FCol cool = FCol{c.r * 0.55f + 10.f, c.g * 0.62f + 14.f, c.b * 0.8f + 34.f}; return mixc(cool, c, clampf(l * 1.6f - 0.6f, 0.f, 1.f)); }
    FCol warm = FCol{fminf(255.f, c.r * 1.12f + 30.f), fminf(255.f, c.g * 1.1f + 26.f), fminf(255.f, c.b * 1.02f + 14.f)};
    return mixc(c, warm, clampf(l - 1.f, 0.f, 1.f));
}
static inline uint16_t to565(FCol c) {
    int r = (int)clampf(c.r, 0.f, 255.f), g = (int)clampf(c.g, 0.f, 255.f), b = (int)clampf(c.b, 0.f, 255.f);
    uint16_t v = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return v ? v : 0x0821;                                   // 0 is "transparent"
}

// ---------------- the families ----------------
enum : uint8_t { PAT_NONE, PAT_SPOTS, PAT_HALO_SPOTS, PAT_BARS, PAT_STRIPE, PAT_WAVY, PAT_MARBLE, PAT_SPECKLE, PAT_PARR, PAT_BLOTCH_STRIPE, PAT_OCELLUS, PAT_KOI, PAT_VERMIC };
struct Family {
    const char* name;
    float depth, peakU, pedun, blunt, tailLen; uint8_t tailType; float fork, spread;
    float d1s, d1e, d1h; uint8_t spiny, sail; float d2s, d2e, d2h; bool adipose, barbels; float analH, finLong;
    FCol back, flank, belly, fin, pat, pat2, eye;
    uint8_t pattern; float patScale, patDensity; bool halo, finEdge; float scales;
    float lenMin, lenMax, wK;
    const char* adj[4];
};
static const Family FAM[FB_COUNT] = {
    // name           depth peak  ped  blunt tailL type fork spread  d1s  d1e  d1h  sp sail d2s  d2e  d2h  adip barb anal long
    {"Brook Trout",   0.24f,0.55f,0.42f,0.35f,0.17f,1,0.15f,1.05f,  0.45f,0.6f,0.55f,0,0, 0.f,0.f,0.f,   true,false,0.4f,0.f,
     C(64,84,46),C(104,112,70),C(236,140,70),C(214,110,58),C(250,226,140),C(210,50,50),C(220,180,70), PAT_VERMIC,1.f,1.f,true,true,0.4f, 18,42,11.f,
     {"Speckled","Mossback","Ember-bellied","Lantern"}},
    {"Rainbow Trout", 0.24f,0.52f,0.4f,0.3f,0.17f,0,0.3f,1.1f,      0.45f,0.6f,0.5f,0,0, 0.f,0.f,0.f,     true,false,0.4f,0.f,
     C(70,98,92),C(190,198,196),C(236,232,224),C(150,150,140),C(30,36,40),C(230,104,128),C(220,190,90), PAT_STRIPE,1.f,1.f,false,false,0.6f, 22,60,11.f,
     {"Silver","Sunrise","Steel-sided","Blushing"}},
    {"Grayling",      0.22f,0.6f,0.4f,0.3f,0.17f,0,0.35f,1.f,       0.4f,0.78f,0.9f,0,1, 0.f,0.f,0.f,     true,false,0.35f,0.f,
     C(90,96,110),C(170,176,186),C(224,222,226),C(120,90,150),C(40,40,52),C(200,120,210),C(210,190,120), PAT_SPOTS,0.8f,0.35f,false,false,0.7f, 25,50,10.f,
     {"Sailfin","Violet","Ghost","Mountain"}},
    {"Perch",         0.3f,0.55f,0.36f,0.4f,0.16f,0,0.25f,1.f,      0.38f,0.66f,0.6f,1,0, 0.66f,0.8f,0.45f, false,false,0.4f,0.f,
     C(70,98,52),C(176,182,74),C(236,230,190),C(232,120,52),C(40,58,30),C(0,0,0),C(240,180,50), PAT_BARS,1.f,1.f,false,false,0.6f, 15,40,14.f,
     {"Tiger","Golden","Barred","Reed"}},
    {"Bass",          0.3f,0.5f,0.38f,0.55f,0.16f,1,0.1f,1.f,       0.36f,0.62f,0.5f,1,0, 0.62f,0.82f,0.5f, false,false,0.45f,0.f,
     C(58,82,44),C(122,146,78),C(226,226,190),C(110,130,80),C(40,52,30),C(0,0,0),C(200,60,40), PAT_BLOTCH_STRIPE,1.f,1.f,false,false,0.5f, 25,60,15.f,
     {"Bucketmouth","Shadow","Emerald","Lily"}},
    {"Pike",          0.16f,0.42f,0.5f,0.05f,0.14f,2,0.f,1.1f,      0.18f,0.3f,0.6f,0,0, 0.f,0.f,0.f,     false,false,0.55f,0.f,
     C(46,72,40),C(106,136,72),C(228,230,200),C(150,120,60),C(214,224,150),C(0,0,0),C(230,200,80), PAT_SPOTS,1.3f,1.f,false,false,0.3f, 45,110,8.f,
     {"Water Wolf","Jade","Lurking","Reedline"}},
    {"Sunfish",       0.52f,0.5f,0.32f,0.6f,0.13f,1,0.05f,0.75f,    0.35f,0.7f,0.4f,1,0, 0.f,0.f,0.f,     false,false,0.6f,0.f,
     C(60,92,98),C(90,140,150),C(244,150,60),C(80,110,110),C(40,70,80),C(250,190,70),C(200,40,30), PAT_SPECKLE,0.7f,0.5f,false,false,0.5f, 10,24,12.f,
     {"Pumpkinseed","Sapphire","Copper","Lantern"}},
    {"Catfish",       0.22f,0.62f,0.5f,0.95f,0.15f,2,0.f,1.f,       0.55f,0.68f,0.5f,0,0, 0.f,0.f,0.f,    true,true,0.5f,0.f,
     C(64,62,58),C(112,106,96),C(214,206,186),C(90,84,76),C(40,38,34),C(0,0,0),C(190,170,110), PAT_SPECKLE,1.f,0.4f,false,false,0.f, 30,90,10.f,
     {"Whiskered","Mud","Channel","Old"}},
    {"Mackerel",      0.2f,0.5f,0.22f,0.15f,0.2f,0,0.75f,1.4f,      0.5f,0.66f,0.5f,1,0, 0.3f,0.42f,0.3f, false,false,0.3f,0.f,
     C(40,112,120),C(150,196,200),C(236,240,240),C(70,90,100),C(20,40,60),C(0,0,0),C(220,220,200), PAT_WAVY,1.f,1.f,false,false,0.f, 25,45,9.f,
     {"Tiger","Blue","Tidal","Running"}},
    {"Rockfish",      0.32f,0.58f,0.34f,0.5f,0.15f,1,0.08f,0.95f,   0.3f,0.7f,0.55f,1,0, 0.f,0.f,0.f,     false,false,0.5f,0.f,
     C(170,50,36),C(220,100,56),C(250,190,150),C(214,90,60),C(130,34,30),C(250,230,200),C(240,200,60), PAT_MARBLE,1.f,0.6f,false,false,0.5f, 25,60,14.f,
     {"Vermilion","Canary","Kelp","Ember"}},
    {"Tarpon",        0.26f,0.5f,0.28f,0.35f,0.2f,0,0.7f,1.4f,      0.5f,0.6f,0.55f,0,0, 0.f,0.f,0.f,     false,false,0.4f,0.3f,
     C(90,112,130),C(206,214,222),C(244,246,248),C(140,150,160),C(0,0,0),C(0,0,0),C(230,230,210), PAT_NONE,1.f,1.f,false,false,1.f, 80,200,9.f,
     {"Silver King","Moonlit","Mirror","Old"}},
    {"Koi",           0.27f,0.52f,0.4f,0.5f,0.18f,0,0.25f,1.1f,     0.4f,0.72f,0.4f,0,0, 0.f,0.f,0.f,     false,true,0.4f,0.4f,
     C(244,240,232),C(246,244,238),C(246,244,240),C(240,236,230),C(222,60,36),C(30,30,34),C(30,30,30), PAT_KOI,1.f,1.f,false,false,0.5f, 30,80,12.f,
     {"Kohaku","Showa","Ogon","Tancho"}},
    {"Golden Carp",   0.3f,0.52f,0.4f,0.55f,0.17f,0,0.3f,1.05f,     0.4f,0.75f,0.45f,0,0, 0.f,0.f,0.f,    false,true,0.4f,0.f,
     C(170,120,40),C(224,170,60),C(246,220,140),C(200,140,60),C(140,90,30),C(0,0,0),C(230,200,80), PAT_NONE,1.f,1.f,false,false,1.f, 25,70,13.f,
     {"Golden","Temple","Lucky","River"}},
    {"Loach",         0.12f,0.5f,0.65f,0.8f,0.1f,2,0.f,0.9f,        0.35f,0.45f,0.5f,0,0, 0.f,0.f,0.f,    false,true,0.4f,0.f,
     C(110,90,60),C(170,150,100),C(220,206,160),C(150,130,90),C(70,52,34),C(0,0,0),C(200,180,120), PAT_MARBLE,0.8f,0.7f,false,false,0.f, 10,22,4.f,
     {"Weather","Paddy","Mud","Spotted"}},
    {"Arctic Char",   0.24f,0.55f,0.4f,0.35f,0.17f,0,0.25f,1.05f,   0.45f,0.6f,0.5f,0,0, 0.f,0.f,0.f,     true,false,0.4f,0.f,
     C(44,62,78),C(84,104,116),C(236,104,50),C(220,90,50),C(236,220,200),C(0,0,0),C(220,190,90), PAT_SPOTS,0.8f,0.8f,false,true,0.4f, 30,70,11.f,
     {"Ice","Aurora","Frost","Crimson"}},
    {"Steelhead",     0.23f,0.52f,0.4f,0.3f,0.17f,1,0.15f,1.1f,     0.45f,0.6f,0.5f,0,0, 0.f,0.f,0.f,     true,false,0.4f,0.f,
     C(56,82,92),C(196,204,206),C(240,240,236),C(140,140,136),C(28,34,40),C(220,96,120),C(220,190,90), PAT_STRIPE,0.8f,1.2f,false,false,0.6f, 40,90,11.f,
     {"Redwood","Chrome","Rain","Storm"}},
    {"Peacock Bass",  0.3f,0.5f,0.36f,0.45f,0.16f,2,0.f,1.f,        0.35f,0.62f,0.55f,1,0, 0.62f,0.82f,0.55f, false,false,0.5f,0.f,
     C(70,104,54),C(206,200,72),C(240,210,120),C(214,110,60),C(40,58,36),C(240,140,40),C(220,40,30), PAT_OCELLUS,1.f,1.f,false,false,0.5f, 30,70,15.f,
     {"Butterfly","Fire","Jungle","Royal"}},
    {"Arapaima",      0.17f,0.4f,0.5f,0.4f,0.12f,2,0.f,0.9f,        0.18f,0.3f,0.45f,0,0, 0.f,0.f,0.f,    false,false,0.45f,0.f,
     C(58,72,64),C(110,124,108),C(220,214,200),C(170,60,40),C(200,60,40),C(0,0,0),C(200,170,100), PAT_PARR,1.f,1.f,false,false,1.f, 100,240,8.f,
     {"Giant","River King","Ancient","Scarlet"}},
    {"Moonfin",       0.26f,0.5f,0.32f,0.3f,0.24f,0,0.5f,1.5f,      0.35f,0.75f,0.9f,0,1, 0.f,0.f,0.f,    false,false,0.7f,1.f,
     C(80,96,160),C(170,190,230),C(236,240,252),C(150,170,240),C(250,250,255),C(160,220,255),C(120,220,255), PAT_SPECKLE,0.6f,0.5f,false,true,0.4f, 40,90,11.f,
     {"Moonfin","Moonfin","Moonfin","Moonfin"}},
};

// which families live where
// which families live where (indices into FAM)
enum : uint8_t { F_BROOK, F_RAINBOW, F_GRAYLING, F_PERCH, F_BASS, F_PIKE, F_SUNFISH, F_CATFISH, F_MACKEREL, F_ROCKFISH,
                 F_TARPON, F_KOI, F_CARP, F_LOACH, F_CHAR, F_STEELHEAD, F_PEACOCK, F_ARAPAIMA, F_MOONFIN };
static uint8_t pickFamily(uint32_t seed, uint8_t water, uint8_t biome, uint8_t tier) {
    uint8_t c[8]; int n = 0;
    auto add = [&](uint8_t f) { if (n < 8) c[n++] = f; };
    if (biome == B_GROVE && tier == TIER_LEGEND) return F_MOONFIN;
    if (biome == B_JAPAN) { add(F_KOI); add(F_KOI); add(F_KOI); add(F_SUNFISH); }
    else if (biome == B_RAINFOREST) { add(F_PEACOCK); add(F_PEACOCK); add(F_CATFISH); if (tier >= TIER_RARE) { add(F_ARAPAIMA); add(F_ARAPAIMA); } }
    else if (biome == B_KARST || water == WATER_PADDY) { add(F_CARP); add(F_LOACH); add(F_CARP); add(F_KOI); }
    else if (water == WATER_SEA) { add(F_MACKEREL); add(F_MACKEREL); add(F_ROCKFISH); if (tier >= TIER_RARE) { add(F_TARPON); add(F_TARPON); } }
    else if (water == WATER_ICE || biome == B_TUNDRA || biome == B_TAIGA) { add(F_CHAR); add(F_CHAR); add(F_GRAYLING); }
    else if (biome == B_REDWOODS || biome == B_FALLS) { add(F_STEELHEAD); add(F_BROOK); add(F_RAINBOW); if (water == WATER_CREEK) add(F_STEELHEAD); }
    else if (biome == B_MARSH) { add(F_CATFISH); add(F_PIKE); add(F_SUNFISH); add(F_PERCH); }
    else if (water == WATER_CREEK) { add(F_BROOK); add(F_RAINBOW); add(F_BROOK); add(F_GRAYLING); }
    else { add(F_PERCH); add(F_BASS); add(F_PIKE); add(F_SUNFISH); add(F_RAINBOW); add(F_CATFISH); }
    return c[hsh(seed) % (uint32_t)n];
}

void fishMake(FishSpec& f, uint32_t seed, uint8_t water, uint8_t biome, uint8_t tier) {
    memset(&f, 0, sizeof f);
    f.seed = seed; f.water = water; f.biome = biome; f.tier = tier;
    f.family = pickFamily(seed, water, biome, tier);
    const Family& F = FAM[f.family];
    auto jit = [&](uint32_t k, float amt) { return 1.f + (h01(seed ^ (k * 0x9E3779B9u)) - 0.5f) * 2.f * amt; };
    f.depth = F.depth * jit(1, 0.08f); f.peakU = F.peakU; f.pedun = F.pedun; f.blunt = F.blunt;
    f.hump = (h01(seed ^ 77u) - 0.3f) * 0.2f; f.belly = 1.f + (h01(seed ^ 78u) - 0.5f) * 0.15f;
    f.tailLen = F.tailLen; f.tailType = F.tailType; f.tailFork = F.fork; f.tailSpread = F.spread;
    f.d1s = F.d1s; f.d1e = F.d1e; f.d1h = F.d1h * jit(2, 0.1f); f.d1spiny = F.spiny; f.d1sail = F.sail;
    f.d2s = F.d2s; f.d2e = F.d2e; f.d2h = F.d2h; f.adipose = F.adipose; f.barbels = F.barbels;
    f.analH = F.analH; f.finLong = F.finLong;
    float bright = jit(3, 0.07f);
    f.back = scalec(F.back, bright); f.flank = scalec(F.flank, bright); f.belly2 = F.belly; f.fin = F.fin;
    f.pat = F.pat; f.pat2 = F.pat2; f.eye = F.eye;
    f.pattern = F.pattern; f.patScale = F.patScale * jit(4, 0.15f); f.patDensity = F.patDensity;
    f.halo = F.halo; f.finEdge = F.finEdge; f.scales = F.scales;
    // koi varieties
    if (f.family == F_KOI) {
        uint32_t v = hsh(seed ^ 0xC01u) % 4u;
        if (v == 2) { f.back = C(236, 190, 70); f.flank = C(246, 210, 90); f.belly2 = C(250, 230, 150); f.fin = C(240, 200, 90); f.pattern = PAT_NONE; f.scales = 1.f; }   // ogon
        if (v == 1) f.pat2 = C(26, 26, 30);                               // showa: black too
        if (v == 3) f.patDensity = 0.15f;                                 // tancho: one red spot
        strcpy(f.familyName, "Koi");
        snprintf(f.name, sizeof f.name, "%s Koi", F.adj[v]);
    }
    // the rare and the dreamlike
    float lenK = tier == TIER_COMMON ? 0.f : tier == TIER_UNCOMMON ? 0.35f : tier == TIER_RARE ? 0.65f : 0.9f;
    f.lengthCm = F.lenMin + (F.lenMax - F.lenMin) * clampf(lenK + (h01(seed ^ 99u) - 0.5f) * 0.3f, 0.f, 1.f);
    if (water == WATER_CREEK || water == WATER_PADDY) f.lengthCm *= 0.85f;
    float Lm = f.lengthCm / 100.f;
    f.weightKg = F.wK * Lm * Lm * Lm * (f.depth / 0.24f) * 0.9f + 0.01f;
    if (tier == TIER_LEGEND && f.family != F_MOONFIN) {
        f.shimmer = true;
        static const FCol DREAM[4][2] = {{{120, 200, 255}, {200, 140, 255}}, {{255, 150, 90}, {255, 220, 120}}, {{130, 255, 200}, {120, 160, 255}}, {{255, 140, 200}, {170, 200, 255}}};
        uint32_t d = hsh(seed ^ 0xD8u) % 4u;
        f.back = mixc(f.back, DREAM[d][1], 0.5f); f.flank = mixc(f.flank, DREAM[d][0], 0.45f); f.fin = mixc(f.fin, DREAM[d][0], 0.6f); f.finLong = 0.7f;
        static const char* DREAMADJ[4] = {"Aurora", "Ember", "Jadelight", "Starscale"};
        snprintf(f.name, sizeof f.name, "%s %s", DREAMADJ[d], F.name);
    }
    if (f.family == F_MOONFIN) f.shimmer = true;
    if (!f.name[0]) {
        const char* a = F.adj[hsh(seed ^ 0xA0u) % 4u];
        if (f.family == F_MOONFIN) snprintf(f.name, sizeof f.name, "Moonfin");
        else snprintf(f.name, sizeof f.name, "%s %s", a, F.name);
    }
    if (!f.familyName[0]) snprintf(f.familyName, sizeof f.familyName, "%s", F.name);
}

// ---------------- drawing ----------------
struct Canvas { uint16_t* buf; int w, h; };
static inline void px(Canvas& c, int x, int y, FCol col) { if ((unsigned)x < (unsigned)c.w && (unsigned)y < (unsigned)c.h) c.buf[y * c.w + x] = to565(col); }
static inline bool dither(int x, int y, float a) { static const uint8_t B[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}}; return a * 16.f > B[y & 3][x & 3] + 0.5f; }

static float bodyHalf(const FishSpec& f, float u, float Hmax) {          // half-depth along the body, u: 0 peduncle .. 1 snout
    if (u < 0.f || u > 1.f) return 0.f;
    float k = logf(0.5f) / logf(clampf(f.peakU, 0.2f, 0.8f));
    float w = powf(u, k);
    float p = 0.75f - 0.25f * f.blunt;
    float s = powf(sinf(3.14159265f * w), p);
    if (u > 0.9f) s *= 1.f - (1.f - f.blunt) * ((u - 0.9f) / 0.1f) * 0.35f;   // the snout narrows
    return Hmax * (f.pedun + (1.f - f.pedun) * s);
}

static float patternAt(const FishSpec& f, float u, float t, int x, int y, FCol& col) {   // returns blend 0..1
    uint32_t sd = f.seed;
    float sc = f.patScale;
    switch (f.pattern) {
        case PAT_SPOTS: case PAT_HALO_SPOTS: {
            float gu = u * 14.f / sc, gt = t * 5.f / sc;
            int iu = (int)floorf(gu), it = (int)floorf(gt);
            float best = 9.f; uint32_t bh = 0;
            for (int du = -1; du <= 1; du++) for (int dt = -1; dt <= 1; dt++) {
                uint32_t h = hsh((uint32_t)(iu + du) * 73856093u ^ (uint32_t)(it + dt) * 19349663u ^ sd);
                if (h01(h) > f.patDensity) continue;
                float cu = iu + du + 0.2f + 0.6f * h01(h ^ 1u), ct = it + dt + 0.2f + 0.6f * h01(h ^ 2u);
                float d = sqrtf((gu - cu) * (gu - cu) * 1.f + (gt - ct) * (gt - ct) * 1.6f);
                if (d < best) { best = d; bh = h; }
            }
            float r = 0.22f + 0.1f * h01(bh ^ 3u);
            if (best < r) { col = f.pat; return t > 0.15f && t < 0.85f ? 1.f : 0.f; }
            return 0.f;
        }
        case PAT_VERMIC: {                                               // brook trout: worm marks on the back, haloed red spots on the flank
            if (t < 0.35f) { float n = sinf(u * 50.f / sc + sinf(t * 30.f) * 2.f) * sinf(t * 40.f / sc + u * 9.f); if (n > 0.55f) { col = f.pat; return 1.f; } return 0.f; }
            float gu = u * 12.f, gt = t * 4.f;
            int iu = (int)floorf(gu), it = (int)floorf(gt);
            uint32_t h = hsh((uint32_t)iu * 73856093u ^ (uint32_t)it * 19349663u ^ sd);
            float cu = iu + 0.3f + 0.4f * h01(h ^ 1u), ct = it + 0.3f + 0.4f * h01(h ^ 2u);
            float d = sqrtf((gu - cu) * (gu - cu) + (gt - ct) * (gt - ct) * 1.6f);
            if (t < 0.75f && h01(h) < 0.55f) {
                if (d < 0.16f) { col = f.pat2; return 1.f; }
                if (d < 0.27f) { col = C(120, 170, 230); return 1.f; }   // the blue halo
            }
            if (t < 0.75f && h01(h ^ 9u) < 0.5f && d > 0.3f && d < 0.4f) { col = f.pat; return 0.7f; }
            return 0.f;
        }
        case PAT_BARS: { float b = sinf(u * 6.f * 3.14159f / sc + 0.5f); if (b > 0.45f && t < 0.75f) { col = f.pat; return clampf((0.75f - t) * 3.f, 0.f, 0.85f); } return 0.f; }
        case PAT_STRIPE: {                                               // a pink band along the flank, black speckles above
            float d = fabsf(t - 0.5f);
            if (d < 0.09f) { col = f.pat2; return 0.85f - d * 4.f; }
            if (t < 0.5f || u < 0.25f) { uint32_t h = hsh((uint32_t)(x / 2) * 0x9E37u ^ (uint32_t)(y / 2) * 0x85EBu ^ sd); if ((h & 31u) < (uint32_t)(4.f * f.patDensity)) { col = f.pat; return 1.f; } }
            return 0.f;
        }
        case PAT_WAVY: { if (t > 0.45f) return 0.f; float w = sinf(u * 40.f / sc + sinf(t * 18.f) * 2.5f); if (w > 0.4f) { col = f.pat; return 0.9f; } return 0.f; }
        case PAT_MARBLE: { float n = sinf(u * 22.f / sc + sinf(t * 9.f + u * 5.f) * 2.f) + sinf(t * 17.f / sc - u * 7.f); if (n > 0.9f - f.patDensity) { col = (n > 1.4f) ? f.pat2 : f.pat; return 0.8f; } return 0.f; }
        case PAT_SPECKLE: { uint32_t h = hsh((uint32_t)x * 0x9E37u ^ (uint32_t)y * 0x85EBu ^ sd); if ((h & 63u) < (uint32_t)(6.f * f.patDensity) && t < 0.8f) { col = f.pat; return 0.9f; } return 0.f; }
        case PAT_PARR: { if (u < 0.45f) { float d = fabsf(t - 0.5f); if (d < 0.3f) { col = f.pat; return clampf((0.45f - u) * 3.f, 0.f, 0.9f); } } return 0.f; }   // arapaima: red toward the tail
        case PAT_BLOTCH_STRIPE: { float d = fabsf(t - 0.5f + 0.04f * sinf(u * 30.f)); if (d < 0.08f + 0.04f * sinf(u * 13.f + 1.f)) { col = f.pat; return 0.85f; } return 0.f; }
        case PAT_OCELLUS: {
            float b = sinf(u * 3.f * 3.14159f + 0.3f); if (b > 0.7f && t < 0.8f && u > 0.2f) { col = f.pat; return 0.8f; }
            return 0.f;
        }
        case PAT_KOI: {
            float n = sinf(u * 9.f + h01(sd) * 6.f) * sinf(t * 6.f + u * 4.f + h01(sd ^ 5u) * 6.f) + sinf(u * 4.f - t * 3.f + h01(sd ^ 7u) * 6.f) * 0.6f;
            if (f.patDensity < 0.3f) { float du = u - 0.88f, dt = t - 0.25f; if (du * du * 4.f + dt * dt < 0.02f) { col = f.pat; return 1.f; } return 0.f; }   // tancho
            if (n > 0.35f && t < 0.8f) { col = f.pat; return 1.f; }
            if (f.pat2.r < 60.f && n < -0.75f && t < 0.75f) { col = f.pat2; return 1.f; }
            return 0.f;
        }
    }
    return 0.f;
}

void fishRender(const FishSpec& f, uint16_t* buf, int bw, int bh, int cx, int cy, int lenPx) {
    Canvas cv = {buf, bw, bh};
    float L = (float)lenPx;
    float tailL = L * f.tailLen;
    float bodyL = L - tailL;
    float x0 = cx - L * 0.5f;                       // tail tip
    float xb = x0 + tailL;                          // peduncle
    float Hmax = bodyL * f.depth * 0.5f;
    float pedH = Hmax * f.pedun;
    FCol outline = scalec(mixc(f.back, C(20, 24, 60), 0.4f), 0.45f);
    FCol finDark = mixc(scalec(f.fin, 0.55f), C(30, 30, 70), 0.3f);
    auto topAt = [&](float u) { float h = bodyHalf(f, u, Hmax); return cy - h * (1.f + f.hump * sinf(3.14159f * u)); };
    auto botAt = [&](float u) { float h = bodyHalf(f, u, Hmax); return cy + h * f.belly; };
    auto xOf = [&](float u) { return xb + u * bodyL; };
    // ---- fins behind the body: dorsal(s), adipose, anal, pelvic, tail ----
    auto fin = [&](float us, float ue, float hgt, bool top, uint8_t spiny, uint8_t sail) {
        if (hgt <= 0.f) return;
        int xs = (int)xOf(us), xe = (int)xOf(ue);
        for (int x = xs; x <= xe; x++) {
            float w = (x - xs) / fmaxf(1.f, (float)(xe - xs));
            float prof = sail ? (0.6f + 0.4f * sinf(w * 3.14159f * 0.9f)) : (w < 0.3f ? w / 0.3f : 1.f - (w - 0.3f) / 0.7f * 0.75f);
            if (spiny) prof *= 0.75f + 0.25f * fabsf(sinf(w * 3.14159f * 7.f));
            float hh = hgt * 1.35f * Hmax * prof * (1.f + 0.5f * f.finLong * w);
            float base = top ? topAt((x - xb) / bodyL) + 1.f : botAt((x - xb) / bodyL) - 1.f;
            for (int k = 0; k < (int)hh; k++) {
                int y = top ? (int)(base - k) : (int)(base + k);
                float e = k / fmaxf(1.f, hh);
                bool ray = ((x - xs) % 3) == 0;
                FCol c = mixc(f.fin, finDark, ray ? 0.45f : 0.f);
                c = shade(c, (top ? 1.1f : 0.9f) - e * 0.15f + (e > 0.7f ? 0.12f : 0.f));   // the thin outer edge lets light through
                if (f.finEdge && e > 0.8f) c = C(246, 246, 240);
                px(cv, x, y, c);
            }
        }
    };
    fin(f.d1s, f.d1e, f.d1h, true, f.d1spiny, f.d1sail);
    fin(f.d2s, f.d2e, f.d2h, true, 0, 0);
    if (f.adipose) fin(0.12f, 0.18f, 0.22f, true, 0, 0);
    fin(0.12f, 0.3f, f.analH, false, 0, 0);
    fin(0.46f, 0.54f, 0.35f, false, 0, 0);
    // the tail
    {
        float spreadH = Hmax * f.tailSpread * (1.f + 0.4f * f.finLong);
        for (int x = (int)x0; x <= (int)xb; x++) {
            float fr = (xb - x) / fmaxf(1.f, tailL);              // 0 at the peduncle, 1 at the tip
            float env;
            switch (f.tailType) {
                case 2: env = pedH + (spreadH * 0.9f - pedH) * sinf(fminf(1.f, fr * 1.1f) * 1.5708f) * (fr > 0.7f ? sqrtf(fmaxf(0.f, 1.f - (fr - 0.7f) / 0.3f * (fr - 0.7f) / 0.3f)) : 1.f); break;
                case 3: env = pedH + (spreadH * 1.1f - pedH) * powf(fr, 0.6f); break;
                default: env = pedH + (spreadH - pedH) * powf(fr, 0.7f); break;
            }
            for (int y = (int)(cy - env); y <= (int)(cy + env); y++) {
                float v = (y - cy) / fmaxf(1.f, env);
                float notch = f.tailType == 0 ? f.tailFork * fr * fr : f.tailType == 3 ? 0.55f * fr * fr : (f.tailType == 1 && fr > 0.88f) ? 0.12f : 0.f;
                if (fabsf(v) < notch) continue;
                bool ray = (((int)(v * 7.f + 20.f)) % 2) == 0 && fr > 0.12f;
                FCol c = mixc(f.fin, finDark, ray ? 0.35f : 0.f);
                c = shade(c, 1.05f - fabsf(v) * 0.2f + (v < 0.f ? 0.1f : -0.08f) + (fr > 0.8f ? 0.1f : 0.f));
                if (f.finEdge && (fabsf(v) > 0.88f || fr > 0.95f)) c = C(244, 244, 238);
                if (f.pattern == PAT_OCELLUS) {                    // the peacock's eye on its tail
                    float ox = (fr - 0.3f) * tailL, oy = (y - cy);
                    float d = sqrtf(ox * ox + oy * oy), r = pedH * 0.9f;
                    if (d < r) c = d < r * 0.55f ? C(20, 20, 26) : C(250, 170, 40);
                }
                px(cv, x, y, c);
            }
        }
    }
    const float ug_c = 0.79f - 0.04f * f.blunt;
    // ---- the body ----
    for (int x = (int)xb; x <= (int)(x0 + L); x++) {
        float u = (x - xb) / bodyL;
        float yt = topAt(u), yb = botAt(u);
        if (yb - yt < 1.f) continue;
        for (int y = (int)ceilf(yt); y <= (int)yb; y++) {
            float t = (y - yt) / fmaxf(1.f, yb - yt);            // 0 back .. 1 belly
            FCol base = t < 0.42f ? mixc(f.back, f.flank, clampf(t / 0.42f, 0.f, 1.f)) : mixc(f.flank, f.belly2, clampf((t - 0.55f) / 0.35f, 0.f, 1.f));
            FCol pc; float pa = patternAt(f, u, t, x, y, pc);
            if (pa > 0.f && u < 0.83f) base = mixc(base, pc, pa);
            else if (pa > 0.f && f.pattern == PAT_KOI) base = mixc(base, pc, pa);
            float light = 1.18f - t * 0.55f;                          // lit from above
            if (t > 0.82f) light -= (t - 0.82f) * 1.6f;               // the underside turns away into shadow
            if (t < 0.08f) light += 0.18f;                            // a rim of light along the back
            light += 0.3f * expf(-((t - 0.26f) / 0.06f) * ((t - 0.26f) / 0.06f));   // the sheen
            if (u > ug_c - 0.02f && u > 0.8f) light += 0.04f;         // the head catches a little more
            if (f.scales > 0.f && t > 0.2f && t < 0.85f && u < 0.78f) {   // scales: a faint diamond lattice
                int s = (int)fmaxf(3.f, Hmax * 0.18f);
                if (((x + y) % s == 0) || ((x - y + 1000) % s == 0)) light -= 0.07f * f.scales;
            }
            if (f.shimmer) {
                float ir = sinf(u * 12.f + t * 5.f + (float)(f.seed & 255));
                base = mixc(base, C(160 + 80 * ir, 200 - 40 * ir, 255), 0.18f);
                if (((x * 7 + y * 13 + (int)f.seed) & 63) == 0) { px(cv, x, y, C(250, 255, 255)); continue; }
            }
            FCol c = shade(base, light);
            if (y <= (int)ceilf(yt) || y >= (int)yb) c = outline;
            px(cv, x, y, c);
        }
    }
    // the snout's outline, the gill plate, the mouth, the eye, the pectoral fin
    float ug = 0.79f - 0.04f * f.blunt;
    for (int k = 0; k < 40; k++) {
        float s = k / 39.f;
        float u = ug + sinf(s * 3.14159f) * 0.05f;
        float yt = topAt(u), yb = botAt(u);
        float y = yt + (yb - yt) * (0.12f + 0.78f * s);
        px(cv, (int)xOf(u), (int)y, scalec(mixc(f.flank, f.back, 0.4f), 0.62f));
    }
    {
        float ym = topAt(0.97f) + (botAt(0.97f) - topAt(0.97f)) * (0.55f - 0.1f * f.blunt);
        int mx = (int)xOf(1.f), my = (int)ym;
        int ml = (int)fmaxf(2.f, Hmax * (0.35f + 0.3f * f.blunt));
        for (int k = 0; k < ml; k++) px(cv, mx - k, my + k / 3, outline);
        if (f.barbels) for (int b = 0; b < 2; b++) for (int k = 0; k < (int)(Hmax * 0.9f); k++) px(cv, mx - 2 - k * (b ? 1 : 0) / 2, my + 1 + k, mixc(outline, f.flank, 0.4f));
    }
    {
        float ue = 0.9f - 0.03f * f.blunt;
        float ex = xOf(ue), ey = topAt(ue) + (botAt(ue) - topAt(ue)) * 0.32f;
        float re = fmaxf(1.8f, Hmax * 0.2f);
        for (int dy = -(int)re - 1; dy <= (int)re + 1; dy++)
            for (int dx = -(int)re - 1; dx <= (int)re + 1; dx++) {
                float d = sqrtf((float)(dx * dx + dy * dy));
                if (d > re + 0.5f) continue;
                FCol c = d > re - 0.6f ? outline : d > re * 0.55f ? shade(f.eye, 1.05f - dy / re * 0.2f) : C(12, 12, 20);
                px(cv, (int)ex + dx, (int)ey + dy, c);
            }
        px(cv, (int)(ex - re * 0.3f), (int)(ey - re * 0.35f), C(255, 255, 255));
    }
    // one crisp outline around everything
    {
        int bx0 = (int)fmaxf(0.f, x0 - 2), bx1 = (int)fminf((float)bw - 1, x0 + L + 2);
        int by0 = (int)fmaxf(0.f, cy - Hmax * 2.6f), by1 = (int)fminf((float)bh - 1, cy + Hmax * 2.6f);
        uint16_t oc = to565(outline);
        for (int y = by0; y <= by1; y++)
            for (int x = bx0; x <= bx1; x++) {
                if (buf[y * bw + x] == 0) continue;
                bool edge = (x == 0 || buf[y * bw + x - 1] == 0) || (x == bw - 1 || buf[y * bw + x + 1] == 0) ||
                            (y == 0 || buf[(y - 1) * bw + x] == 0) || (y == bh - 1 || buf[(y + 1) * bw + x] == 0);
                if (edge) buf[y * bw + x] = 0x0001;              // mark, then paint (so marks don't spread)
            }
        for (int y = by0; y <= by1; y++) for (int x = bx0; x <= bx1; x++) if (buf[y * bw + x] == 0x0001) buf[y * bw + x] = oc;
    }
    {
        float up = 0.74f;
        float pxx = xOf(up), pyy = topAt(up) + (botAt(up) - topAt(up)) * 0.62f;
        float pl = Hmax * (0.75f + 0.3f * f.finLong), ph = Hmax * 0.28f;
        for (int k = 0; k < (int)pl; k++) {
            float s = k / fmaxf(1.f, pl);
            int hh = (int)(ph * sinf(s * 3.14159f * 0.85f + 0.4f));
            for (int j = -hh / 2; j <= hh / 2; j++) {
                int x = (int)(pxx - k), y = (int)(pyy + s * ph * 0.8f + j);
                FCol c = shade(mixc(f.fin, f.flank, 0.35f), 1.15f - s * 0.25f);
                if (j == hh / 2 || j == -hh / 2) c = shade(f.fin, 0.8f);
                if (f.finEdge && s > 0.85f) c = C(244, 244, 238);
                px(cv, x, y, c);
            }
        }
    }
}

}
