#include "journey.h"
#include "universe.h"
#include "mem.h"
#include <math.h>
#include <string.h>

namespace mantis {

// ---------------- the biomes ----------------
static BiomeParams* DEF = nullptr;            // runtime buffer (internal RAM: biomeAt is a hot path)

static void ramp(float (&r)[4][3], const int (&c)[4][3]) { for (int i = 0; i < 4; i++) for (int k = 0; k < 3; k++) r[i][k] = (float)c[i][k]; }
static const int LEAF_STD[4][3]  = {{14, 44, 50}, {36, 92, 48}, {112, 166, 58}, {200, 218, 96}};
static const int GRASS_STD[4][3] = {{22, 64, 56}, {62, 118, 46}, {132, 180, 58}, {196, 214, 92}};
static const int STONE_STD[4][3] = {{50, 50, 72}, {106, 102, 112}, {164, 158, 146}, {214, 206, 184}};
static const int BARK_STD[4][3]  = {{46, 26, 40}, {98, 58, 40}, {170, 104, 54}, {226, 164, 92}};
static const int ROCK_STD[4][3]  = {{64, 52, 100}, {116, 92, 124}, {190, 128, 112}, {226, 170, 140}};

static void defineBiomes() {
    if (DEF) return;
    DEF = (BiomeParams*)memFast(sizeof(BiomeParams) * B_ALL_N);
    if (!DEF) return;
    for (int i = 0; i < B_ALL_N; i++) {
        BiomeParams& b = DEF[i];
        memset(&b, 0, sizeof(b));
        b.forestVar = 0.25f; b.flowers = 1.f; b.squirrels = 0.5f;
        ramp(b.leaf, LEAF_STD); ramp(b.grass, GRASS_STD); ramp(b.stone, STONE_STD); ramp(b.bark, BARK_STD); ramp(b.rock, ROCK_STD);
    }
    BiomeParams* b;
    b = &DEF[B_MEADOW];   b->forest = 0.12f; b->pine = 0.2f; b->lakes = 0.3f; b->creeks = 0.5f; b->rocks = 0.15f; b->squirrels = 0.2f;
    b = &DEF[B_FOREST];   b->forest = 0.8f; b->pine = 0.5f; b->lakes = 0.3f; b->creeks = 0.45f; b->rocks = 0.25f; b->wet = 0.05f; b->flowers = 0.6f; b->mist = 0.05f; b->squirrels = 1.f;
    b = &DEF[B_LAKES];    b->forest = 0.4f; b->forestVar = 0.35f; b->pine = 0.4f; b->lakes = 1.f; b->creeks = 0.3f; b->rocks = 0.2f; b->mist = 0.1f; b->wet = 0.05f; b->flowers = 0.8f;
    b = &DEF[B_HIGHLAND]; b->forest = 0.05f; b->forestVar = 0.1f; b->pine = 0.5f; b->lakes = 0.15f; b->creeks = 0.3f; b->rocks = 0.6f; b->ruins = 0.85f;
                          b->mist = 0.1f; b->wet = 0.05f; b->flowers = 1.6f; b->heather = 1.f; b->wind = 0.25f; b->squirrels = 0.f;
        { const int L[4][3] = {{20, 46, 46}, {52, 92, 50}, {120, 156, 64}, {196, 206, 104}}; ramp(b->leaf, L); }
        { const int G[4][3] = {{40, 62, 50}, {98, 118, 52}, {168, 170, 80}, {214, 206, 120}}; ramp(b->grass, G); }
        { const int S[4][3] = {{58, 58, 70}, {112, 110, 114}, {170, 166, 156}, {218, 212, 196}}; ramp(b->stone, S); }
    b = &DEF[B_FALLS];    b->forest = 0.85f; b->forestVar = 0.15f; b->pine = 0.88f; b->tall = 1.f; b->lakes = 0.35f; b->creeks = 0.9f; b->rocks = 0.6f; b->falls = 0.65f;
                          b->mist = 0.3f; b->wet = 0.12f; b->flowers = 0.4f; b->squirrels = 0.8f;
        { const int L[4][3] = {{10, 38, 44}, {28, 80, 52}, {80, 136, 62}, {150, 190, 90}}; ramp(b->leaf, L); }
        { const int G[4][3] = {{18, 58, 52}, {48, 104, 54}, {104, 156, 70}, {164, 200, 104}}; ramp(b->grass, G); }
        { const int S[4][3] = {{40, 48, 56}, {88, 98, 96}, {140, 150, 130}, {190, 196, 170}}; ramp(b->stone, S); }
    // autumn woodland: amber, crimson and gold; leaves drifting down; crows; squirrels busy everywhere
    b = &DEF[B_AUTUMN];   b->forest = 0.72f; b->pine = 0.15f; b->lakes = 0.25f; b->creeks = 0.4f; b->rocks = 0.2f; b->mist = 0.12f; b->flowers = 0.3f;
                          b->fallLeaves = 1.f; b->crows = 1.f; b->squirrels = 1.6f;
        { const int L[4][3] = {{70, 24, 30}, {168, 60, 32}, {226, 136, 44}, {246, 206, 96}}; ramp(b->leaf, L); }
        { const int G[4][3] = {{56, 44, 40}, {120, 100, 52}, {178, 150, 70}, {222, 196, 112}}; ramp(b->grass, G); }
    // marsh: reeds, lily pads, still water and creeks, frog chorus, herons and dragonflies, mist
    b = &DEF[B_MARSH];    b->forest = 0.1f; b->forestVar = 0.2f; b->pine = 0.2f; b->lakes = 1.2f; b->creeks = 0.8f; b->rocks = 0.05f; b->mist = 0.35f; b->wet = 0.1f;
                          b->flowers = 0.7f; b->reeds = 1.f; b->lily = 1.f; b->frogs = 1.f; b->heron = 1.f; b->dragonfly = 1.f; b->squirrels = 0.f;
        { const int G[4][3] = {{26, 56, 48}, {70, 104, 50}, {130, 150, 64}, {184, 192, 96}}; ramp(b->grass, G); }
    // birch and fern grove: white trunks, ferns, bright dappled light, the cuckoo
    b = &DEF[B_BIRCH];    b->forest = 0.7f; b->pine = 0.08f; b->lakes = 0.2f; b->creeks = 0.4f; b->rocks = 0.1f; b->flowers = 0.9f; b->cuckoo = 1.f; b->squirrels = 0.8f;
        { const int L[4][3] = {{24, 60, 40}, {70, 124, 52}, {150, 196, 76}, {216, 236, 128}}; ramp(b->leaf, L); }
        { const int K[4][3] = {{40, 38, 44}, {150, 146, 140}, {214, 210, 200}, {244, 242, 236}}; ramp(b->bark, K); }
    // snowy taiga (north): snow on the ground and the firs, frozen lakes, snowfall, white hares and caribou
    b = &DEF[B_TAIGA];    b->forest = 0.55f; b->pine = 0.95f; b->tall = 0.4f; b->lakes = 0.4f; b->creeks = 0.2f; b->rocks = 0.35f; b->mist = 0.15f; b->wet = 0.1f;
                          b->flowers = 0.f; b->snow = 1.f; b->wind = 0.2f; b->hare = 1.f; b->squirrels = 0.4f;
        { const int G[4][3] = {{120, 136, 170}, {186, 200, 224}, {226, 234, 246}, {250, 252, 255}}; ramp(b->grass, G); }
        { const int L[4][3] = {{10, 34, 42}, {24, 66, 54}, {60, 104, 70}, {120, 150, 110}}; ramp(b->leaf, L); }
    // the Japanese garden (east, toward the sunrise): maples, cherries, black pines, bamboo, moss and gravel,
    // koi ponds, stone lanterns, a torii now and then, cranes and sika deer, a temple bell at sunset
    b = &DEF[B_JAPAN];    b->forest = 0.45f; b->forestVar = 0.3f; b->pine = 0.3f; b->lakes = 0.75f; b->creeks = 0.5f; b->rocks = 0.35f;
                          b->mist = 0.12f; b->flowers = 0.3f; b->japan = 1.f; b->petals = 1.f; b->squirrels = 0.3f;
        { const int L[4][3] = {{64, 16, 28}, {160, 40, 36}, {222, 92, 48}, {248, 172, 96}}; ramp(b->leaf, L); }     // momiji
        { const int G[4][3] = {{20, 56, 46}, {52, 104, 52}, {110, 156, 70}, {172, 204, 110}}; ramp(b->grass, G); }  // moss
        { const int S[4][3] = {{56, 58, 66}, {118, 118, 116}, {176, 174, 164}, {222, 220, 210}}; ramp(b->stone, S); }
        { const int K[4][3] = {{36, 26, 30}, {78, 54, 44}, {130, 92, 62}, {186, 140, 96}}; ramp(b->bark, K); }
    // the red rock canyon (southwest): dry. Mesas on the horizon, sandstone, hoodoos and arches, cacti and sage,
    // no lakes and hardly a creek; lizards, jackrabbits, mule deer, a roadrunner, a hawk, a coyote at night
    b = &DEF[B_CANYON];   b->forest = 0.f; b->forestVar = 0.f; b->pine = 0.f; b->lakes = -1.5f; b->creeks = -1.f; b->rocks = 0.8f;
                          b->mist = 0.f; b->wet = -0.3f; b->flowers = 0.35f; b->wind = 0.15f; b->squirrels = 0.f;
                          b->desert = 1.f; b->mesa = 1.f; b->starBoost = 1.f;
        { const int G[4][3] = {{92, 48, 40}, {168, 96, 58}, {214, 150, 92}, {240, 204, 150}}; ramp(b->grass, G); }      // sand and red dirt
        { const int S[4][3] = {{90, 36, 34}, {170, 74, 48}, {214, 124, 74}, {240, 184, 126}}; ramp(b->stone, S); }     // sandstone
        { const int L[4][3] = {{44, 58, 52}, {88, 110, 84}, {138, 156, 112}, {190, 200, 150}}; ramp(b->leaf, L); }      // sage
        { const int K[4][3] = {{58, 46, 44}, {110, 92, 80}, {160, 140, 120}, {206, 190, 170}}; ramp(b->bark, K); }
        { const int R[4][3] = {{80, 34, 44}, {160, 70, 58}, {216, 124, 82}, {240, 178, 122}}; ramp(b->rock, R); }      // red mesas
    // ancient redwoods (northwest): colossal red trunks, ferns, mossy logs, mist and the strongest light;
    // elk, owls, the varied thrush; at night the moss glows faintly
    b = &DEF[B_REDWOODS]; b->forest = 0.6f; b->forestVar = 0.15f; b->pine = 0.92f; b->lakes = 0.05f; b->creeks = 0.5f; b->rocks = 0.12f;
                          b->mist = 0.35f; b->wet = 0.1f; b->flowers = 0.15f; b->squirrels = 0.9f;
                          b->giant = 1.f; b->fern = 1.f; b->glowMoss = 1.f; b->hush = 1.f; b->shafts = 1.f;
        { const int L[4][3] = {{8, 32, 34}, {20, 66, 46}, {56, 112, 58}, {120, 164, 84}}; ramp(b->leaf, L); }
        { const int G[4][3] = {{14, 46, 40}, {38, 88, 48}, {86, 138, 60}, {150, 186, 92}}; ramp(b->grass, G); }
        { const int K[4][3] = {{40, 18, 20}, {110, 46, 34}, {168, 84, 52}, {214, 138, 92}}; ramp(b->bark, K); }
    // the sunset coast (west): dune grass and sea cliffs, then the open sea filling the west, where the sun sets;
    // surf, gulls, a whale at golden hour, a lighthouse at night. You stop at the water's edge.
    b = &DEF[B_COAST];    b->forest = 0.12f; b->forestVar = 0.2f; b->pine = 0.7f; b->lakes = -1.f; b->creeks = 0.2f; b->rocks = 0.55f;
                          b->mist = 0.15f; b->wet = 0.05f; b->flowers = 0.7f; b->wind = 0.3f; b->squirrels = 0.f; b->ocean = 1.f;
        { const int G[4][3] = {{60, 72, 56}, {120, 132, 80}, {180, 182, 118}, {224, 220, 170}}; ramp(b->grass, G); }
        { const int S[4][3] = {{40, 42, 52}, {84, 86, 94}, {136, 134, 132}, {188, 184, 172}}; ramp(b->stone, S); }
    // bamboo and karst (southeast): pillar mountains in layered mist, flooded terraces, bamboo, limestone pillars,
    // cranes and kingfishers, lanterns floating on the water at night
    b = &DEF[B_KARST];    b->forest = 0.35f; b->forestVar = 0.3f; b->pine = 0.1f; b->lakes = 0.2f; b->creeks = 0.4f; b->rocks = 0.45f;
                          b->mist = 0.45f; b->wet = 0.1f; b->flowers = 0.5f; b->karst = 1.f; b->paddies = 1.f; b->squirrels = 0.2f;
        { const int L[4][3] = {{16, 48, 44}, {44, 104, 56}, {120, 172, 72}, {200, 226, 120}}; ramp(b->leaf, L); }
        { const int G[4][3] = {{24, 62, 50}, {64, 116, 52}, {130, 172, 66}, {196, 218, 110}}; ramp(b->grass, G); }
        { const int S[4][3] = {{56, 62, 70}, {110, 116, 116}, {164, 168, 160}, {214, 216, 204}}; ramp(b->stone, S); }
        { const int R[4][3] = {{28, 44, 50}, {56, 80, 76}, {96, 122, 104}, {140, 164, 138}}; ramp(b->rock, R); }
    // the frozen tundra (north): treeless snow, walkable frozen lakes that sing, frozen falls, caribou herds,
    // arctic foxes, snowy owls, sun dogs, and the strongest aurora
    b = &DEF[B_TUNDRA];   b->forest = 0.02f; b->forestVar = 0.05f; b->pine = 0.9f; b->lakes = 0.7f; b->creeks = 0.05f; b->rocks = 0.5f; b->falls = 0.4f;
                          b->mist = 0.1f; b->wet = 0.05f; b->flowers = 0.f; b->wind = 0.45f; b->snow = 1.f; b->hare = 1.f; b->squirrels = 0.f;
                          b->auroraBoost = 1.f; b->sundogs = 1.f; b->tundra = 1.f;
        { const int G[4][3] = {{136, 154, 190}, {196, 210, 232}, {232, 240, 250}, {252, 254, 255}}; ramp(b->grass, G); }
        { const int L[4][3] = {{10, 34, 42}, {24, 66, 54}, {60, 104, 70}, {120, 150, 110}}; ramp(b->leaf, L); }
        { const int S[4][3] = {{56, 62, 80}, {104, 112, 128}, {160, 168, 180}, {214, 220, 230}}; ramp(b->stone, S); }
        { const int R[4][3] = {{60, 70, 110}, {110, 130, 170}, {176, 196, 222}, {226, 236, 248}}; ramp(b->rock, R); }
    // the rainforest (south): towering canopy, vines, big leaves, orchids, emerald pools and waterfalls,
    // parrots, blue morphos, tree frogs, howler monkeys, warm rain
    b = &DEF[B_RAINFOREST]; b->forest = 0.92f; b->forestVar = 0.1f; b->pine = 0.04f; b->lakes = 0.55f; b->creeks = 0.8f; b->rocks = 0.4f; b->falls = 0.55f;
                          b->mist = 0.4f; b->wet = 0.25f; b->flowers = 1.4f; b->frogs = 0.7f; b->squirrels = 0.f; b->tropical = 1.f;
        { const int L[4][3] = {{10, 40, 30}, {24, 98, 40}, {90, 170, 50}, {190, 232, 90}}; ramp(b->leaf, L); }
        { const int G[4][3] = {{16, 50, 34}, {40, 104, 44}, {96, 156, 52}, {170, 206, 86}}; ramp(b->grass, G); }
        { const int K[4][3] = {{40, 34, 34}, {90, 78, 64}, {146, 128, 100}, {196, 182, 150}}; ramp(b->bark, K); }
        { const int S[4][3] = {{30, 50, 46}, {70, 96, 84}, {120, 148, 120}, {176, 196, 160}}; ramp(b->stone, S); }
    // the moonlit grove (northeast): birch and aspen, silver mist, white flowers, bright moonlit nights,
    // owls, and very rarely the white stag
    b = &DEF[B_GROVE];    b->forest = 0.6f; b->forestVar = 0.2f; b->pine = 0.06f; b->lakes = 0.25f; b->creeks = 0.4f; b->rocks = 0.12f;
                          b->mist = 0.28f; b->flowers = 0.8f; b->hush = 0.4f; b->moonlight = 1.f; b->stag = 1.f; b->squirrels = 0.5f;
        { const int L[4][3] = {{20, 52, 48}, {70, 118, 80}, {150, 184, 112}, {222, 232, 170}}; ramp(b->leaf, L); }
        { const int G[4][3] = {{26, 60, 54}, {66, 110, 70}, {130, 164, 96}, {196, 210, 150}}; ramp(b->grass, G); }
        { const int K[4][3] = {{40, 38, 44}, {150, 146, 140}, {214, 210, 200}, {244, 242, 236}}; ramp(b->bark, K); }
    DEF[B_JAPAN].pondScale = 2.2f;                                 // garden ponds: small and round
}

// ---------------- the journey ----------------
static uint32_t s_gen = 1;
uint32_t journeyGeneration() { return s_gen; }
static void memoReset();
static constexpr int MAXSEG = 512;
static constexpr float BLEND = 60.f;            // metres over which one biome becomes the next
static uint32_t s_seed = 1;
static float s_start[MAXSEG + 1];
static uint8_t s_biome[MAXSEG];
static int s_cur = 0;
static float s_hx = 0.f, s_hy = 0.f, s_hlen = 0.f;   // heading accumulated through the current biome
static int s_streak = 0, s_streakOct = -1;

static inline float hashf01(uint32_t x) { return (hash32(x) >> 8) * (1.f / 16777216.f); }

static uint8_t pickOrdinary(int i, int octant, uint8_t avoid) {
    // the climate leans with your heading: north and west wetter and wilder, south and east more open
    float w[B_ORDINARY_N] = {1.f, 1.f, 0.8f, 0.8f, 0.7f, 0.8f, 0.6f, 0.7f, 0.f};   // snow only when you head north
    if (octant >= 0) {
        float ang = octant * 0.7853982f;
        float north = -sinf(ang), west = -cosf(ang);           // octant 0 = east; bearings grow toward the south
        w[B_FOREST] += 0.4f * north;
        w[B_FALLS] += 0.6f * fmaxf(0.f, north) + 0.6f * fmaxf(0.f, west);
        w[B_HIGHLAND] += 0.5f * fmaxf(0.f, -north) + 0.3f * fmaxf(0.f, west);
        w[B_MEADOW] += 0.5f * fmaxf(0.f, -north);
        w[B_LAKES] += 0.4f * fmaxf(0.f, -west);
        w[B_BIRCH] += 0.7f * fmaxf(0.f, north);
        w[B_TAIGA] += 1.4f * fmaxf(0.f, north - 0.3f);           // the cold only when you're really heading north
        w[B_AUTUMN] += 0.4f * fmaxf(0.f, west) + 0.3f * fmaxf(0.f, north);
        w[B_MARSH] += 0.5f * fmaxf(0.f, -west) + 0.3f * fmaxf(0.f, -north);
    }
    w[avoid] *= 0.15f;                                         // rarely the same twice in a row
    float sum = 0.f;
    for (int k = 0; k < B_ORDINARY_N; k++) sum += k == B_TAIGA ? w[k] : fmaxf(0.05f, w[k]);
    float r = hashf01(s_seed ^ (uint32_t)i * 0x9E3779B1u) * sum;
    for (int k = 0; k < B_ORDINARY_N; k++) { r -= k == B_TAIGA ? w[k] : fmaxf(0.05f, w[k]); if (r <= 0.f) return (uint8_t)k; }
    return B_MEADOW;
}

void journeyInit(uint32_t seed, float distance) {
    defineBiomes();
    memoReset();
    s_seed = hash32(seed ^ 0x10A2E7u);
    s_start[0] = distance - 50.f;
    for (int i = 0; i < MAXSEG; i++) s_start[i + 1] = s_start[i] + 900.f + hashf01(s_seed + (uint32_t)i * 7919u) * 600.f;
    s_biome[0] = hashf01(s_seed) < 0.5f ? B_MEADOW : B_FOREST;     // the dream begins somewhere gentle
    for (int i = 1; i < MAXSEG; i++) s_biome[i] = pickOrdinary(i, -1, s_biome[i - 1]);
    s_cur = 0;
    s_hx = s_hy = s_hlen = 0.f;
    s_streak = 0; s_streakOct = -1;
}

static int segAt(float D) {
    int i = s_cur;
    while (i > 0 && D < s_start[i]) i--;
    while (i < MAXSEG - 1 && D >= s_start[i + 1]) i++;
    return i;
}

static uint8_t specialFor(int oct) {
    switch (oct) {
        case 0: return B_JAPAN;                                // east, toward the sunrise
        case 1: return B_KARST;                                // southeast
        case 2: return B_RAINFOREST;                           // south
        case 3: return B_CANYON;                               // southwest
        case 4: return B_COAST;                                // west, into the sunset
        case 5: return B_REDWOODS;                             // northwest
        case 6: return B_TUNDRA;                               // north
        case 7: return B_GROVE;                                // northeast
        default: return 255;                                   // the other directions arrive in later phases
    }
}

void journeyUpdate(float distance, float yaw, float moved) {
    if (moved > 0.f) { s_hx += cosf(yaw) * moved; s_hy += sinf(yaw) * moved; s_hlen += moved; }
    int i = segAt(distance);
    // two biomes held on one heading, and still holding it well into the third: the next one is special
    if (s_streak >= 2 && s_cur + 1 < MAXSEG && s_biome[s_cur + 1] < B_ORDINARY_N && s_biome[s_cur] < B_ORDINARY_N) {
        float into = (distance - s_start[s_cur]) / (s_start[s_cur + 1] - s_start[s_cur]);
        float straight = s_hlen > 1.f ? sqrtf(s_hx * s_hx + s_hy * s_hy) / s_hlen : 0.f;
        if (into > 0.35f && into < 0.85f && straight > 0.45f) {
            int oct = ((int)floorf(atan2f(s_hy, s_hx) / 0.7853982f + 0.5f) % 8 + 8) % 8;
            uint8_t sp = specialFor(oct);
            if (sp != 255 && ((oct - s_streakOct + 9) % 8) <= 2 && s_biome[s_cur + 1] != sp) { s_biome[s_cur + 1] = sp; s_gen++; }
        }
    }
    while (s_cur < i) {                                       // we've crossed into the next biome
        // how straight was the heading through the biome we just left?
        float straight = s_hlen > 1.f ? sqrtf(s_hx * s_hx + s_hy * s_hy) / s_hlen : 0.f;
        int oct = -1;
        if (straight > 0.45f) {
            float h = atan2f(s_hy, s_hx);
            oct = ((int)floorf(h / 0.7853982f + 0.5f) % 8 + 8) % 8;
        }
        if (oct >= 0 && (s_streakOct < 0 || ((oct - s_streakOct + 9) % 8) <= 2)) { s_streak++; s_streakOct = oct; }
        else { s_streak = oct >= 0 ? 1 : 0; s_streakOct = oct; }
        s_cur++;
        // after a special, back to a random ordinary biome; otherwise the next leans with your heading
        if (s_biome[s_cur] >= B_ORDINARY_N) { s_streak = 0; s_streakOct = -1; }
        s_gen++;
        if (s_cur + 1 < MAXSEG) s_biome[s_cur + 1] = pickOrdinary(s_cur + 1, s_streakOct, s_biome[s_cur] < B_ORDINARY_N ? s_biome[s_cur] : (uint8_t)B_MEADOW);
        s_hx = s_hy = s_hlen = 0.f;
    }
}

static void lerpParams(BiomeParams& o, const BiomeParams& a, const BiomeParams& b, float t) {
    const float* pa = &a.forest; const float* pb = &b.forest; float* po = &o.forest;
    const int n = (int)(sizeof(BiomeParams) / sizeof(float));
    for (int k = 0; k < n; k++) po[k] = pa[k] + (pb[k] - pa[k]) * t;
}

// Near a border every field lookup blends two biomes (~110 numbers). Thousands of lookups a frame land
// on the same few spots, so the last blends are remembered (the blend is quantised to 1/128: invisible).
struct BlendMemo { int a, b, q; BiomeParams p; };
static constexpr int MEMO_N = 8;
static BlendMemo* s_memo = nullptr;            // runtime buffer (internal RAM)
float* journeyRampSlot(int biome, const char* ramp, int shade) {
    if (!DEF) defineBiomes();
    if (!DEF || biome < 0 || biome >= B_ALL_N || shade < 0 || shade > 3) return nullptr;
    BiomeParams& b = DEF[biome];
    if (!strcmp(ramp, "leaf")) return b.leaf[shade];
    if (!strcmp(ramp, "grass")) return b.grass[shade];
    if (!strcmp(ramp, "stone")) return b.stone[shade];
    if (!strcmp(ramp, "bark")) return b.bark[shade];
    if (!strcmp(ramp, "rock")) return b.rock[shade];
    return nullptr;
}
void journeyColoursChanged() { memoReset(); }
static void memoReset() {
    if (!s_memo) s_memo = (BlendMemo*)memFast(sizeof(BlendMemo) * MEMO_N);
    if (s_memo) for (int i = 0; i < MEMO_N; i++) s_memo[i].q = -1;
    s_gen++;
}

static int s_memoNext = 0;
static const BiomeParams& blendOf(uint8_t ba, uint8_t bb, float t) {
    int q = (int)(t * 128.f + 0.5f);
    if (!s_memo) memoReset();
    for (int i = 0; i < MEMO_N; i++) { BlendMemo& m = s_memo[i]; if (m.q == q && m.a == ba && m.b == bb) return m.p; }
    BlendMemo& m = s_memo[s_memoNext];
    s_memoNext = (s_memoNext + 1) & (MEMO_N - 1);
    float tq = q / 128.f;
    m.a = ba; m.b = bb; m.q = q;
    lerpParams(m.p, DEF[ba], DEF[bb], tq * tq * (3.f - 2.f * tq));
    return m.p;
}

const BiomeParams& biomeAt(float D) {
    if (!DEF) defineBiomes();
    int i = segAt(D);
    float into = D - s_start[i], left = s_start[i + 1] - D;
    if (left < BLEND * 0.5f && i + 1 < MAXSEG) return blendOf(s_biome[i], s_biome[i + 1], 0.5f - left / BLEND);   // approaching the border
    if (into < BLEND * 0.5f && i > 0) return blendOf(s_biome[i - 1], s_biome[i], 0.5f + into / BLEND);             // just past it
    return DEF[s_biome[i]];
}

uint8_t journeyBiome() { return s_biome[s_cur]; }
uint8_t biomeIdAt(float D) { return s_biome[segAt(D)]; }
float biomeStart(float D) { return s_start[segAt(D)]; }
void journeyForce(float D, uint8_t b) { s_biome[segAt(D)] = b; }
int journeyStreak() { return s_streak; }
int journeyOctant() { return s_streakOct; }

}
