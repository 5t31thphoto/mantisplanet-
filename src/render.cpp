// MantisPlanet renderer
// 160x120 indexed framebuffer. Every pixel is a palette index; the palette is rebuilt each
// frame from the time of day, so day/night, fog and lighting are palette work, not pixel work.
// Layers back to front: sky -> night sky -> sun -> clouds -> far/mid mountains -> hill tree
// lines -> mist -> ground/lake reflection -> god rays -> near-field plants, rocks + critters.

#include "render.h"
#include "universe.h"
#include "field.h"
#include "scene.h"
#include "audio.h"
#include "journey.h"
#include "mem.h"
#include "creatures.h"
#include "fishing.h"
#include "bugs.h"
#include <math.h>
#include <string.h>
#ifdef HOST
#include <stdio.h>
#include <stdlib.h>
#else
#include <esp_heap_caps.h>
#endif

namespace mantis {

static constexpr int   W = FB_W, H = FB_H;
static constexpr int   HOR = 70;                 // horizon / waterline row
static constexpr float FOV = 1.10f;              // radians across the screen
static constexpr float PXR = W / FOV;            // pixels per radian
static constexpr float ZK = 1.4f * PXR;          // ground: rows below horizon = ZK / depth
static constexpr float PI_F = 3.14159265f;
static float SUN_AZ = 0.0f, MOON_AZ = 3.14159f;                 // follow the day: see renderFrame
static constexpr float PLANET_AZ = 4.0f;

// ---------------- palette layout: 16 ramps x 16, dark -> light ----------------
enum : uint8_t {
    R_SKY = 0, R_CLOUD = 16, R_SUN = 32, R_MTN_FAR = 48, R_MTN_NEAR = 64, R_HILLS = 80,
    R_GROUND = 96, R_LEAF = 112, R_BARK = 128, R_ACCENT = 144, R_WATER = 160
};
static constexpr int N_REFLECT = 96;   // ramps 0..5 have darkened water twins at R_WATER + i
// The creatures' extra 16 colours live where the far mountains' water twins were: far peaks reflect with the
// near mountains' twins instead. (So R_WATER.. is water, except this block.)
static constexpr int R_FAUNA = 160 + 48;  // R_WATER + R_MTN_FAR
static inline bool isWaterI(uint8_t p) { return p >= 160 && !(p >= R_FAUNA && p < R_FAUNA + 16); }
static uint8_t s_farTwin[16] = {224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239};   // far peak -> the near twin of matching brightness
static inline uint8_t reflIdx(uint8_t src) { return (uint8_t)(src >= 48 && src < 64 ? s_farTwin[src - 48] : 160 + src); }
// HILLS / LEAF: 0..7 near, 8..15 same shades fogged for distance.
// BARK: 0..7 bark, 8..15 stone (distant stone uses the near-mountain ramp).
// GROUND: (depth bucket 0 near..7 far) * 2 + variation.
enum : uint8_t {
    A_SIL, A_DEER_D, A_DEER, A_DEER_L, A_RAB, A_RAB_L, A_PINK_D, A_PINK, A_PINK_L,
    A_BLUE_D, A_BLUE, A_LAV, A_YEL, A_YEL_L, A_WHITE, A_BIRD
};

static uint8_t  fb[W * H];
static uint16_t pal565[256];
static uint8_t  palRGB[256 * 3];
static uint8_t  HW[17][17];

// Big scratch buffers live on the heap (internal RAM first, PSRAM if needed) to keep .bss small.
static void* allocScratch(size_t bytes) {
#ifdef HOST
    void* p = calloc(1, bytes);
#else
    void* p = heap_caps_calloc(1, bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
    return p;
}
static int      s_bob = 0;

static const uint8_t BAYER[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
// The view scrolls in whole pixels (g_pan = yaw on the pixel grid). Every dither and noise pattern is
// keyed to world columns (x + g_pan), so patterns travel with the scenery instead of crawling over it.
static int g_pan = 0;
static inline float dith(int x, int y) { return (BAYER[((y & 3) << 2) | ((x + g_pan) & 3)] + 0.5f) * (1.f / 16.f); }
static inline float ign(int x, int y) {     // interleaved gradient noise: organic, no lattice
    x += g_pan;
    float f = 0.06711056f * (x & 4095) + 0.00583715f * y;
    f = 52.9829189f * (f - floorf(f));
    return f - floorf(f);
}
// Pixel-art banding: clean bands with a thin checkered seam, instead of dithering whole gradients.
static inline int band(float f, int x, int y) {
    int i = (int)f;
    float fr = f - i;
    if (fr > 0.86f) return i + 1;
    if (fr > 0.62f && (((x + g_pan) ^ y) & 1)) return i + 1;
    return i;
}
static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline int   clampi(int v, int a, int b) { return v < a ? a : (v > b ? b : v); }
static inline float wrapPi(float a) { return a - kTau * floorf((a + PI_F) / kTau); }

static float g_alpha = 1.f;   // < 1: object dissolves through ordered dither
static inline void put(int x, int y, uint8_t c) {
    if ((unsigned)x < (unsigned)W && (unsigned)y < (unsigned)H) {
        if (g_alpha < 1.f && dith(x, y) >= g_alpha) return;
        fb[y * W + x] = c;
    }
}
static inline void hspan(int x0, int x1, int y, uint8_t c) {
    if ((unsigned)y >= (unsigned)H) return;
    if (x0 < 0) x0 = 0;
    if (x1 > W - 1) x1 = W - 1;
    if (x0 > x1) return;
    if (g_alpha >= 1.f) { memset(fb + y * W + x0, c, x1 - x0 + 1); return; }
    for (int x = x0; x <= x1; x++) if (dith(x, y) < g_alpha) fb[y * W + x] = c;
}
static inline void splat(int cx, int cy, int r, uint8_t c) {
    if (r > 16) r = 16;
    if (r <= 0) { put(cx, cy, c); return; }
    for (int dy = -r; dy <= r; dy++) {
        int hw = HW[r][dy < 0 ? -dy : dy];
        hspan(cx - hw, cx + hw, cy + dy, c);
    }
}
static void line(int x0, int y0, int x1, int y1, uint8_t c) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = -(y1 > y0 ? y1 - y0 : y0 - y1), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (int n = 0; n < 512; n++) {
        put(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
static inline float fastAtan2(float y, float x) {
    float ax = fabsf(x), ay = fabsf(y);
    float mx = ax > ay ? ax : ay, mn = ax > ay ? ay : ax;
    if (mx < 1e-6f) return 0.f;
    float a = mn / mx, s = a * a;
    float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    if (ay > ax) r = 1.57079637f - r;
    if (x < 0) r = 3.14159274f - r;
    if (y < 0) r = -r;
    return r;
}

// ---------------- colour / time of day ----------------
struct C3 { float r, g, b; };
static inline C3 mix(C3 a, C3 b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
static inline C3 scale(C3 a, float s) { return {a.r * s, a.g * s, a.b * s}; }
static inline C3 add(C3 a, C3 b) { return {a.r + b.r, a.g + b.g, a.b + b.b}; }
static inline C3 mul(C3 a, C3 b) { return {a.r * b.r / 255.f, a.g * b.g / 255.f, a.b * b.b / 255.f}; }

struct Key {
    float t;
    C3 zen, hor, glow, cDark, cLit, sun, amb, key, fog;
    float elev, night, mist;
};
#define NIGHT_KEY(T, E, M) {T, {6,5,20},{24,18,52},{40,24,70},{18,12,40},{95,55,150},{215,220,255},{70,75,120},{60,70,150},{26,22,55}, E, 1.0f, M}
static const Key KEYS[] = {
    NIGHT_KEY(0.00f, -0.40f, 0.25f),
    {0.19f, {18,14,50},{75,42,105},{160,75,115},{40,28,75},{190,105,150},{255,200,160},{105,92,140},{190,105,125},{85,58,115}, -0.06f, 0.55f, 0.55f},
    {0.26f, {58,48,128},{238,118,92},{255,188,112},{105,58,118},{255,168,110},{255,240,190},{160,130,140},{255,150,80},{195,118,128}, 0.07f, 0.0f, 0.60f},
    {0.36f, {70,120,210},{188,208,235},{255,240,200},{138,150,190},{255,250,240},{255,255,230},{220,220,230},{255,230,180},{168,192,222}, 0.42f, 0.0f, 0.25f},
    {0.50f, {40,100,215},{168,208,245},{240,248,255},{150,165,200},{255,255,255},{255,255,240},{255,255,255},{255,245,220},{160,195,235}, 0.80f, 0.0f, 0.10f},
    {0.63f, {62,90,190},{218,190,172},{255,220,150},{150,128,170},{255,230,190},{255,245,200},{230,215,205},{255,210,140},{190,170,182}, 0.36f, 0.0f, 0.15f},
    {0.71f, {82,50,142},{255,118,70},{255,200,90},{120,50,110},{255,148,90},{255,240,170},{170,115,128},{255,128,50},{190,90,120}, 0.06f, 0.0f, 0.35f},
    {0.79f, {35,22,75},{178,50,100},{255,110,70},{60,30,80},{220,90,125},{255,180,120},{105,78,115},{200,70,80},{108,45,95}, -0.08f, 0.35f, 0.45f},
    NIGHT_KEY(0.89f, -0.35f, 0.30f),
    NIGHT_KEY(1.00f, -0.40f, 0.25f),
};
static constexpr int NKEYS = sizeof(KEYS) / sizeof(KEYS[0]);
static const Key* g_keys = KEYS;            // a planet mod may point this at its own copy

static Key sampleKey(float tod) {
    int k = 0;
    while (k < NKEYS - 2 && tod >= g_keys[k + 1].t) k++;
    const Key &A = g_keys[k], &B = g_keys[k + 1];
    float t = clampf((tod - A.t) / (B.t - A.t), 0.f, 1.f);
    t = t * t * (3.f - 2.f * t);
    Key o;
    o.t = tod;
    o.zen = mix(A.zen, B.zen, t);   o.hor = mix(A.hor, B.hor, t);     o.glow = mix(A.glow, B.glow, t);
    o.cDark = mix(A.cDark, B.cDark, t); o.cLit = mix(A.cLit, B.cLit, t); o.sun = mix(A.sun, B.sun, t);
    o.amb = mix(A.amb, B.amb, t);   o.key = mix(A.key, B.key, t);     o.fog = mix(A.fog, B.fog, t);
    o.elev = A.elev + (B.elev - A.elev) * t;
    o.night = A.night + (B.night - A.night) * t;
    o.mist = A.mist + (B.mist - A.mist) * t;
    return o;
}

// ---------------- per-frame state ----------------
static Key   S;
static float g_yaw, g_dist, g_t;
// The eye sits CAM_L in front of the body's turning axis: turning sweeps it along a small arc, so
// near things slide across far ones. That parallax is what makes the screen feel like a window.
static constexpr float CAM_L = 0.4f;
static float g_camX = 0.f, g_camY = 0.f;
static float sunX, sunY, sunVis, lowSun;
static float colAng[W], sideCol[W], mistN[W];
static float hFar[W + 2], hMid[W + 2], hHF[W + 2], hHN[W + 2];
static int   xoffRow[H];

// The base colours: read-only defaults in flash, read through a pointer; a planet mod gets its own copy.
struct BaseCols { C3 rock, rock2, snow, hill, grass, leaf, bark, water, stone; };
static const BaseCols BASE_DEF = {{112, 98, 128}, {88, 80, 102}, {240, 240, 252}, {30, 66, 46}, {78, 130, 50}, {40, 102, 44}, {84, 56, 36}, {18, 40, 74}, {120, 118, 122}};
static const BaseCols* g_base = &BASE_DEF;
#define ROCK (g_base->rock)
#define ROCK2 (g_base->rock2)
#define SNOW (g_base->snow)
#define HILL (g_base->hill)
#define GRASS (g_base->grass)
#define LEAF (g_base->leaf)
#define BARK (g_base->bark)
#define WATER (g_base->water)
#define STONE (g_base->stone)
// Artist ramps: shadows lean cool (teal, violet), highlights lean warm (lime, gold).
struct Ramp4 { C3 k[4]; };
static const Ramp4 LEAF_R  = {{{14, 44, 50}, {36, 92, 48}, {112, 166, 58}, {200, 218, 96}}};
static const Ramp4 BARK_R  = {{{46, 26, 40}, {98, 58, 40}, {170, 104, 54}, {226, 164, 92}}};
static const Ramp4 GRASS_R = {{{22, 64, 56}, {62, 118, 46}, {132, 180, 58}, {196, 214, 92}}};
static const Ramp4 WETG_R  = {{{16, 50, 52}, {44, 96, 50}, {96, 150, 62}, {160, 196, 96}}};
static const Ramp4 STONE_R = {{{50, 50, 72}, {106, 102, 112}, {164, 158, 146}, {214, 206, 184}}};
static const Ramp4 HILL_R  = {{{16, 42, 50}, {30, 66, 50}, {58, 98, 58}, {96, 128, 70}}};
static const Ramp4 ROCK_R  = {{{64, 52, 100}, {116, 92, 124}, {190, 128, 112}, {226, 170, 140}}};
static const Ramp4 ROCK2_R = {{{50, 46, 80}, {96, 82, 108}, {160, 110, 100}, {200, 150, 124}}};
static bool s_flashOn = false;            // the flashlight (long-press B)
static bool s_flashWarm = false;          // this frame: dark enough that its warm ramp is in the palette
static float g_windLat = 0.f;             // the west wind's sideways push on screen (+ right), from your facing
static float g_objBearing = 0.f;          // bearing of the object being drawn (for north-side moss)
static bool s_alignOn = false;            // the alignment drag's diagnostic overlay
static float s_alignSum = 0.f;
bool g_noGroundCache = false;           // tests: render without the ground cache
static inline uint8_t leafI(int s, bool far);
static Ramp4 g_leafR, g_grassR, g_stoneR, g_wetR, g_barkR, g_rockR, g_rock2R;
static float g_desert = 0.f, g_mesa = 0.f, g_starBoost = 0.f, g_fern = 0.f, g_glowMoss = 0.f, g_shafts = 0.f, g_ocean = 0.f;
static float g_karst = 0.f, g_paddies = 0.f, g_auroraBoost = 0.f, g_sundogs = 0.f, g_tundra = 0.f, g_tropical = 0.f, g_moonlight = 0.f;
static float g_snow = 0.f, g_lily = 0.f, g_fallLeaves = 0.f, g_japan = 0.f, g_petals = 0.f;
static bool g_altFoliage = false;   // black pines draw from the deep-green hill ramp
static bool g_blossom = false;      // cherry trees: the canopy is blossom
static void biomeRamps() {
    const BiomeParams& b = biomeAt(g_dist);
    for (int k = 0; k < 4; k++) {
        g_leafR.k[k] = C3{b.leaf[k][0], b.leaf[k][1], b.leaf[k][2]};
        g_grassR.k[k] = C3{b.grass[k][0], b.grass[k][1], b.grass[k][2]};
        g_stoneR.k[k] = C3{b.stone[k][0], b.stone[k][1], b.stone[k][2]};
        g_wetR.k[k] = mix(g_grassR.k[k], C3{16, 50, 52}, 0.35f);
        g_barkR.k[k] = C3{b.bark[k][0], b.bark[k][1], b.bark[k][2]};
        g_rockR.k[k] = C3{b.rock[k][0], b.rock[k][1], b.rock[k][2]};
        g_rock2R.k[k] = scale(g_rockR.k[k], 0.8f);
    }
    g_desert = b.desert; g_mesa = b.mesa; g_starBoost = b.starBoost; g_fern = b.fern; g_glowMoss = b.glowMoss; g_shafts = b.shafts; g_ocean = b.ocean;
    g_moonlight = b.moonlight;
    if (g_moonlight > 0.f) S.amb = mix(S.amb, scale(add(S.amb, C3{0.10f, 0.12f, 0.18f}), 1.6f), g_moonlight * S.night);   // moonlit: you can see
    g_karst = b.karst; g_paddies = b.paddies; g_auroraBoost = b.auroraBoost; g_sundogs = b.sundogs; g_tundra = b.tundra; g_tropical = b.tropical;
    g_snow = b.snow; g_lily = b.lily; g_fallLeaves = b.fallLeaves; g_japan = b.japan; g_petals = b.petals;
    if (g_snow > 0.f) {                                   // snow settles on the tops of things
        const C3 SNOWC = {236, 242, 252};
        g_leafR.k[3] = mix(g_leafR.k[3], SNOWC, 0.85f * g_snow);
        g_leafR.k[2] = mix(g_leafR.k[2], SNOWC, 0.6f * g_snow);
        g_stoneR.k[3] = mix(g_stoneR.k[3], SNOWC, 0.8f * g_snow);
        g_wetR = g_grassR;
    }
}
static C3 rampAt(const Ramp4& r, float s) {
    s = clampf(s, 0.f, 1.f) * 3.f;
    int i = (int)s;
    if (i >= 3) return r.k[3];
    return mix(r.k[i], r.k[i + 1], s - i);
}
// The creatures' second 16 (see R_FAUNA): blacks and browns for hooves, eyes and coats; greys for foxes,
// herons and owls; rust, orange and red; deep and bright greens; deep and sky blues.
static const C3 AURORA_DEF[7] = {{150, 90, 230}, {80, 120, 255}, {70, 200, 235}, {90, 225, 110}, {250, 235, 90}, {255, 160, 60}, {240, 70, 60}};
static const C3 FAUNA_DEF[16] = {
    {22, 18, 16}, {58, 38, 26}, {96, 62, 38}, {150, 104, 62}, {206, 170, 122}, {238, 224, 194},
    {56, 58, 64}, {110, 112, 118}, {176, 178, 184}, {170, 72, 40}, {228, 132, 52}, {196, 40, 38},
    {38, 94, 50}, {112, 190, 72}, {40, 70, 160}, {92, 172, 222}};
static const C3 ACCENT_DEF[16] = {
    {14, 10, 22}, {70, 40, 22}, {130, 78, 40}, {195, 145, 95}, {120, 110, 100}, {190, 180, 165},
    {170, 40, 100}, {240, 90, 160}, {255, 175, 215}, {50, 60, 170}, {95, 135, 250}, {175, 135, 245},
    {240, 170, 40}, {255, 232, 120}, {240, 240, 235}, {30, 28, 40}};
static const C3* g_accent = ACCENT_DEF;      // read through pointers: a planet mod points these at its own copies
static const C3* g_fauna = FAUNA_DEF;
static const C3* g_aurora = AURORA_DEF;
#define ACCENT g_accent
#define FAUNA g_fauna
#define AURORA g_aurora

static bool s_rainbowSwap = false;
static float s_occ[256];           // how much of the sun's light each palette entry blocks per ray step
static float s_fogAmt[256];        // how readily each entry steps into its hazier twin

static C3 spectrum(float t) {      // 0 violet (inner) .. 1 red (outer)
    const C3* K = AURORA;
    float f = clampf(t, 0.f, 1.f) * 6.f;
    int i = (int)f;
    if (i >= 6) return K[6];
    return mix(K[i], K[i + 1], f - i);
}

// Overcast and rain wash colour out of the sky and take the sun's directional light away.
static void weatherKey(const Scene& sc) {
    float grey = clampf(sc.cloud * 0.6f - 0.15f + sc.rain * 0.45f + sc.storm * 0.25f, 0.f, 0.9f);
    float dark = 1.f - 0.28f * sc.rain - 0.22f * sc.storm;
    auto desat = [&](C3 c, float k, float d) -> C3 {
        float l = c.r * 0.3f + c.g * 0.55f + c.b * 0.15f;
        return scale(mix(c, C3{l * 0.92f, l * 0.96f, l * 1.05f}, k), d);
    };
    S.zen = desat(S.zen, grey, dark);
    S.hor = desat(S.hor, grey * 0.8f, dark);
    S.glow = desat(S.glow, grey * 0.9f, dark);
    S.cDark = desat(S.cDark, grey, dark * 0.85f);
    S.cLit = desat(S.cLit, grey * 0.8f, dark);
    S.fog = desat(S.fog, grey * 0.7f, dark);
    S.amb = scale(S.amb, 1.f - 0.25f * grey);
    S.key = scale(S.key, 1.f - 0.75f * grey);
}

static void buildPalette() {
    const Scene& sc = g_scene;
    C3 P[256];
    auto lit = [&](C3 m, float s) -> C3 {
        C3 c = scale(mul(m, S.amb), 0.28f + 0.82f * s);
        return add(c, scale(mul(m, S.key), s * s * 0.55f));
    };
    const float shade = 1.f - 0.3f * sc.forestNear;                 // under the canopy it's dimmer
    auto fogc = [&](C3 c, float f) -> C3 { return mix(c, S.fog, f); };
    // light a hue-shifted ramp: the ramp carries the form, the sky carries the mood
    auto litR = [&](const Ramp4& r, float s) -> C3 {
        C3 b = rampAt(r, s);
        return add(scale(mul(b, S.amb), 0.6f + 0.45f * s), scale(mul(b, S.key), s * s * 0.4f));
    };
    auto grassAt = [&](float s) -> C3 { return mix(rampAt(g_grassR, s), rampAt(g_wetR, s), sc.wet * 0.7f); };
    auto litG = [&](float s) -> C3 { C3 b = grassAt(s); return add(scale(mul(b, S.amb), 0.6f + 0.45f * s), scale(mul(b, S.key), s * s * 0.4f)); };

    for (int i = 0; i < 16; i++) {
        float t = i / 15.f;
        // sky: zenith -> horizon (0..9) -> sun glow (10..15)
        P[R_SKY + i] = i < 10 ? mix(S.zen, S.hor, powf(i / 9.f, 1.15f)) : mix(S.hor, S.glow, (i - 9) / 6.f);
        // clouds: shadowed belly -> sunlit edge, with horizon colour bleeding through the middle
        C3 c = mix(S.cDark, S.cLit, t);
        P[R_CLOUD + i] = mix(c, S.hor, 0.22f * sinf(t * PI_F));
        // sun / stars / glitter
        P[R_SUN + i] = i == 15 ? mix(S.sun, C3{255, 255, 255}, 0.4f) : mix(S.glow, S.sun, t);
        // mountains: hue-shifted rock with snow at the top of the ramp, heavily / moderately fogged
        C3 mf = mix(rampAt(g_rockR, t), SNOW, clampf((t - 0.74f) / 0.18f, 0.f, 1.f) * (1.f - g_desert) * (1.f - g_karst));
        P[R_MTN_FAR + i] = fogc(add(scale(mul(mf, S.amb), 0.55f + 0.5f * t), scale(mul(mf, S.key), t * t * 0.45f)), 0.5f);
        C3 mn = mix(rampAt(g_rock2R, t), SNOW, clampf((t - 0.84f) / 0.14f, 0.f, 1.f) * (1.f - g_desert) * (1.f - g_karst));
        P[R_MTN_NEAR + i] = fogc(add(scale(mul(mn, S.amb), 0.55f + 0.5f * t), scale(mul(mn, S.key), t * t * 0.45f)), 0.28f);
        // half-ramps: near 0..7, fogged 8..15
        float s = (i & 7) / 7.f;
        bool far = i >= 8;
        C3 hl = litR(HILL_R, s);
        if (g_desert > 0.f) hl = mix(hl, add(scale(mul(rampAt(g_rock2R, s), S.amb), 0.6f + 0.45f * s), scale(mul(rampAt(g_rock2R, s), S.key), s * s * 0.4f)), g_desert);   // desert: low red rises, not forest
        P[R_HILLS + i] = fogc(hl, far ? 0.44f : 0.16f);
        C3 lf = scale(litR(g_leafR, s), far ? 1.f : shade);
        if ((i & 7) == 7) lf = add(lf, scale(S.key, 0.08f));               // warm rim
        P[R_LEAF + i] = fogc(lf, far ? 0.36f : 0.03f);
        if (!far) {
            C3 bk = scale(litR(g_barkR, s), shade);
            if (i == 7) bk = add(bk, scale(S.key, 0.1f));
            P[R_BARK + i] = fogc(bk, 0.04f);
        } else {
            C3 st = scale(litR(g_stoneR, s), shade);
            if (i == 15) st = add(st, scale(S.key, 0.08f));
            P[R_BARK + i] = fogc(st, 0.04f);
        }
        // ground: 4 depth bands x 4 shades (deep shade, shade, base, sunlit)
        {
            int bandI = i >> 2, sh = i & 3;
            static const float GS[4] = {0.1f, 0.3f, 0.5f, 0.74f};
            C3 g = litG(GS[sh]);
            if (bandI < 2) g = scale(g, shade);
            P[R_GROUND + i] = fogc(g, powf(bandI / 3.f, 1.5f) * 0.7f);
        }
        // accents: critters + flowers, lit but stay saturated
        P[R_ACCENT + i] = mix(lit(ACCENT[i], 0.7f), mul(ACCENT[i], S.amb), 0.35f);
    }
    P[R_ACCENT + A_YEL_L] = mix(C3{215, 255, 120}, C3{255, 196, 104}, fmaxf(g_japan, g_karst));
    if (s_alignOn) { P[R_ACCENT + A_LAV] = C3{235, 40, 40}; P[R_ACCENT + A_BLUE] = C3{40, 235, 90}; }   // diagnostic red and green, unlit   // firefly light is its own; lantern light is warm
    if (g_tropical > 0.01f) for (int i = R_WATER; i < R_WATER + N_REFLECT + 16 && i < 256; i++) if (isWaterI((uint8_t)i)) P[i] = mix(P[i], mul(C3{40, 170, 130}, S.amb), 0.3f * g_tropical);   // emerald pools
    if (g_japan > 0.01f) P[R_ACCENT + A_BLUE_D] = mix(P[R_ACCENT + A_BLUE_D], mix(lit(C3{226, 70, 44}, 0.75f), mul(C3{226, 70, 44}, S.amb), 0.35f), g_japan);   // vermilion
    if (sc.aurora > 0.01f && S.night > 0.2f) {                      // aurora nights: the cloud ramp glows
        float k = sc.aurora * S.night;
        for (int i = 0; i < 16; i++) {
            float t = i / 15.f;
            C3 a = t < 0.5f ? mix(C3{8, 30, 44}, C3{40, 225, 140}, t * 2.f) : mix(C3{40, 225, 140}, C3{195, 110, 255}, (t - 0.5f) * 2.f);
            P[R_CLOUD + i] = mix(P[R_CLOUD + i], a, k);
        }
    }
    if (s_rainbowSwap)                                              // sun out of sight: its ramp becomes the bow
        for (int i = 0; i < 16; i++) P[R_SUN + i] = mix(spectrum(i / 15.f), P[R_SKY + 9], 0.28f);
    for (int i = 0; i < N_REFLECT; i++) {
        bool sun = i >= R_SUN && i < R_SUN + 16;
        P[R_WATER + i] = sun ? scale(P[i], 0.9f) : add(scale(P[i], 0.56f), scale(mul(WATER, S.amb), 0.44f));
    }
    // far peaks reflect through another water twin (their own slots now hold creature colours): the closest
    // colour to what their twin used to be, from the sky, cloud, near-mountain and hill twins (not the sun's,
    // which the flashlight borrows), so the cool, slightly desaturated reflection survives
    for (int i = 0; i < 16; i++) {
        C3 want = add(scale(P[R_MTN_FAR + i], 0.56f), scale(mul(WATER, S.amb), 0.44f));
        float best = 1e9f;
        for (int j = 0; j < N_REFLECT; j++) {
            if ((j >= R_SUN && j < R_SUN + 16) || (j >= R_MTN_FAR && j < R_MTN_FAR + 16)) continue;
            const C3& c = P[R_WATER + j];
            float dr = c.r - want.r, dg = c.g - want.g, db = c.b - want.b;
            float d = dr * dr * 0.3f + dg * dg * 0.59f + db * db * 0.11f;
            if (d < best) { best = d; s_farTwin[i] = (uint8_t)(R_WATER + j); }
        }
    }
    for (int i = 0; i < 16; i++) P[R_FAUNA + i] = mix(lit(FAUNA[i], 0.7f), mul(FAUNA[i], S.amb), 0.35f);   // the creatures' extra colours, lit like the accents
    if (s_flashWarm) {                       // the flashlight's warm light: foliage (0..7) and everything else (8..15)
        static const C3 WARM[16] = {{18, 22, 8}, {36, 44, 14}, {60, 72, 22}, {90, 104, 32}, {126, 138, 48}, {170, 176, 72}, {212, 210, 104}, {244, 240, 150},
                                    {24, 14, 8}, {52, 30, 14}, {88, 52, 24}, {130, 82, 40}, {176, 118, 62}, {214, 158, 92}, {240, 198, 132}, {255, 232, 184}};
        for (int i = 0; i < 16; i++) P[R_WATER + R_SUN + i] = WARM[i];
    }
    // light weights for the crepuscular march and fog amounts for the veil
    for (int i = 0; i < 256; i++) {
        float lum = (P[i].r * 0.3f + P[i].g * 0.55f + P[i].b * 0.15f) / 255.f;
        float oc = 0.f;
        if (i >= R_CLOUD && i < R_SUN) oc = 0.4f * (1.f - lum);                 // dark cloud bellies cut beams
        else if (i >= R_MTN_FAR && i < R_MTN_NEAR) oc = 0.05f;
        else if (i >= R_MTN_NEAR && i < R_HILLS) oc = 0.15f;
        else if (i >= R_HILLS && i < R_HILLS + 8) oc = 0.3f;
        else if (i >= R_HILLS + 8 && i < R_GROUND) oc = 0.12f;
        else if (i >= R_LEAF && i < R_LEAF + 8) oc = 0.5f;                     // near canopies and trunks
        else if (i >= R_LEAF + 8 && i < R_BARK) oc = 0.15f;
        else if (i >= R_BARK && i < R_ACCENT) oc = 0.55f;
        else if ((i >= R_ACCENT && i < R_WATER) || (i >= R_FAUNA && i < R_FAUNA + 16)) oc = 0.3f;
        s_occ[i] = oc;
        float f = 0.f;
        if (i >= R_MTN_FAR && i < R_HILLS) f = 0.6f;
        else if (i >= R_HILLS && i < R_HILLS + 8) f = 0.45f;
        else if (i >= R_HILLS + 8 && i < R_GROUND) f = 0.6f;
        else if (i >= R_GROUND && i < R_LEAF) f = 0.15f + 0.15f * ((i - R_GROUND) >> 2);
        else if (i >= R_LEAF && i < R_LEAF + 8) f = 0.f;                    // near things stay crisp
        else if (i >= R_LEAF + 8 && i < R_BARK) f = 0.55f;
        else if (i >= R_BARK && i < R_ACCENT) f = 0.f;
        else if (isWaterI((uint8_t)i)) f = 0.4f;
        s_fogAmt[i] = f * sc.fog;
    }
    if (sc.flash > 0.01f)
        for (int i = 0; i < 256; i++) P[i] = mix(P[i], C3{225, 230, 255}, sc.flash * ((i < R_GROUND || isWaterI((uint8_t)i)) ? 0.6f : 0.3f));
    if (sc.intro > 0.f) {                                           // the dream fades in out of white mist
        float k = powf(sc.intro, 1.5f);
        for (int i = 0; i < 256; i++) P[i] = mix(P[i], C3{238, 234, 244}, k);
    }
    for (int i = 0; i < 256; i++) {
        int r = clampi((int)P[i].r, 0, 255), g = clampi((int)P[i].g, 0, 255), b = clampi((int)P[i].b, 0, 255);
        palRGB[i * 3] = r; palRGB[i * 3 + 1] = g; palRGB[i * 3 + 2] = b;
        uint16_t v = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
        pal565[i] = (uint16_t)((v >> 8) | (v << 8));
    }
}

// ---------------- columns: panorama sampling ----------------
static float spikes(float a, int Nc, float amp, float v, uint32_t salt) {
    float t = angU(a, Nc);
    int ci = (int)floorf(t);
    float best = 0.f;
    for (int k = -1; k <= 1; k++) {
        int c = ci + k;
        float th = vnoiseP((float)c, v, Nc, salt);
        if (th < 0.3f) continue;
        float hgt = amp * (0.35f + 0.65f * (th - 0.3f) / 0.7f);
        float p = hgt * (1.f - fabsf(t - (c + 0.5f)) / 0.8f);
        if (p > best) best = p;
    }
    return best;
}
static inline float stretch(float n, float lo, float hi) { return clampf((n - lo) / (hi - lo), 0.f, 1.f); }

static void computeColumns() {
    const float D = g_dist;
    for (int i = 0; i < W + 2; i++) {
        float a = g_yaw + ((i - 1) + 0.5f - W * 0.5f) / PXR;
        float m = stretch(fbmP(angU(a, 4), D * 0.0012f, 4, 2, 0xA0u), 0.25f, 0.75f);
        float pk = fbmP(angU(a, 13), D * 0.0015f, 13, 4, 0xA1u);
        pk = 1.f - fabsf(2.f * pk - 1.f);
        hFar[i] = 6.f + 14.f * m + 42.f * m * pk * pk * pk;
        if (g_karst > 0.f) {                                        // karst: tall rounded pillars instead of ranges
            float tf = 0.f, tm = 0.f;
            for (int layer = 0; layer < 2; layer++) {
                int P = layer ? 70 : 130;
                float u = angU(a, P);
                int c0 = (int)floorf(u);
                for (int c = c0 - 1; c <= c0 + 1; c++) {
                    uint32_t hh = hash32((uint32_t)((c % P + P) % P) * 0x9E3779B1u ^ (layer ? 0x4A57u : 0x4B57u));
                    float cx0 = c + 0.5f + ((hh & 255) / 255.f - 0.5f) * 0.5f;
                    float w = 0.38f + ((hh >> 8) & 255) / 255.f * 0.3f;
                    float x = fabsf(u - cx0) / w;
                    if (x >= 1.f) continue;
                    float Ht = layer ? 4.f + ((hh >> 16) & 255) / 255.f * 14.f : 10.f + ((hh >> 16) & 255) / 255.f * 30.f;
                    float hgt = Ht * powf(fmaxf(0.f, 1.f - x * x * x * x), 0.35f) * (0.75f + 0.25f * sqrtf(1.f - x * x));   // steep sides, a rounded dome
                    if (layer) tm = fmaxf(tm, hgt); else tf = fmaxf(tf, hgt);
                }
            }
            hFar[i] += (tf - hFar[i]) * g_karst;
            hMid[i] += (tm - 2.f - hMid[i]) * g_karst;
        }
        if (g_mesa > 0.f) {                                         // mesas: flat tops cut in benches
            float cap = 10.f + 22.f * fbmP(angU(a, 5), D * 0.001f, 5, 2, 0x3E5Au);
            float flat = hFar[i] > cap ? cap + (hFar[i] - cap) * 0.08f : hFar[i];
            flat = floorf(flat / 3.f) * 3.f + fminf(3.f, fmodf(flat, 3.f) * 3.f);   // benches
            hFar[i] += (flat - hFar[i]) * g_mesa;
        }

        float m2 = stretch(fbmP(angU(a, 7), D * 0.0025f, 7, 2, 0xB0u), 0.3f, 0.7f);
        float pk2 = fbmP(angU(a, 22), D * 0.003f, 22, 3, 0xB1u);
        pk2 = 1.f - fabsf(2.f * pk2 - 1.f);
        hMid[i] = -3.f + 10.f * m2 + 14.f * m2 * pk2 * pk2;
        if (g_mesa > 0.f) { float cap = 6.f + 8.f * m2; if (hMid[i] > cap) hMid[i] += (cap + (hMid[i] - cap) * 0.1f - hMid[i]) * g_mesa; }

        hHF[i] = 1.f + 8.f * stretch(fbmP(angU(a, 11), D * 0.006f, 11, 3, 0xC0u), 0.3f, 0.7f)
                 + spikes(a, 300, 4.f, D * 0.006f, 0xC1u) * (1.f - g_desert);
        hHN[i] = -1.f + 6.f * stretch(fbmP(angU(a, 17), D * 0.012f, 17, 3, 0xD0u), 0.35f, 0.7f)
                 + spikes(a, 180, 6.f, D * 0.012f, 0xD1u) * (1.f - g_desert);
        if (g_ocean > 0.f) {                                        // the open sea flattens the western horizon
            float dw = fabsf(wrapPi(a - PI_F));
            float sea = g_ocean * clampf((1.2f - dw) / 0.35f, 0.f, 1.f);
            hFar[i] *= 1.f - sea; hMid[i] *= 1.f - sea; hHF[i] *= 1.f - sea; hHN[i] *= 1.f - sea;
        }
        if (i >= 1 && i <= W) {
            int x = i - 1;
            colAng[x] = a;
            sideCol[x] = sinf(wrapPi(SUN_AZ - a));      // + : sun to the right of this column
            mistN[x] = fbmP(angU(a, 30), g_t * 0.08f + D * 0.02f, 30, 2, 0xE0u);
        }
    }
    for (int y = HOR; y < H; y++)
        xoffRow[y] = (int)lroundf(sinf(y * 0.9f + g_t * 0.8f + g_dist * 0.3f) * (0.2f + (y - HOR) * 0.03f));
}

// ---------------- sky ----------------
static void drawSky() {
    float hglow[W];
    for (int x = 0; x < W; x++) {
        float rel = wrapPi(SUN_AZ - colAng[x]);
        hglow[x] = sunVis * lowSun / (1.f + rel * rel * 3.f);
    }
    for (int y = 0; y < HOR; y++) {
        float ty = (y + 0.5f) / HOR;
        float base = 9.f * powf(ty, 1.2f), tt = ty * ty;
        uint8_t* row = fb + y * W;
        float dy = y - sunY;
        for (int x = 0; x < W; x++) {
            float dx = x - sunX;
            float halo = sunVis * 5.5f / (1.f + (dx * dx + dy * dy) * (1.f / 260.f));
            float f = base + hglow[x] * tt * 6.5f + halo;
            row[x] = R_SKY + clampi(band(f, x, y), 0, 15);
        }
    }
}

static void drawNight() {
    const float n = S.night;
    if (n < 0.08f) return;
    const uint32_t seed = worldSeed();
    const uint32_t tw = (uint32_t)(g_t * 5.f);
    for (int x = 0; x < W; x++) {
        float a = colAng[x];
        int su = ((int)floorf(angU(a, 1024)) % 1024 + 1024) % 1024;
        float bandY = 24.f + 13.f * sinf(a + 0.7f);
        for (int y = 0; y < HOR - 6; y++) {
            uint8_t& p = fb[y * W + x];
            float bd = fabsf(y - bandY);
            float band = bd < 11.f ? 1.f - bd / 11.f : 0.f;
            if (band > 0.f) {   // galactic band
                float mw = band * fbmP(angU(a, 48), y * 0.18f, 48, 2, 0x3117u) * n;
                if (mw * 1.7f > dith(x, y) + 0.3f) p = R_CLOUD + clampi((int)(2.f + mw * 10.f), 0, 15);
            }
            uint32_t h = hash32((uint32_t)su * 0x9E3779B1u ^ (uint32_t)y * 0x85EBCA77u ^ seed);
            uint32_t thr = (uint32_t)(n * (5.f + band * 24.f) * (1.f - g_scene.cloud * 0.8f) * (1.f + 1.4f * g_starBoost));
            if ((h & 1023u) < thr) {
                int b = 8 + (int)((h >> 10) % 8);
                if ((hash32(h ^ tw) & 7u) == 0) b -= 5;
                p = R_SUN + clampi(b, 0, 15);
            }
        }
    }
    // the North Star: one steady star, a touch brighter than the rest, always due north
    if (n > 0.3f && g_scene.cloud < 0.75f) {
        float rel = wrapPi(AZ_NORTH - g_yaw);
        if (fabsf(rel) < FOV * 0.5f + 0.02f) {
            int x = (int)lroundf(W * 0.5f + rel * PXR), y = 21;
            put(x, y, R_SUN + 15);
            put(x - 1, y, R_SUN + 10); put(x + 1, y, R_SUN + 10); put(x, y - 1, R_SUN + 10); put(x, y + 1, R_SUN + 10);
        }
    }
    // moon
    if (n > 0.3f) {
        float mx = W * 0.5f + wrapPi(MOON_AZ - g_yaw) * PXR, my = 18.f;
        if (mx > -10 && mx < W + 10) {
            for (int dy = -6; dy <= 6; dy++)
                for (int dx = -6; dx <= 6; dx++) {
                    int d2 = dx * dx + dy * dy;
                    if (d2 > 30) continue;
                    if (dith((int)mx + dx, (int)my + dy) > (n - 0.3f) * 2.f) continue;
                    int b = 13;
                    if ((dx + 1) * (dx + 1) + (dy - 1) * (dy - 1) < 3 || (dx - 2) * (dx - 2) + (dy + 2) * (dy + 2) < 2) b = 10;
                    if (dx * 0.8f + dy * 0.4f > 3.2f) b = 11;
                    put((int)mx + dx, (int)my + dy, R_SUN + b);
                }
        }
    }
    // ringed giant
    if (n > 0.4f) {
        float pxc = W * 0.5f + wrapPi(PLANET_AZ - g_yaw) * PXR, pyc = 19.f;
        if (pxc > -60 && pxc < W + 60) {
            const int R = 15;
            const float fade = (n - 0.4f) * 2.f;
            auto ring = [&](bool front) {
                for (int dx = -36; dx <= 36; dx++) {
                    float e = 1.f - (dx / 36.f) * (dx / 36.f);
                    if (e < 0) continue;
                    float ry = sqrtf(e) * 7.f * (front ? 1.f : -1.f);
                    int y = (int)lroundf(pyc + ry + dx * 0.2f);
                    int x = (int)pxc + dx;
                    if (!front && dx * dx + (y - pyc) * (y - pyc) < R * R) continue;
                    if (dith(x, y) > fade) continue;
                    put(x, y, R_SUN + 9);
                    if ((dx & 3) != 0) put(x, y + 1, R_SUN + 6);
                }
            };
            ring(false);
            for (int dy = -R; dy <= R; dy++)
                for (int dx = -R; dx <= R; dx++) {
                    if (dx * dx + dy * dy > R * R) continue;
                    int x = (int)pxc + dx, y = (int)pyc + dy;
                    if (dith(x, y) > fade) continue;
                    float nx = dx / (float)R, ny = dy / (float)R;
                    float l = 0.45f - nx * 0.45f - ny * 0.2f + sinf(ny * 9.f + nx * 1.5f) * 0.1f;
                    put(x, y, R_CLOUD + clampi((int)(l * 15.f + dith(x, y)), 0, 15));
                }
            ring(true);
        }
    }
}

static void drawSun() {
    if (sunVis <= 0.f || sunX < -12 || sunX > W + 12 || sunY < -12) return;
    int cx = (int)lroundf(sunX), cy = (int)lroundf(sunY);
    for (int dy = -8; dy <= 8; dy++)
        for (int dx = -8; dx <= 8; dx++) {
            float d = sqrtf((float)(dx * dx + dy * dy));
            int x = cx + dx, y = cy + dy;
            if (y >= HOR) continue;
            if (d < 4.6f) put(x, y, R_SUN + 15);
            else if (d < 8.f && (1.f - (d - 4.6f) / 3.4f) > dith(x, y)) put(x, y, R_SUN + 12);
        }
    if (g_sundogs > 0.5f && S.elev < 0.22f && g_scene.cloud < 0.6f) {      // sun dogs: bright spots beside the low sun, a faint halo
        int off = (int)(0.384f * PXR);
        for (int side = -1; side <= 1; side += 2)
            for (int dy = -2; dy <= 2; dy++) for (int dx = -1; dx <= 1; dx++) {
                int x = cx + side * off + dx, y = cy + dy;
                if (y < HOR && (abs(dx) + abs(dy) < 3) && dith(x, y) < 0.8f) put(x, y, (dx == side) ? R_ACCENT + A_PINK_L : R_SUN + 13);
            }
        for (int k = 0; k < 40; k++) {
            float t = PI_F * (0.1f + 0.8f * k / 39.f);
            int x = cx + (int)(cosf(t) * off), y = cy - (int)(sinf(t) * off);
            if (y >= 0 && y < HOR && dith(x, y) < 0.35f) put(x, y, R_SUN + 11);
        }
    }
}

static void drawClouds() {
    constexpr int CR = (HOR - 6) / 2, CW = W / 3 + 3;
    const Scene& sc = g_scene;
    const float cov = sc.cloud;
    const float thresh = 0.92f - 0.62f * cov + 0.12f * S.night * (1.f - sc.cloud) + 0.25f * sc.aurora * S.night;
    const float drift = floorf(g_t * 0.006f * (1.f + 2.f * sc.wind) * PXR) / PXR;   // whole-pixel cloud scroll
    const float centre = CR * (0.42f + 0.2f * sc.storm);
    static float* dens = nullptr;                                  // CW x (CR + 2), on the heap
    static float prox[CW];
    if (!dens) dens = (float*)allocScratch(sizeof(float) * CW * (CR + 2));
    if (!dens) return;
    auto DN = [&](int cx, int cyp1) -> float& { return dens[cx * (CR + 2) + cyp1]; };
    // a 3-px density grid anchored to world columns, so clouds stay put as you turn
    const int wx0 = g_pan - W / 2;
    const int c0 = (wx0 >= 0 ? wx0 : wx0 - 2) / 3;
    for (int cx = 0; cx < CW; cx++) {
        float a = ((c0 + cx) * 3 + 0.5f) / PXR + drift;
        float u = angU(a, 36);
        for (int cy = -1; cy <= CR; cy++) {
            float n = fbmP(u, cy * 0.15f + g_dist * 0.004f + g_t * 0.004f, 36, 3, 0xC1u);
            float prof = clampf(1.f - fabsf(cy - centre) / (CR * 0.6f), 0.f, 1.f);
            DN(cx, cy + 1) = n * 0.8f + prof * 0.35f - thresh;
        }
        float rel = wrapPi(SUN_AZ - a);
        prox[cx] = sunVis / (1.f + rel * rel * 6.f);
    }
    // Bilinear density at cell coordinates. The per-column and per-row parts of the lookup are computed once per
    // frame (the same expressions, so the same values); a cell whose four corners are all too thin to draw is
    // skipped outright (a blend never exceeds its largest corner; drawing needs d >= 0.0409, the skip uses 0.035).
    struct ColL { int ix, ix1; float tx; int prox; };
    struct RowL { int iy; float ty; };
    struct CloudLUT { ColL col[W]; RowL rowC[CR * 2], rowD[CR * 2], rowU[CR * 2]; float cellMax[CW * (CR + 2)]; };
    static CloudLUT* lut = nullptr;                                // runtime buffer (internal RAM): too big for static RAM
    if (!lut) lut = (CloudLUT*)allocScratch(sizeof(CloudLUT));
    if (!lut) return;
    ColL* col = lut->col; RowL* rowC = lut->rowC; RowL* rowD = lut->rowD; RowL* rowU = lut->rowU; float* cellMax = lut->cellMax;
    for (int x = 0; x < W; x++) {
        float fxRaw = (wx0 + x) / 3.f - c0;
        float fx = clampf(fxRaw, 0.f, CW - 1.001f);
        int ix = (int)fx;
        col[x] = {ix, ix + 1 < CW ? ix + 1 : ix, fx - ix, clampi((int)fxRaw, 0, CW - 1)};
    }
    auto rowOf = [&](float fy) -> RowL { fy = clampf(fy, -1.f, CR - 0.001f); int iy = (int)floorf(fy); return {iy, fy - iy}; };
    for (int y = 0; y < CR * 2; y++) { float fy = y * 0.5f - 0.25f; rowC[y] = rowOf(fy); rowD[y] = rowOf(fy + 1.f); rowU[y] = rowOf(fy - 1.f); }
    for (int cx = 0; cx < CW; cx++) {
        int cx1 = cx + 1 < CW ? cx + 1 : cx;
        for (int r = 0; r + 1 < CR + 2; r++)
            cellMax[cx * (CR + 2) + r] = fmaxf(fmaxf(DN(cx, r), DN(cx1, r)), fmaxf(DN(cx, r + 1), DN(cx1, r + 1)));
    }
    auto D = [&](const ColL& c, const RowL& r) -> float {
        float a0 = DN(c.ix, r.iy + 1) + (DN(c.ix1, r.iy + 1) - DN(c.ix, r.iy + 1)) * c.tx;
        float a1 = DN(c.ix, r.iy + 2) + (DN(c.ix1, r.iy + 2) - DN(c.ix, r.iy + 2)) * c.tx;
        return a0 + (a1 - a0) * r.ty;
    };
    for (int y = 0; y < CR * 2; y++) {
        const RowL& rc = rowC[y];
        for (int x = 0; x < W; x++) {
            const ColL& c = col[x];
            if (cellMax[c.ix * (CR + 2) + rc.iy + 1] < 0.035f) continue;          // nothing here thick enough to draw
            float d = D(c, rc);
            if (d <= 0.f) continue;
            float alpha = clampf(d * 11.f, 0.f, 1.f);
            if (alpha < 0.45f || (alpha < 0.7f && (((x + g_pan) ^ y) & 1))) continue;   // crisp 1 px edge, thin seam
            float dn = D(c, rowD[y]), up = D(c, rowU[y]);
            float bottom = clampf((d - dn) * 6.f, 0.f, 1.f), top = clampf((d - up) * 6.f, 0.f, 1.f);
            float light = 0.28f + bottom * 0.62f * lowSun + top * 0.45f * (1.f - lowSun) + prox[c.prox] * 0.3f + (1.f - alpha) * 0.18f;
            light *= 1.f - 0.35f * sc.rain;
            int lvl = clampi(band(light * 4.f, x, y), 0, 4) * 3 + 2;               // five light bands
            if (dn <= 0.f && lowSun > 0.25f) lvl = 14;                             // sunset lights the bellies
            else if (up <= 0.f && lowSun < 0.5f) lvl = clampi(lvl + 3, 0, 15);     // midday lights the tops
            fb[y * W + x] = R_CLOUD + lvl;
        }
    }
}

static void drawRidge(const float* hh, int baseY, uint8_t ramp, float snowLine, uint32_t salt) {
    for (int x = 0; x < W; x++) {
        float h = hh[x + 1];
        if (h <= 0.f) continue;
        float top = baseY - h;
        float slope = (hh[x + 2] - hh[x]) * 0.5f;
        float face = -slope * sideCol[x];                       // + faces the sun
        float lit = 0.5f + 0.3f * face / (1.f + fabsf(face));    // soft, never a hard column split
        float u = angU(colAng[x], 240);
        int y0 = clampi((int)ceilf(top), 0, H);
        float snowDepth = h > snowLine ? (h - snowLine) * 0.55f : -1.f;
        for (int y = y0; y < baseY && y < H; y++) {
            float d = y - top, depth = d / h;
            // erosion gullies: diagonal streaks anchored to the panorama, lit on the sun side
            float g = vnoiseP(u + d * 0.2f * (sideCol[x] >= 0.f ? 1.f : -1.f), d * 0.12f, 240, salt);
            // spur: shade from a column displaced by depth so the lit/shadow split runs down diagonally
            int xs = clampi(x + (int)((g - 0.5f) * d * 1.4f), 0, W - 1);
            float fs = -(hh[xs + 2] - hh[xs]) * 0.5f * sideCol[x];
            float ls = 0.5f + 0.3f * fs / (1.f + fabsf(fs));
            float s = (lit + ls) * 0.5f + (g - 0.5f) * 0.38f * (0.4f + 0.6f * (1.f - depth)) - depth * 0.3f;
            if (snowDepth > 0.f && d < snowDepth + g * 3.f) s += 0.3f + 0.15f * (lit - 0.3f);
            int lv = clampi((int)(s * 7.99f), 0, 7) * 2 + 1;                    // eight clean facet tones
            if (d < 1.f && fs > 0.15f) lv = clampi(lv + 4, 0, 15);              // the ridgeline catches the light
            fb[y * W + x] = ramp + lv;
        }
    }
}

static void drawHills(const float* hh, int baseY, int half) {
    for (int x = 0; x < W; x++) {
        float h = hh[x + 1];
        if (h <= 0.f) continue;
        float top = baseY - h;
        float face = clampf(-(hh[x + 2] - hh[x]) * 0.5f * sideCol[x], -1.f, 1.f);
        int y0 = clampi((int)ceilf(top), 0, H);
        for (int y = y0; y < baseY && y < H; y++) {
            float d = y - top;
            uint32_t hs = hash32((uint32_t)((x + g_pan) * 2654435761u) ^ (uint32_t)(y * 40503) ^ (uint32_t)half);
            float s = 2.4f + face * 2.2f - d * 0.18f + ((int)(hs & 3) - 1.5f) * 0.35f;
            if (d < 1.2f && face > 0.35f) s += 1.2f * lowSun + 0.5f;
            fb[y * W + x] = R_HILLS + half + clampi((int)(s + dith(x, y)), 0, 7);
        }
    }
}

// Index-space fog: step a pixel to its more-fogged palette twin. No new colours, no dots.
static inline uint8_t fogStep(uint8_t p) {
    if (isWaterI(p)) {
        uint8_t q = p - R_WATER;
        uint8_t f = fogStep(q);
        return f < N_REFLECT ? reflIdx(f) : p;
    }
    if (p >= R_HILLS && p < R_HILLS + 8) return p + 8;                                   // near hills -> far hills
    if (p >= R_HILLS + 8 && p < R_GROUND) return R_MTN_NEAR + 1 + (p & 7);                // far hills -> mountain haze
    if (p >= R_MTN_NEAR && p < R_HILLS) return R_MTN_FAR + (p & 15);                     // near range -> far range
    if (p >= R_MTN_FAR && p < R_MTN_NEAR) return (p & 15) < 14 ? p + 2 : p;               // far range: a softer, lighter rock tone
    if (p >= R_GROUND && p < R_LEAF) { int b = (p - R_GROUND) >> 2; return b < 3 ? p + 4 : p; }
    if (p >= R_LEAF && p < R_LEAF + 8) return p + 8;
    if (p >= R_LEAF + 8 && p < R_BARK) return R_HILLS + 8 + (p & 7);                    // far leaves -> hazy hills
    if (p >= R_BARK && p < R_BARK + 8) return R_HILLS + 8 + ((p & 7) >> 1);             // trunks dissolve into haze
    if (p >= R_BARK + 8 && p < R_ACCENT) return R_MTN_NEAR + (p & 7) * 2;                // stone -> hazy rock
    return p;
}

static void drawMist(int y0, int y1) {
    float mist = S.mist + g_scene.fog * 0.7f;
    if (mist < 0.02f) return;
    float spread = 5.f + g_scene.fog * 9.f;
    for (int y = y0; y < y1; y++) {
        if (y < 0 || y >= H) continue;
        float fy = mist * 1.6f * expf(-fabsf(y - (HOR - 0.5f)) / spread);
        for (int x = 0; x < W; x++) {
            float amt = fy * (0.3f + 0.9f * mistN[x]);
            uint8_t& p = fb[y * W + x];
            float r = ign(x, y);
            if (r < amt) p = fogStep(p);
            if (r < amt - 0.6f) p = fogStep(p);
        }
    }
}

// ---------------- ground + lake ----------------
// ---- Grass clumps: a small texture of overlapping tufts, generated once, read with one lookup per ground pixel.
// Each cell is a shade OFFSET on top of the ground's own light (so day, night, dapples, snow and every biome's
// ramp still apply): lit spiky blade tips on the far side of each tuft, a mid-tone body, and dark crevices
// where one tuft tucks behind the next. Tufts are drawn far to near so nearer ones overlap. Two variants, each
// also mirrored, give four looks; the width (130) divides the world's grass lattice so it wraps seamlessly.
static constexpr int GT_W = 130, GT_H = 48;
static int8_t* g_grassTex = nullptr;                   // [2][GT_H][GT_W], internal RAM (read per ground pixel)
static void buildGrassTex() {
    g_grassTex = (int8_t*)memFast(2 * GT_W * GT_H);
    if (!g_grassTex) return;
    uint32_t r = 0x6A55u;
    auto rnd = [&]() { r = r * 1664525u + 1013904223u; return (r >> 8) * (1.f / 16777216.f); };
    for (int v = 0; v < 2; v++) {
        int8_t* T = g_grassTex + v * GT_W * GT_H;
        // Two layers, added: MOUNDS (tufts gathered into rounded clumps, lit on top, dark crevices at their feet
        // where each tucks behind the one in front) and BLADES (tall narrow tufts fanning from their base, each
        // blade a column with a darker seam beside it, the tips catching the light). A cell is ~1.8 px wide and
        // ~1 row tall on screen, so shapes are drawn tall and narrow in cells.
        int8_t* mound = (int8_t*)malloc(2 * GT_W * GT_H);              // scratch for building (freed below)
        if (!mound) { memset(T, 0, GT_W * GT_H); continue; }
        int8_t* blade = mound + GT_W * GT_H;
        auto wr = [&](int8_t* L, int x, int y, int8_t v) { x %= GT_W; if (x < 0) x += GT_W; y %= GT_H; if (y < 0) y += GT_H; L[y * GT_W + x] = v; };
        for (int i = 0; i < GT_W * GT_H; i++) { mound[i] = -1; blade[i] = 0; }
        for (int cyb = GT_H + 8; cyb > -8; cyb -= 5) {                      // mounds, far (high y) to near
            for (float cx = rnd() * 6.f; cx < GT_W; cx += 6.f + rnd() * 4.f) {
                int rw = 5 + (int)(rnd() * 3.f), rh = 5 + (int)(rnd() * 3.f);
                int cy = cyb + (int)(rnd() * 3.f) - 1;
                for (int dx = -rw; dx <= rw; dx++) {
                    float k = (float)dx / (rw + 0.5f);
                    int top = cy + (int)floorf(rh * sqrtf(fmaxf(0.f, 1.f - k * k)));
                    for (int y = cy; y <= top; y++) wr(mound, (int)cx + dx, y, (int8_t)(y >= top - 2 ? 1 : 0));
                    wr(mound, (int)cx + dx, cy - 1, -1);                          // its shadowed foot
                }
            }
        }
        for (int cyb = GT_H + 8; cyb > -8; cyb -= 2) {                      // blades, far to near
            for (float cx = rnd() * 4.f; cx < GT_W; cx += 2.5f + rnd() * 3.f) {
                int hw = 1 + (int)(rnd() * 2.2f), ht = 3 + (int)(rnd() * 4.f);
                int cy = cyb + (int)(rnd() * 2.f);
                float lean = (rnd() - 0.5f) * 0.5f;
                for (int dx = -hw; dx <= hw; dx++) {
                    int colTop = cy + ht - (abs(dx) == hw ? 1 + (int)(rnd() * 2.f) : (int)(rnd() * 2.f));
                    bool seam = ((dx + hw) & 1) != 0;
                    for (int y = cy; y <= colTop; y++) {
                        float u = (float)(y - cy) / (float)(colTop - cy + 1);
                        int x = (int)cx + (int)lroundf(dx * (0.45f + 0.55f * u) + lean * (y - cy));
                        wr(blade, x, y, (int8_t)(seam ? (u > 0.6f ? 0 : -1) : (y == colTop ? 1 : 0)));
                    }
                }
            }
        }
        for (int i = 0; i < GT_W * GT_H; i++) { int o = mound[i] + blade[i]; T[i] = (int8_t)(o < -1 ? -1 : o > 2 ? 2 : o); }
        free(mound);
    }
}

static void drawGround() {
    const uint32_t seed = worldSeed();
    const BiomeParams& bh = biomeAt(g_dist);
    const uint32_t flowerRate = (uint32_t)(3.f * bh.flowers + 0.5f);
    const bool heather = bh.heather > 0.5f;
    const float daylight = sunVis * clampf((S.elev + 0.05f) / 0.12f, 0.f, 1.f) * (1.f - S.night);
    const uint32_t tglit = (uint32_t)(g_t * 2.f);
    const int tstreak = (int)(g_t * 0.6f);
    // Where the water lies, the gravel, the canopy's light pools: these depend only on where you stand and
    // face, so while you're standing still (fishing, watching) they're reused exactly. Cloud shadows still move.
    struct GCell { float lf, canopyN; uint8_t flags; };
    static GCell* gcache = nullptr;
    static int gcPan = 0x7FFFFFFF; static float gcDist = -1.f; static bool gcCanopy = false; static uint32_t gcGen = 0, gcSeed = 0;
    const int GCW = W / 2, GCH = (H - HOR + 1) / 2;
    if (!gcache) gcache = (GCell*)allocScratch(sizeof(GCell) * GCW * GCH);
    const float canopyNow = clampf((g_scene.forestNear - 0.3f) * 2.5f, 0.f, 1.f);
    const bool reuse = gcache && !g_noGroundCache && gcPan == g_pan && gcDist == g_dist && gcCanopy == (canopyNow > 0.f) && gcGen == journeyGeneration() && gcSeed == worldSeed();
    for (int by = HOR; by < H; by += 2) {
        float zr = fminf(ZK / (by - HOR + 1.f), 70.f);
        bool rowCreeks = reuse ? false : fbmP(0.f, (g_dist + zr) * 0.0035f, 1, 2, 0x5C1u) >= 0.75f - 0.5f * biomeAt(g_dist + zr).creeks;   // does this stretch have creeks (the biome decides)
        for (int bx = 0; bx < W; bx += 2) {
            float zc = zr;
            GCell* gcell = gcache ? &gcache[((by - HOR) >> 1) * GCW + (bx >> 1)] : nullptr;
            float lf;
            bool lake, creek = false, paddy = false, gravelAt;
            float canopyNoise = 0.f;
            if (reuse) {
                lf = gcell->lf; lake = gcell->flags & 1; creek = gcell->flags & 2; paddy = gcell->flags & 4; gravelAt = gcell->flags & 8; canopyNoise = gcell->canopyN;
            } else {
                lf = lakeField(colAng[bx] + 0.5f / PXR, g_dist + zc);
                lake = lf > LAKE_T;
                if (!lake && g_paddies > 0.5f && zc < 60.f && paddyAt(colAng[bx] + 0.5f / PXR, g_dist + zc)) lake = paddy = true;   // a flooded terrace
                if (!lake && zc < 60.f && lf < LAKE_T - 0.02f && rowCreeks)                                // a creek winding across
                    creek = lake = fabsf(fbmP(angU(colAng[bx] + 0.5f / PXR, 3), (g_dist + zc) * 0.011f, 3, 2, 0x57E4u) - 0.5f) < 0.009f;
                gravelAt = g_japan > 0.5f && !lake && fbmP(angU(colAng[bx], 24), (g_dist + zc) * 0.08f, 24, 2, 0x6EA7u) > 0.6f;
                if (!lake && canopyNow > 0.f) canopyNoise = fbmP(angU(colAng[bx], 160), (g_dist + zc) * 0.55f, 160, 2, 0xD4A1u);
                if (gcell) { gcell->lf = lf; gcell->canopyN = canopyNoise; gcell->flags = (uint8_t)((lake ? 1 : 0) | (creek ? 2 : 0) | (paddy ? 4 : 0) | (gravelAt ? 8 : 0)); }
            }
            // dappled light: crisp-edged pools of sun under the canopy, cloud shadows in the open
            int lightAt = 2;
            if (!lake && daylight > 0.1f) {
                float canopy = canopyNow;
                if (canopy > 0.f) {
                    float n = canopyNoise;
                    float thr = 0.4f + 0.2f * canopy + 0.25f * (1.f - daylight);
                    lightAt = n > thr + 0.08f ? 3 : n > thr - 0.04f ? 2 : n > thr - 0.16f ? 1 : 0;
                    if (g_snow > 0.5f && lightAt == 0) lightAt = 1;               // shade on snow stays soft and blue
                } else {
                    float n = fbmP(angU(colAng[bx], 12), (g_dist + zc) * 0.03f + g_t * 0.02f, 12, 2, 0xC5ADu);
                    lightAt = n > 0.38f + 0.5f * (1.f - g_scene.cloud) ? 1 : 2;   // base tone; blades carry the sun
                }
            } else if (!lake) lightAt = 1;
            for (int py = by; py < by + 2 && py < H; py++)
                for (int px = bx; px < bx + 2; px++) {
                    uint8_t idx;
                    if (lake) {
                        int sy = 2 * HOR - 1 - py;
                        if (sy < 0) sy = 0;
                        int sx = clampi(px + (paddy ? 0 : xoffRow[py]), 0, W - 1);   // terraces lie mirror-still
                        uint8_t src = fb[sy * W + sx];
                        idx = src < N_REFLECT ? reflIdx(src) : (uint8_t)(R_WATER + R_HILLS);
                        if (s_flashWarm && src >= R_SUN && src < R_SUN + 16) idx = R_WATER + R_CLOUD + 14;   // (those slots hold the flashlight's light)
                        if (g_snow > 0.5f && !creek) {                     // frozen: pale ice, a faint mirror, cracks
                            float zq = ZK / (py - HOR + 0.5f);
                            uint32_t ic = hash32((uint32_t)((px + g_pan) >> 2) * 0x9E3779B1u ^ (uint32_t)(int)floorf((g_dist + zq) * 1.5f) * 0x85EBCA77u);
                            idx = (ic & 7u) == 0 ? idx : (uint8_t)(R_GROUND + 3);                          // pale ice (the snow's own light), a glint of reflection
                            if ((ic & 31u) == 5u || (ic & 31u) == 6u) idx = R_GROUND + 1;                   // fine cracks
                            fb[py * W + px] = idx;
                            continue;
                        }
                        if (g_lily > 0.f && !creek) {                       // lily pads on still water
                            float zq = ZK / (py - HOR + 0.5f);
                            uint32_t lp = hash32((uint32_t)((px + g_pan) / 3) * 0x27D4EB2Du ^ (uint32_t)(int)floorf((g_dist + zq) * 1.2f) * 0x165667B1u);
                            if ((lp & 63u) < (uint32_t)(g_lily * 9.f)) {
                                idx = ((lp >> 8) & 15u) == 0 ? (uint8_t)(R_ACCENT + A_PINK_L) : leafI(2 + (int)((lp >> 12) & 1u), false);
                                fb[py * W + px] = idx;
                                continue;
                            }
                        }
                        if (((py + tstreak) % 5) == 0 && src < N_REFLECT) idx = (uint8_t)((idx + 2) > (idx | 15) ? (idx | 15) : idx + 2);
                        // the sun's path on the water: broken horizontal glints, densest at its centre
                        if (g_ocean > 0.5f && !creek) {                    // the sea: swells rolling in, whitecaps breaking
                            float zq = ZK / (py - HOR + 0.5f);
                            if (zq < 45.f) {
                                float ph = (g_dist + zq) * 0.45f + g_t * 0.35f;
                                float fr = ph - floorf(ph);
                                uint32_t f = hash32((uint32_t)((px + g_pan) >> 1) * 0x9E3779B1u ^ (uint32_t)(int)floorf(ph) * 0x85EBCA77u);
                                if (fr < 0.07f && (f & 3u) != 0) idx = R_CLOUD + 14;
                                else if (fr < 0.14f && (f & 7u) == 0) idx = R_WATER + R_SKY + 13;
                            }
                        }
                        if (creek) {                                        // moving water: foam and riffles flowing past
                            float zq = ZK / (py - HOR + 0.5f);
                            int flow = (int)floorf((g_dist + zq) * 3.f + g_t * 2.5f);
                            uint32_t f = hash32((uint32_t)((px + g_pan) >> 1) * 0x9E3779B1u ^ (uint32_t)flow * 0x85EBCA77u);
                            if ((f & 7u) == 0) idx = R_WATER + R_SKY + 14;
                            else if ((f & 7u) == 1) idx = R_WATER + R_SKY + 11;
                        }
                        float wcol = 2.f + (py - HOR) * (0.18f + 0.3f * lowSun);
                        float gl = sunVis * (1.f - g_scene.rain) * (0.35f + 0.65f * lowSun);
                        float cf = 1.f - fabsf(px - sunX) / wcol;
                        if (gl > 0.05f && cf > 0.f && !s_rainbowSwap && ((py + tstreak) % 3) != 0) {
                            uint32_t g = hash32((uint32_t)((px + g_pan + py * 5) >> 2) * 131u ^ (uint32_t)py * 977u ^ tglit);
                            if ((g & 255u) < (uint32_t)(gl * 200.f * cf * cf)) idx = R_SUN + 9 + (uint8_t)(cf * 5.f + ((g >> 9) & 1u));
                        }
                    } else {
                        float z = ZK / (py - HOR + 0.5f);
                        float f = 1.f - (py - HOR) / (float)(H - HOR);
                        int bnd = (int)(f * f * 3.99f);
                        // grass in clusters: blades ~2 px wide, 2-3 rows tall, anchored to the world
                        int gx = ((int)floorf(angU(colAng[px], 520)) % 520 + 520) % 520;
                        static const float BLADE_K[4] = {3.6f, 1.2f, 0.45f, 0.15f};  // ~3 rows per blade in every band
                        if (gravelAt && z < 26.f) {                         // raked gravel: lines drawn by a rake, anchored to the world
                            int rake = (int)floorf((g_dist + z) * 5.f);
                            idx = R_BARK + 8 + ((rake & 1) ? 5 : 4) + (lightAt >= 2 ? 1 : 0) - (bnd > 1 ? 1 : 0);
                            fb[py * W + px] = idx;
                            continue;
                        }
                        // tufts from the clump texture: bigger near you, finer toward the horizon
                        static const float TUFT_V[4] = {2.f, 3.f, 4.f, 4.f};
                        int gxs = bnd < 2 ? gx : ((int)floorf(angU(colAng[px], 1040)) % 1040 + 1040) % 1040;   // finer toward the horizon
                        float dz = (g_dist + z) * BLADE_K[bnd] * TUFT_V[bnd];
                        int dzi = (int)floorf(dz);
                        int ty = dzi % GT_H; if (ty < 0) ty += GT_H;
                        int tile = gxs / GT_W + (dzi >= 0 ? dzi / GT_H : -1 - (-dzi - 1) / GT_H) * 3 + bnd;
                        int tx = gxs % GT_W;
                        if (tile & 2) tx = GT_W - 1 - tx;                   // the mirrored looks
                        int off = g_grassTex ? g_grassTex[(tile & 1) * GT_W * GT_H + ty * GT_W + tx] : 0;
                        if (bnd == 3 || g_snow > 0.5f || g_desert > 0.5f)    // the far distance, snowfields and sand: only the strongest shapes, softly
                            off = off >= 2 ? 1 : (off <= -1 && bnd != 3 && (tx & 1)) ? -1 : 0;
                        int sh = lightAt + off;                             // the tuft on the ground's own light
                        sh = sh < 0 ? 0 : sh > 3 ? 3 : sh;
                        idx = R_GROUND + bnd * 4 + sh;
                        bool nearRows = z < 9.f || (g_glowMoss > 0.5f && S.night > 0.35f && bnd < 2);
                        uint32_t h = 0;
                        if (nearRows) {                                     // flowers and glowing moss: exactly where they were
                            int gz = (int)floorf((g_dist + z) * BLADE_K[bnd]);
                            h = hash32((uint32_t)gx * 0x9E3779B1u ^ (uint32_t)gz * 0x85EBCA77u ^ seed);
                        }
                        if (g_glowMoss > 0.5f && S.night > 0.35f && bnd < 2 && (h & 511u) < 5u) {   // moss glowing faintly in the dark
                            fb[py * W + px] = R_ACCENT + A_YEL_L;
                            continue;
                        }
                        if (z < 9.f && nearRows && (h & 255u) < flowerRate) {
                            static const uint8_t fl[4] = {A_PINK_L, A_BLUE, A_YEL, A_LAV};
                            static const uint8_t he[4] = {A_LAV, A_PINK, A_LAV, A_PINK_L};   // heather on the moor
                            idx = R_ACCENT + (heather ? he : fl)[(h >> 8) & 3u];
                        }
                    }
                    fb[py * W + px] = idx;
                }
        }
    }
    gcPan = g_pan; gcDist = g_dist; gcCanopy = canopyNow > 0.f; gcGen = journeyGeneration(); gcSeed = worldSeed();
}

// ---------------- pixel care: waterline, cast shadows, stray pixels ----------------
static inline bool isWater(uint8_t p) { return isWaterI(p); }
static inline bool isGround(uint8_t p) { return p >= R_GROUND && p < R_LEAF; }
static inline uint8_t groundShade(uint8_t p, int sh) { return (uint8_t)(R_GROUND + (((p - R_GROUND) >> 2) << 2) + sh); }

// a bright line where the water meets the bank, and the bank's lip in shadow
static void shoreline() {
    for (int y = HOR + 1; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint8_t& p = fb[y * W + x];
            uint8_t up = fb[(y - 1) * W + x];
            if (isWater(p) && isGround(up)) {
                p = R_WATER + R_SKY + 13;
                fb[(y - 1) * W + x] = groundShade(up, 0);
                if (g_ocean > 0.5f && y + 1 < H && isWater(fb[(y + 1) * W + x]) && ((x + g_pan + (int)(g_t * 3.f)) % 5) != 0) fb[(y + 1) * W + x] = R_CLOUD + 15;   // surf
                if (g_japan > 0.5f && y > HOR + 12 && (hash32((uint32_t)((x + g_pan) >> 1) * 0x9E37u) % 4u) == 0)   // garden ponds: stones set along the near edge
                    fb[(y - 1) * W + x] = R_BARK + 8 + 4 + (int)(hash32((uint32_t)((x + g_pan) >> 1)) & 1u);
            }
        }
}

// Trees and rocks lay their shadows across the ground, away from the sun.
static void castShadows() {
    float daylight = sunVis * clampf((S.elev + 0.02f) / 0.1f, 0.f, 1.f) * (1.f - S.night) * (1.f - g_scene.cloud * 0.6f);
    if (daylight < 0.25f) return;
    const float elev = fmaxf(S.elev, 0.05f);
    const float dx = -cosf(SUN_AZ), dy = -sinf(SUN_AZ);          // shadows point away from the sun
    const FieldObj* objs = fieldObjects();
    const int snowShade = g_snow > 0.5f ? 1 : 0;
    for (int i = 0; i < FIELD_N; i++) {
        const FieldObj& o = objs[i];
        if (!o.solid || o.wet) continue;
        float ox = o.x - g_camX, oy = o.y - g_camY;
        float r0 = sqrtf(ox * ox + oy * oy);
        if (r0 > 24.f || r0 < 0.8f) continue;
        float hgt = o.type == OBJ_PINE || o.type == OBJ_BROAD ? o.a : o.type == OBJ_OUTCROP ? o.b : o.a * 1.4f;
        float len = fminf(hgt * 0.8f / elev, 14.f);
        float trunkW = o.type == OBJ_BROAD || o.type == OBJ_PINE ? o.radius * 1.6f : o.radius * 1.8f;
        float crownW = o.type == OBJ_BROAD ? o.b * 1.6f : o.type == OBJ_PINE ? o.a * 0.35f : trunkW;
        int steps = 48;
        for (int k = 0; k <= steps; k++) {
            float t = (float)k / steps;
            float qx = ox + dx * len * t, qy = oy + dy * len * t;
            float rq = sqrtf(qx * qx + qy * qy);
            if (rq < 1.f) continue;
            float rel = wrapPi(atan2f(qy, qx) - g_yaw);
            if (fabsf(rel) > FOV * 0.5f + 0.2f) continue;
            float w = t < 0.45f ? trunkW : crownW * sinf((t - 0.3f) / 0.7f * PI_F) + trunkW * 0.5f;
            int sx = (int)(W * 0.5f + rel * PXR), sy = (int)(HOR + ZK / rq);
            if (sy < HOR || sy >= H) continue;
            int half = (int)(w * 0.5f * PXR / rq);
            int rows = 1 + (int)(ZK / (rq * rq) * len / steps);                 // fill the gap to the next sample
            for (int yy = sy; yy < sy + rows && yy < H; yy++)
                for (int xx = sx - half; xx <= sx + half; xx++) {
                    if ((unsigned)xx >= (unsigned)W) continue;
                    uint8_t& p = fb[yy * W + xx];
                    if (isGround(p)) p = groundShade(p, snowShade);   // on snow, shadows are bright and blue
                }
        }
    }
}

// A pixel that differs from all four neighbours, which agree, is noise not detail: fold it in.
// Terrain, ground and plants only; stars, glints, rain, motes and fireflies are deliberate.
static inline void cleanRow(int y) {
    uint8_t* row = fb + y * W;
    for (int x = 1; x < W - 1; x++) {
        uint8_t p = row[x];
        if (p < R_MTN_FAR || p >= R_ACCENT) continue;
        uint8_t l = row[x - 1];
        if (l == p || l != row[x + 1] || l != row[x - W] || l != row[x + W]) continue;
        if (l < R_MTN_FAR || l >= R_ACCENT) continue;
        row[x] = l;
    }
}
static inline void veilRow(int y);
// One pass over the frame: clean row y, then veil row y-1 (which nothing reads any more), so the
// result is identical to two separate passes.
static void cleanAndVeil() {
    bool veil = g_scene.fog >= 0.03f;
    for (int y = 0; y < H; y++) {
        if (y >= 1 && y < H - 1) cleanRow(y);
        if (veil && y >= 1) veilRow(y - 1);
    }
    if (veil) veilRow(H - 1);
}

// ---------------- crepuscular rays: screen-space light scattering ----------------
// March outward from the sun along 192 angle bins, letting each bin's light follow what it passes:
// open sky feeds it, canopies, trunks, ridges and cloud bellies starve it. The shafts are whatever
// light survives the gaps. Then every pixel looks up its bin and radius and brightens (or, for
// solid things, hazes) by that much. Fog makes the air scatter more.
static constexpr int RAY_NB = 192, RAY_NR = 128;
static constexpr float RAY_STEP = 3.f;
static uint8_t* s_rayT = nullptr;      // RAY_NB x RAY_NR, allocated in renderInit

static void drawCrepuscular() {
    const Scene& sc = g_scene;
    float rel = wrapPi(SUN_AZ - g_yaw);
    if (fabsf(rel) > 1.35f || S.night > 0.6f) return;
    float sunUp = clampf((S.elev + 0.04f) / 0.08f, 0.f, 1.f);
    float lowK = clampf(1.f - S.elev / 0.5f, 0.f, 1.f);
    float str = sunUp * (0.35f + 0.65f * lowK) * (1.f - sc.rain * 0.85f) * (1.f - 0.8f * clampf((sc.cloud - 0.75f) * 4.f, 0.f, 1.f))
                * (1.45f + sc.fog * 1.3f + g_shafts * 0.8f) * clampf(1.45f - fabsf(rel), 0.f, 1.f) * (1.f - sc.intro);
    if (str < 0.04f || !s_rayT) return;
#ifdef HOST
    if (getenv("NORAYS")) return;
#ifdef HOST
    if (getenv("RAYDBG")) fprintf(stderr, "rays: str %.2f sun (%.0f,%.0f) elev %.2f\n", str, sunX, sunY, S.elev);   // desktop debugging only
#endif
#endif
    // light leaves the sun along each bin and is attenuated by whatever it crosses; a whisper of
    // multiple scattering lets blocked shafts glow back up slowly. A soft angular streak pattern
    // gives the rays their structure even across open sky.
    for (int bi = 0; bi < RAY_NB; bi++) {
        float ang = bi * (kTau / RAY_NB);
        float dx = cosf(ang) * RAY_STEP, dy = sinf(ang) * RAY_STEP;
        float pat = fbmP((float)bi * (48.f / RAY_NB), g_t * 0.08f, 48, 2, 0x4A7u);
        pat = 0.45f + 0.55f * clampf((pat - 0.3f) * 2.5f, 0.f, 1.f);
        float x = sunX, y = sunY, T = 1.f;
        for (int k = 0; k < RAY_NR; k++) {
            x += dx; y += dy;
            int ix = (int)x, iy = (int)y;
            if ((unsigned)ix < (unsigned)W && (unsigned)iy < (unsigned)H) T *= 1.f - s_occ[fb[iy * W + ix]];
            T += (1.f - T) * 0.006f;
            s_rayT[bi * RAY_NR + k] = (uint8_t)(T * pat * 255.f);
        }
    }
    const uint32_t drift = (uint32_t)(g_t * 1.2f), drift2 = (uint32_t)(g_t * 0.5f);
    for (int by2 = 0; by2 < H; by2 += 2) {
        float dy = by2 + 0.5f - sunY;
        for (int bx2 = 0; bx2 < W; bx2 += 2) {
            float dx = bx2 + 0.5f - sunX;
            float r = sqrtf(dx * dx + dy * dy);
            if (r < 6.f) continue;
            float kf = r / RAY_STEP - 1.f;
            if (kf >= RAY_NR - 1) continue;
            float ang = fastAtan2(dy, dx);
            if (ang < 0.f) ang += kTau;
            float bf = ang * (RAY_NB / kTau);
            int b0 = (int)bf, k0 = (int)kf;
            float fbb = bf - b0, fk = kf - k0;
            if (b0 >= RAY_NB) b0 -= RAY_NB;
            int b1 = b0 + 1 >= RAY_NB ? 0 : b0 + 1;
            const uint8_t* r0 = s_rayT + b0 * RAY_NR + k0;
            const uint8_t* r1 = s_rayT + b1 * RAY_NR + k0;
            float t0 = r0[0] + (r0[1] - r0[0]) * fk;
            float t1 = r1[0] + (r1[1] - r1[0]) * fk;
            float T = (t0 + (t1 - t0) * fbb) * (1.f / 255.f);
            float B = T * str * (1.f - r / (RAY_NR * RAY_STEP));
            if (B < 0.02f) continue;
            for (int y = by2; y < by2 + 2 && y < H; y++)
            for (int x = bx2; x < bx2 + 2; x++) {
                uint8_t& p = fb[y * W + x];
                // dust motes drifting in the light
                if (B > 0.32f && !s_rainbowSwap && y > HOR - 24 && (hash32((uint32_t)(x + g_pan + drift) * 0x9E3779B1u ^ (uint32_t)(y - drift2) * 0x85EBCA77u) & 1023u) < (uint32_t)(B * 1.6f)) {
                    p = R_SUN + 13;
                    continue;
                }
                int b = (int)(B * 6.5f + dith(x, y) - 0.45f);
                if (b <= 0) continue;
                if (p < R_SUN || (p >= R_MTN_FAR && p < R_HILLS)) { int lim = p | 15; p = (uint8_t)(p + b > lim ? lim : p + b); }
                else if (isWaterI(p)) {
                    int q = p - R_WATER;
                    if (q >= R_SUN && q < R_SUN + 16) continue;
                    int lim = (q >= R_HILLS && q < R_GROUND) ? (p | 7) : (p | 15);
                    p = (uint8_t)(p + b > lim ? lim : p + b);
                } else if (isGround(p)) {
                    int sh = (p - R_GROUND) & 3;
                    p = groundShade(p, sh + b > 3 ? 3 : sh + b);                            // a shaft lands on the ground
                } else if ((p >= R_LEAF && p < R_LEAF + 8) || (p >= R_BARK && p < R_ACCENT)) {
                    int lim = p | 7;                                            // near leaves, bark, stone: just catch the light
                    p = (uint8_t)(p + 1 > lim ? lim : p + 1);
                } else if ((p >= R_HILLS && p < R_ACCENT)) {
                    if (b > 2) b = 2;
                    for (int k = 0; k < b; k++) p = fogStep(p);    // light scattering in front of solid things reads as haze
                }
            }
        }
    }
}

// ---------------- weather & sky moments ----------------
static inline void veilRow(int y) {
    uint8_t* row = fb + y * W;
    for (int x = 0; x < W; x++) {
        uint8_t& p = row[x];
        float a = s_fogAmt[p];
        if (a <= 0.f) continue;
        float r = ign(x, y);
        if (r < a) p = fogStep(p);
        if (r < a - 0.5f) p = fogStep(p);
    }
}

// Snowflakes or autumn leaves: slow, swaying, keyed to the world so they don't slide as you turn.
static void drawDrift(int n, bool leaves, bool petals = false) {
    for (int k = 0; k < n; k++) {
        uint32_t h = hash32((uint32_t)k * 0x632BE5ABu + (leaves ? 0x1EAFu : 0x5A0Bu));
        float depth = hashf(h);
        float v = (leaves ? 9.f : 14.f) + 16.f * (1.f - depth);
        float period = (H + 20.f) / v;
        float tt = fmodf(g_t + hashf(h ^ 1u) * period, period);
        float y = -10.f + tt * v;
        float span = W * 1.5f;
        float sway = sinf(g_t * (1.2f + hashf(h ^ 3u)) + hashf(h ^ 4u) * 6.28f) * (leaves ? 7.f : 3.f) * (1.f - depth * 0.5f);
        float x = fmodf(hashf(h ^ 2u) * span - g_pan * (0.5f + 0.5f * (1.f - depth)) + sway + g_scene.wind * y * 0.3f * g_windLat + span * 64.f, span) - W * 0.25f;
        uint8_t c;
        if (petals) c = (sinf(g_t * 4.f + k) > 0.f) ? R_ACCENT + A_PINK_L : R_ACCENT + A_WHITE;
        else if (leaves) {
            bool flip = sinf(g_t * 5.f + k) > 0.f;             // tumbling: lit side, then shade side
            c = leafI(flip ? 6 : 4, false);
        } else c = depth < 0.5f ? R_CLOUD + 15 : R_CLOUD + 12;
        put((int)x, (int)y, c);
        if (depth < 0.3f) put((int)x + 1, (int)y, c);          // near ones are a touch bigger
    }
}

static void drawRain() {
    const Scene& sc = g_scene;
    if (g_fallLeaves > 0.05f) drawDrift((int)(g_fallLeaves * (22.f + 20.f * sc.gust)), true);
    if (g_petals > 0.05f) drawDrift((int)(g_petals * (14.f + 40.f * sc.gust * sc.gust)), true, true);   // gusts make petal storms
    if (sc.rain < 0.03f) return;
    if (g_snow > 0.5f) { drawDrift((int)(sc.rain * 260.f), false); return; }   // in the north it snows
    int N = (int)(sc.rain * 230.f + sc.storm * 140.f);
    float slant = (0.12f + sc.wind * 0.55f) * g_windLat;          // rain blows from the west
    float yawShift = g_yaw * PXR;
    for (int k = 0; k < N; k++) {
        uint32_t h = hash32((uint32_t)k * 0x9E3779B1u + 0x51A1u);
        float depth = hashf(h);
        float v = 150.f + 150.f * (1.f - depth);
        float len = 2.f + (1.f - depth) * 5.f;
        float period = (H + 30.f) / v;
        float tt = fmodf(g_t + hashf(h ^ 1u) * period, period);
        float y = -15.f + tt * v;
        float span = W * 1.5f;
        float x = fmodf(hashf(h ^ 2u) * span - yawShift * (0.5f + 0.5f * (1.f - depth)) + y * slant + span * 64.f, span) - W * 0.25f;
        uint8_t c = depth < 0.4f ? R_CLOUD + 13 : R_CLOUD + 10;
        int n = (int)len;
        for (int j = 0; j < n; j++) {
            int px = (int)(x - slant * j), py = (int)(y - j);
            if (depth > 0.6f && ((px + py) & 1)) continue;
            put(px, py, c);
        }
    }
    // splashes: rings on the lake, ticks on the ground
    int M = (int)(sc.rain * 34.f);
    uint32_t slice = (uint32_t)(g_t * 8.f);
    for (int k = 0; k < M; k++) {
        uint32_t h = hash32((uint32_t)k * 0x632BE5ABu ^ slice * 0x85EBCA77u);
        int x = (int)(hashf(h) * W), y = HOR + 2 + (int)(hashf(h ^ 7u) * (H - HOR - 2));
        uint8_t p = fb[y * W + x];
        if (isWaterI(p)) {
            uint8_t c = R_WATER + R_SKY + 13;
            int r = 1 + (int)(fmodf(g_t * 8.f, 1.f) * 2.f);
            put(x - r, y, c); put(x + r, y, c); put(x, y - 1, c);
        } else if (p >= R_GROUND && p < R_LEAF) put(x, y, R_CLOUD + 11);
    }
}

static void drawBolt() {
    const Scene& sc = g_scene;
    if (sc.boltAge > 0.3f || sc.flash < 0.2f) return;
    float rel = wrapPi(sc.boltAz - g_yaw);
    if (fabsf(rel) > FOV * 0.5f + 0.1f) return;
    uint32_t h = sc.boltSeed;
    int x = (int)(W * 0.5f + rel * PXR), y = 6;
    while (y < HOR - 1) {
        h = hash32(h + 0x9E3779B9u);
        int ny = y + 3 + (int)(h % 4u), nx = x + (int)((h >> 4) % 7u) - 3;
        line(x + 1, y, nx + 1, ny, R_SUN + 11);
        line(x - 1, y, nx - 1, ny, R_SUN + 11);
        line(x, y, nx, ny, R_SUN + 15);
        if (((h >> 9) & 7u) == 0) {                               // a branch
            int bx = nx + (((h >> 12) & 1u) ? 6 : -6), by = ny + 5 + (int)((h >> 13) % 6u);
            line(nx, ny, bx, by, R_SUN + 12);
        }
        x = nx; y = ny;
    }
}

static void drawRainbow() {
    if (!s_rainbowSwap) return;
    const Scene& sc = g_scene;
    float k = sc.rainbow * sunVis;
    float rel = wrapPi(SUN_AZ + PI_F - g_yaw);
    float cx = W * 0.5f + rel * PXR, cy = HOR - 5.f + S.elev * 80.f;     // the antisolar point
    const float R1 = 0.46f * PXR, R2 = 0.57f * PXR, bw = 7.f;     // dream-sized: the whole bow fits the view
    for (int y = 0; y < HOR; y++) {
        float dy = y - cy;
        float fade = k * clampf((HOR - y) / 8.f, 0.35f, 1.f);
        for (int x = 0; x < W; x++) {
            uint8_t& p = fb[y * W + x];
            if (p >= R_GROUND) continue;
            float dx = x - cx;
            float d = sqrtf(dx * dx + dy * dy);
            float dt = dith(x, y);
            if (d > R1 - bw * 0.5f && d < R1 + bw * 0.5f) {
                if (dt < fade * 0.85f) p = R_SUN + clampi((int)((d - (R1 - bw * 0.5f)) / bw * 15.99f), 0, 15);
            } else if (d > R2 - bw * 0.6f && d < R2 + bw * 0.6f) {
                if (dt < fade * 0.38f) p = R_SUN + 15 - clampi((int)((d - (R2 - bw * 0.6f)) / (bw * 1.2f) * 15.99f), 0, 15);
            } else if (d < R1 - bw * 0.5f) {
                if (p < R_CLOUD && dt < fade * 0.35f) p = (uint8_t)((p + 1) > (p | 15) ? p : p + 1);   // brighter inside the bow
            } else if (d < R2 - bw * 0.6f) {
                if (p < R_CLOUD && (p & 15) > 0 && dt < fade * 0.3f) p = p - 1;                    // Alexander's dark band
            }
        }
    }
}

static void drawAurora() {
    const Scene& sc = g_scene;
    float au = sc.aurora * S.night;
    if (au < 0.02f) return;
    for (int x = 0; x < W; x++) {
        float a = colAng[x];
        float d = wrapPi(a - AZ_NORTH);                                  // the aurora hangs in the north
        float wgt = expf(-d * d / (0.9f + 2.f * g_auroraBoost));          // in the far north it fills the sky
        if (wgt < 0.03f) continue;
        float n = fbmP(angU(a, 48), g_t * 0.06f, 48, 3, 0xA0A0u);
        float I = clampf((n - 0.36f) / 0.3f, 0.f, 1.f);
        I = I * I * (3.f - 2.f * I) * wgt * au;
        if (I < 0.03f) continue;
        float yb = 46.f + 6.f * sinf(a * 3.f + g_t * 0.12f) + 10.f * (fbmP(angU(a, 8), g_t * 0.03f, 8, 2, 0xA1A1u) - 0.5f);
        float hgt = (30.f + 22.f * fbmP(angU(a, 12), g_t * 0.05f, 12, 2, 0xA2A2u)) * (1.f + 0.5f * g_auroraBoost);
        int y0 = clampi((int)(yb - hgt), 0, HOR - 1), y1 = clampi((int)yb, 0, HOR - 1);
        for (int y = y0; y <= y1; y++) {
            float pp = (y - (yb - hgt)) / hgt;
            float bb = I * powf(pp, 1.2f) * (pp > 0.88f ? 1.5f : 1.f);
            if (bb > 0.06f && ign(x, y) < 0.35f + bb * 3.f) fb[y * W + x] = R_CLOUD + clampi((int)(2.f + bb * 14.f + dith(x + 1, y)), 0, 15);
        }
    }
}

static void drawMeteors() {
    const Scene& sc = g_scene;
    float rate = sc.meteors * S.night;
    if (rate < 0.02f) return;
    const float slice = 0.5f;
    int k0 = (int)floorf(g_t / slice);
    for (int k = k0 - 2; k <= k0; k++) {
        uint32_t h = hash32((uint32_t)k ^ sc.daySeed ^ 0x3E7E0u);
        if (hashf(h) > rate * 0.45f) continue;
        float u = (g_t - k * slice) / 0.75f;
        if (u < 0.f || u > 1.f) continue;
        float rel = wrapPi(hashf(h ^ 1u) * kTau - g_yaw);
        if (fabsf(rel) > FOV * 0.5f + 0.2f) continue;
        float x0 = W * 0.5f + rel * PXR, y0 = 4.f + hashf(h ^ 2u) * 30.f;
        float ang = 0.4f + hashf(h ^ 3u) * 0.8f;
        float dx = cosf(ang) * ((h & 1u) ? 1.f : -1.f), dy = sinf(ang);
        float len = 30.f + hashf(h ^ 4u) * 30.f;
        float hx = x0 + dx * len * u, hy = y0 + dy * len * u;
        for (int j = 0; j < 11; j++) {
            int px = (int)(hx - dx * j), py = (int)(hy - dy * j);
            if (py >= HOR - 6) continue;
            float bright = (1.f - j / 11.f) * (1.f - u * 0.5f);
            if (dith(px, py) < bright * 1.3f) put(px, py, R_SUN + clampi(15 - j, 6, 15));
        }
    }
}

static void drawMurmuration() {
    const Scene& sc = g_scene;
    float m = sc.murmur;
    if (m < 0.02f) return;
    float az = SUN_AZ + sc.murmurAz + 0.15f * sinf(g_t * 0.05f);
    float rel = wrapPi(az - g_yaw);
    if (fabsf(rel) > FOV * 0.5f + 0.6f) return;
    // one breathing sheet of birds: stretch, rotate and fold it, and it starts to look alive
    float cx = W * 0.5f + rel * PXR, cy = 30.f + 7.f * sinf(g_t * 0.13f);
    float A = 34.f + 14.f * sinf(g_t * 0.21f), B = 9.f + 6.f * sinf(g_t * 0.17f + 1.f), phi = 0.5f * sinf(g_t * 0.09f);
    float cp = cosf(phi), sp = sinf(phi);
    for (int i = 0; i < 220; i++) {
        uint32_t h = hash32((uint32_t)i * 2654435761u + 77u);
        float u = hashf(h) * 2.f - 1.f, v = hashf(h ^ 0x55u) * 2.f - 1.f;
        float uu = u + 0.3f * sinf(v * 3.f + g_t * 0.6f);
        float vv = v + 0.55f * sinf(u * 2.5f + g_t * 0.8f);
        int x = (int)(cx + (uu * cp - vv * sp * 0.5f) * A), y = (int)(cy + (uu * sp * 0.5f + vv * cp) * B);
        if (y < HOR - 4 && dith(x, y) < m * 1.2f) put(x, y, R_ACCENT + A_BIRD);
    }
}

static void drawFloatingLanterns() {
    if (g_karst < 0.5f || (S.night < 0.2f && g_scene.tod < 0.74f)) return;
    for (int k = 0; k < 28; k++) {
        uint32_t h = hash32((uint32_t)k * 0x632BE5ABu ^ 0x1A7Eu);
        float a = g_yaw + (hashf(h) - 0.5f) * 1.6f + g_t * 0.004f * ((h & 1) ? 1.f : -1.f);
        float z = 6.f + hashf(h ^ 7u) * 40.f;
        int x = (int)(W * 0.5f + wrapPi(a - g_yaw) * PXR), y = (int)(HOR + ZK / z);
        if ((unsigned)x >= (unsigned)W || y >= H - 1 || y <= HOR) continue;
        uint8_t under = fb[y * W + x];
        if (!isWaterI(under) || under >= R_WATER + N_REFLECT + 16) continue;   // only where there is water
        bool flick = sinf(g_t * 3.f + k) > -0.7f;
        put(x, y - 1, flick ? R_ACCENT + A_YEL_L : R_ACCENT + A_YEL);
        if (z < 15.f) { put(x + 1, y - 1, R_ACCENT + A_YEL); put(x, y - 2, R_ACCENT + A_YEL_L); }
        if (y + 1 < H) put(x, y + 1, R_ACCENT + A_YEL);                  // its reflection
    }
}

static void drawCanyonLife() {
    if (g_desert < 0.5f) return;
    const Scene& sc = g_scene;
    if (S.night < 0.3f) {                                            // a hawk riding the thermals
        float ph = g_t * 0.25f;
        float az = sc.sunAz + 2.2f + 0.5f * sinf(g_t * 0.01f);
        float rel = wrapPi(az + 0.25f * cosf(ph) - g_yaw);
        if (fabsf(rel) < FOV * 0.6f) {
            int x = (int)(W * 0.5f + rel * PXR), y = (int)(18.f + 6.f * sinf(ph));
            bool glide = sinf(g_t * 0.7f) > -0.6f;
            uint8_t c = R_ACCENT + A_SIL;
            put(x, y, c); put(x - 1, y - (glide ? 0 : 1), c); put(x + 1, y - (glide ? 0 : 1), c);
            put(x - 2, y - 1, c); put(x + 2, y - 1, c);
        }
        uint32_t slice = (uint32_t)(g_t / 40.f);                       // a dust devil now and then
        if ((hash32(slice ^ sc.daySeed) & 3u) < 2u) {
            float life = fmodf(g_t, 40.f);
            if (life < 18.f) {
                float az2 = sc.sunAz + ((hash32(slice * 31u) & 255) / 255.f - 0.5f) * 3.f + life * 0.01f;
                float rel2 = wrapPi(az2 - g_yaw);
                if (fabsf(rel2) < FOV * 0.6f) {
                    int x0 = (int)(W * 0.5f + rel2 * PXR), base = HOR + 3;
                    float fade = life < 3.f ? life / 3.f : life > 15.f ? (18.f - life) / 3.f : 1.f;
                    for (int k = 0; k < 40; k++) {
                        float hy = (float)k / 40.f;
                        float ang = g_t * 5.f + k * 0.9f;
                        int x = x0 + (int)(cosf(ang) * (1.5f + hy * 5.f)), y = base - (int)(hy * 24.f);
                        if (dith(x, y) < fade * (1.f - hy * 0.6f)) put(x, y, R_GROUND + 3);
                    }
                }
            }
        }
    } else {                                                          // a coyote on a mesa top, under the moon
        float az = sc.sunAz + 2.9f + ((sc.daySeed >> 4) & 255) / 255.f * 0.6f;
        float rel = wrapPi(az - g_yaw);
        if (fabsf(rel) < FOV * 0.5f - 0.05f) {
            int x = (int)(W * 0.5f + rel * PXR);
            int top = (int)(HOR - 1 - hFar[clampi(x + 1, 0, W + 1)]);
            static const char* COY[] = {"....k", "...kk", "k.kkk", ".kkk.", ".k.k."};
            bool howl = fmodf(g_t, 30.f) < 6.f;
            for (int y = 0; y < 5; y++)
                for (int xx = 0; xx < 5; xx++)
                    if (COY[y][xx] == 'k') put(x - 2 + xx, top - 5 + y - (howl && y < 2 ? 1 : 0), R_ACCENT + A_SIL);
        }
    }
}

static void drawCoastLife() {
    if (g_ocean < 0.5f) return;
    const Scene& sc = g_scene;
    float west = PI_F;
    if (S.night < 0.4f) {
        for (int k = 0; k < 4; k++) {                                 // gulls drifting over the water
            float az = west + 0.5f * sinf(g_t * 0.05f + k * 1.7f) + (k - 1.5f) * 0.2f;
            float rel = wrapPi(az - g_yaw);
            if (fabsf(rel) > FOV * 0.55f) continue;
            int x = (int)(W * 0.5f + rel * PXR), y = (int)(22.f + 10.f * sinf(g_t * 0.13f + k) + k * 4);
            bool up = sinf(g_t * 3.f + k * 2.f) > 0.3f;
            uint8_t c = R_CLOUD + 15;
            put(x, y, c); put(x - 1, y - (up ? 1 : 0), c); put(x + 1, y - (up ? 1 : 0), c); put(x - 2, y - 1, c); put(x + 2, y - 1, c);
        }
        float low = clampf(1.f - S.elev / 0.25f, 0.f, 1.f) * (S.elev > -0.04f ? 1.f : 0.f);
        uint32_t slice = (uint32_t)(g_t / 45.f);
        if (low > 0.3f && (hash32(slice ^ sc.daySeed ^ 0x3A1Eu) & 1u)) {           // a whale breaching in the low sun
            float life = fmodf(g_t, 45.f);
            if (life < 4.f) {
                float az = west + ((hash32(slice * 7u) & 255) / 255.f - 0.5f) * 0.6f;
                float rel = wrapPi(az - g_yaw);
                if (fabsf(rel) < FOV * 0.5f) {
                    int x = (int)(W * 0.5f + rel * PXR), base = HOR + 3;
                    float rise = life < 1.4f ? sinf(life / 1.4f * PI_F) : 0.f;
                    int hgt = (int)(rise * 9.f);
                    for (int y = 0; y < hgt; y++) {
                        int hw = 1 + (y < hgt - 2 ? 1 : 0);
                        hspan(x - hw + y / 3, x + hw + y / 3, base - y, R_ACCENT + A_SIL);
                    }
                    if (life > 1.2f && life < 3.f) {                    // the splash
                        float k2 = (life - 1.2f) / 1.8f;
                        for (int i = 0; i < 18; i++) {
                            uint32_t g = hash32((uint32_t)i * 0x9E37u + slice);
                            int sx = x + (int)(((g & 255) / 255.f - 0.5f) * 10.f * (0.4f + k2));
                            int sy = base - (int)(((g >> 8) & 255) / 255.f * 6.f * (1.f - k2));
                            if (dith(sx, sy) < 1.f - k2 * 0.7f) put(sx, sy, R_CLOUD + 15);
                        }
                    }
                }
            }
        }
    } else {                                                          // a lighthouse on the far headland, its beam sweeping
        float az = west + 0.95f;
        float rel = wrapPi(az - g_yaw);
        if (fabsf(rel) < FOV * 0.6f) {
            int x = (int)(W * 0.5f + rel * PXR), base = HOR - 1;
            for (int y = 0; y < 6; y++) put(x, base - y, (y & 2) ? R_CLOUD + 12 : R_ACCENT + A_PINK_D);
            float ph = g_t * 1.1f;
            float facing = cosf(ph);
            int ly = base - 7;
            put(x, ly, R_SUN + (facing > 0.9f ? 15 : 11));
            if (facing > 0.9f) for (int k = 0; k < 10; k++) { int gx = x + (k % 5) - 2, gy = ly + (k / 5) * 2 - 1; if (dith(gx, gy) < 0.6f) put(gx, gy, R_SUN + 12); }
            int dir = sinf(ph) > 0.f ? 1 : -1;
            float len = 50.f * fabsf(sinf(ph));                         // the beam, fanning across the dark
            for (int k = 3; k < (int)len; k++) {
                int bx = x + dir * k, by2 = ly - k / 8;
                uint8_t& p = fb[clampi(by2, 0, H - 1) * W + clampi(bx, 0, W - 1)];
                if ((unsigned)bx < (unsigned)W && p < R_CLOUD && dith(bx, by2) < 0.6f - k / (len * 1.6f)) p = (uint8_t)((p | 15) < p + 3 ? (p | 15) : p + 3);
            }
        }
    }
}

static void drawFireflies() {
    const Scene& sc = g_scene;
    if (sc.fireflies < 0.02f) return;
    const Firefly* ff = fieldFireflies();
    float beats = audioBeats();
    for (int i = 0; i < FIREFLY_N; i++) {
        const Firefly& f = ff[i];
        float fx = f.x - g_camX, fy = f.y - g_camY;
        float rng = sqrtf(fx * fx + fy * fy);
        if (rng < 0.6f) continue;
        float rel = wrapPi(atan2f(fy, fx) - g_yaw);
        if (fabsf(rel) > FOV * 0.5f) continue;
        float s = PXR / rng;
        int sx = (int)(W * 0.5f + rel * PXR), sy = (int)(HOR + ZK / rng - f.h * s);
        // they flash together, a wave rolling through the swarm, in time with the music
        float ph = beats * 0.5f - rng * 0.04f - f.ph * 0.15f;
        float fr = ph - floorf(ph);
        float glow = (fr < 0.2f ? 1.f - fr / 0.2f : 0.f) * sc.fireflies;
        if (glow < 0.05f) continue;
        if (glow > 0.15f) { put(sx, sy, R_ACCENT + A_YEL_L); if (s > 14.f) put(sx + 1, sy, R_ACCENT + A_YEL_L); }
        if (s > 9.f && glow > 0.35f) {
            put(sx - 1, sy, R_ACCENT + A_YEL); put(sx + 1, sy, R_ACCENT + A_YEL);
            put(sx, sy - 1, R_ACCENT + A_YEL); put(sx, sy + 1, R_ACCENT + A_YEL);
        }
    }
}

// ---------------- foliage: approaching depth slots of splat-built plants ----------------
static inline uint8_t leafI(int s, bool far) { return (g_altFoliage ? R_HILLS : R_LEAF) + (far ? 8 : 0) + clampi(s, 0, 7); }
static inline uint8_t blossomI(int s, bool) { static const uint8_t B[8] = {A_PINK_D, A_PINK_D, A_PINK, A_PINK, A_PINK_L, A_PINK_L, A_WHITE, A_WHITE}; return R_ACCENT + B[clampi(s, 0, 7)]; }
static inline uint8_t barkI(int s, bool) { return R_BARK + clampi(s, 0, 7); }
static inline uint8_t stoneI(int s, bool far) { return far ? R_MTN_NEAR + clampi(s, 0, 7) * 2 : R_BARK + 8 + clampi(s, 0, 7); }

struct Lighting { float side; bool back; bool far; float sway; };   // sway: px the top moves in the wind
static void drawTrunk(int cx, int ytop, int by, int tw, int flareRows, const Lighting& L, uint32_t seed);

static void drawPineNear(float sx, float baseY, float s, float hW, const Lighting& L, uint32_t seed);
static void drawPine(float sx, float baseY, float s, float hW, const Lighting& L, uint32_t seed) {
    if (hW * s > 70.f) { drawPineNear(sx, baseY, s, hW, L, seed); return; }
    int hp = (int)(hW * s);
    int cx = (int)lroundf(sx), by = (int)lroundf(baseY);
    if (hp < 3) { line(cx, by, cx, by - 2, leafI(2, L.far)); return; }
    int trunkH = hp / 7, top = by - hp;
    int crownH = hp - trunkH;
    float maxW = hp * 0.2f;
    int tiers = clampi(3 + hp / 25, 3, 7);
    bool right = L.side >= 0.f;
    int tw = clampi((int)(s * 0.12f), 0, 6);
    (void)right;
    drawTrunk(cx, by - trunkH, by, tw, 2, L, seed);
    int yStart = top < 0 ? -top : 0;
    for (int yy = yStart; yy <= crownH; yy++) {
        int y = top + yy;
        if (y >= H) break;
        float p = yy / (float)crownH;
        float tp = p * tiers, fr = tp - floorf(tp);
        int wl = (int)(maxW * (0.12f + 0.88f * p) * (0.5f + 0.5f * fr) + 0.5f);
        int dark = L.back ? 1 : 1 + (p < 0.2f), lit = L.back ? 2 : 4 - (int)(p * 1.6f) + (fr < 0.25f);
        uint32_t hr = hash32(seed ^ (uint32_t)yy * 0x9E37u);
        int lshade = right ? dark : lit, rshade = right ? lit : dark;
        int cx0 = cx;
        cx = cx0 + (int)lroundf(L.sway * (1.f - p) * (1.f - p));
        hspan(cx - wl, cx - 1, y, leafI(lshade, L.far));
        hspan(cx, cx + wl, y, leafI(rshade, L.far));
        int nsp = wl / 3 + 1;   // needle clumps
        for (int k = 0; k < nsp && wl > 1; k++) {
            hr = hash32(hr + 0x9E3779B9u);
            int ox = (int)(hr % (uint32_t)(wl + 1));
            put(cx - ox, y, leafI(lshade + ((hr >> 12) & 1) * 2 - 1, L.far));
            put(cx + (int)((hr >> 16) % (uint32_t)(wl + 1)), y, leafI(rshade + ((hr >> 13) & 1) * 2 - 1, L.far));
        }
        if (fr > 0.82f) hspan(cx - wl, cx + wl, y, leafI(0, L.far));   // shadow under each tier
        int rim = lowSun > 0.3f || L.back ? 7 : 5;
        if (L.back) { put(cx - wl, y, leafI(rim, L.far)); put(cx + wl, y, leafI(rim, L.far)); }
        else put(right ? cx + wl : cx - wl, y, leafI(rim, L.far));
        cx = cx0;
    }
}


// Close up a pine is drooping tiers of needle clumps: each tier a dark underside with lit clumps
// on top toward the sun, built from splats like the broadleaf crowns.
static void drawPineNear(float sx, float baseY, float s, float hW, const Lighting& L, uint32_t seed) {
    int hp = (int)(hW * s);
    int cxBase = (int)lroundf(sx), by = (int)lroundf(baseY);
    int cx = cxBase;
    int trunkH = hp / 6, top = by - hp;
    int tw = clampi((int)(s * 0.12f), 1, 14);
    bool right = L.side >= 0.f;
    drawTrunk(cx, top + hp / 5, by, tw, hp / 30 + 2, L, seed);
    int tiers = clampi(4 + hp / 60, 4, 8);
    float crownH = (float)(hp - trunkH);
    float sd = right ? 1.f : -1.f;
    for (int t = 0; t < tiers; t++) {
        float p = (t + 1.f) / tiers;                     // 0 top .. 1 bottom
        float ty = top + crownH * (t + 0.7f) / tiers;
        float w = hp * 0.21f * (0.2f + 0.8f * p);
        float th = crownH / tiers * 0.95f;
        int cx = cxBase + (int)lroundf(L.sway * powf(1.f - p, 1.5f));
        if (ty + th < 0 || ty - th > H || cx + w < 0 || cx - w > W) continue;
        uint32_t h = hash32(seed ^ (uint32_t)t * 0x9E3779B9u);
        // silhouette: a drooping band, ragged at the bottom edge
        int xa = clampi((int)(cx - w), 0, W - 1), xb = clampi((int)(cx + w), 0, W - 1);
        for (int x = xa; x <= xb; x++) {
            float u = (x - cx) / w;
            float droop = u * u * th * 0.6f;
            uint32_t hr = hash32(seed ^ (uint32_t)t * 131u ^ (uint32_t)(int)((x - cx) / fmaxf(2.f, th * 0.08f)));
            float yTop = ty - th * 0.5f + droop;
            float yBot = ty + th * 0.3f + droop + (hr & 7) / 7.f * th * 0.18f;
            int dark = (u * sd > 0.f) ? 2 : 1;
            int yt = (int)yTop;
            for (int y = clampi(yt, 0, H); y < (int)yBot && y < H; y++)
                fb[y * W + x] = leafI(y > yBot - th * 0.12f ? 0 : dark, false);
            if ((unsigned)yt < (unsigned)H) {                    // the tier's top edge catches the light
                int e = L.back ? (fabsf(u) > 0.55f ? 7 : 2) : (u * sd > -0.25f ? (lowSun > 0.3f ? 6 : 5) : 3);
                fb[yt * W + x] = leafI(e, false);
            }
        }
        // needle clumps: small splats, lit toward the sun and on top
        int r = clampi((int)(th * 0.06f), 1, 4);
        int n = clampi((int)(w * th / (r * r * 4.f + 1.f) * 0.9f), 20, 420);
        for (int i = 0; i < n; i++) {
            h = hash32(h + 0x7F4A7C15u);
            float u = (h & 0xFFFF) / 32767.5f - 1.f;
            float vv = ((h >> 16) & 0xFF) / 255.f;
            float droop = u * u * th * 0.6f;
            float yy = ty - th * 0.5f + droop + vv * th * 0.7f;
            float xx = cx + u * w * 0.96f;
            float lit = 0.35f + 0.35f * u * sd - vv * 1.1f;
            int sh = clampi((int)(1.f + lit * 4.f), 0, 4);
            if (vv < 0.2f && u * sd > -0.3f) sh = 4;                               // lit clumps near the top
            if (L.back) sh = sh > 2 ? 2 : sh;
            splat((int)xx, (int)yy, r, leafI(sh, false));
        }
    }
}

typedef uint8_t (*ShadeFn)(int, bool);
static void blob(float cx, float cy, float Rx, float Ry, const Lighting& L, uint32_t seed, float sizeHint, ShadeFn shadeI) {
    int r = clampi((int)(fminf(Rx, Ry) * 0.11f), 0, 7);
    float area = 3.1416f * Rx * Ry;
    int n = clampi((int)(area / ((r + 0.7f) * (r + 0.7f) * 3.1416f) * 1.1f), 4, (int)(60 + sizeHint * 4));
    if (n > 260) n = 260;
    uint32_t h = seed;
    auto pass = [&](int count, float ox, float oy, float sc, int s0, int s1, int rr, bool rimOnly) {
        for (int i = 0; i < count; i++) {
            float u, v;
            int guard = 0;
            do {
                h = hash32(h + 0x9E3779B9u);
                u = (h & 0xFFFF) / 32767.5f - 1.f;
                v = (h >> 16) / 32767.5f - 1.f;
            } while (u * u + v * v > 1.f && ++guard < 6);
            float rr2 = u * u + v * v;
            if (rimOnly && (rr2 < 0.55f || v > 0.35f)) continue;
            int px = (int)(cx + ox * Rx + u * Rx * sc), py = (int)(cy + oy * Ry + v * Ry * sc);
            int sh = s0 + (int)((h >> 28) % (uint32_t)(s1 - s0 + 1));
            splat(px, py, rr, shadeI(sh, L.far));
        }
    };
    float sd = L.side >= 0.f ? 1.f : -1.f;
    pass(n, 0.f, 0.f, 1.f, 0, 1, r, false);
    if (L.back) {
        pass(n * 2 / 3, 0.f, -0.05f, 0.85f, 1, 2, r, false);
        pass(n, 0.f, 0.f, 1.0f, 6, 7, r > 1 ? r - 1 : r, true);
    } else {
        pass(n * 3 / 4, sd * 0.16f, -0.16f, 0.8f, 2, 4, r, false);
        pass(n / 2, sd * 0.32f, -0.3f, 0.52f, 5, 6, r > 1 ? r - 1 : r, false);
        pass(n / 5, sd * 0.45f, -0.35f, 0.5f, 7, 7, r > 2 ? r - 2 : 0, false);
    }
}
static inline void canopy(float cx, float cy, float Rx, float Ry, const Lighting& L, uint32_t seed, float sizeHint) {
    blob(cx, cy, Rx, Ry, L, seed, sizeHint, leafI);
}

// A trunk as a lit cylinder: light peaks toward the sun, vertical crevices in short dark clusters,
// a rim on the sun side, a dark edge on the shade side, roots flaring at the base, moss low on the shade side.
static void drawTrunk(int cx, int ytop, int by, int tw, int flareRows, const Lighting& L, uint32_t seed) {
    bool right = L.side >= 0.f;
    int rimSh = lowSun > 0.3f || L.back ? 7 : 6;
    for (int y = ytop < 0 ? 0 : ytop; y <= by && y < H; y++) {
        int up = by - y;
        int flare = up < flareRows ? (flareRows - up) * (tw / 2 + 1) / (flareRows + 1) : 0;
        int w = tw + flare;
        for (int x = cx - w; x <= cx + w; x++) {
            if ((unsigned)x >= (unsigned)W) continue;
            float u = (x - cx) / (w + 0.5f);
            float lu = right ? u : -u;                          // +1 = toward the sun
            int sh;
            if (L.back) sh = lu > 0.8f ? rimSh : (lu > 0.35f ? 2 : 1);
            else sh = (int)(1.5f + clampf(1.f - fabsf(lu - 0.4f) * 1.25f, 0.f, 1.f) * 4.f);
            if (w >= 2) {
                int col = (int)((u + 1.f) * w * 0.5f);              // bark column, anchored to the trunk
                uint32_t hc = hash32(seed ^ (uint32_t)col * 0x9E3779B1u);
                int seg = (up + (int)(hc & 7u)) / (3 + (int)((hc >> 3) & 3u));
                uint32_t hs = hash32(hc ^ (uint32_t)seg * 0x85EBCA77u);
                if ((hs & 7u) < 2u) sh -= 2;                      // a crevice
                else if ((hs & 15u) == 15u) sh += 1;              // a ridge catching light
                if (x == cx - w || x == cx + w) sh = (lu > 0.f) ? rimSh : 0;
                if (!L.far && up < (by - (ytop < 0 ? 0 : ytop)) * 6 / 10 + flareRows) {   // moss grows on the north side, low on the trunk
                    float nb = g_objBearing + PI_F - u * 1.5707963f;                       // which way this bit of bark faces
                    float m = cosf(nb - AZ_NORTH);
                    float lowK = 1.f - (float)up / (float)((by - (ytop < 0 ? 0 : ytop)) * 6 / 10 + flareRows + 1);
                    if (m > 0.35f && (float)((hs >> 5) & 15u) < m * lowK * 9.f) { put(x, y, leafI(2 + (int)((hs >> 9) & 1u), false)); continue; }
                }
            }
            put(x, y, barkI(clampi(sh, 0, 7), L.far));
        }
    }
}

static void drawBroad(float sx, float baseY, float s, float hW, float crW, const Lighting& L, uint32_t seed) {
    int cx = (int)lroundf(sx), by = (int)lroundf(baseY);
    int trunkH = (int)(hW * 0.55f * s);
    int tw = clampi((int)(s * 0.14f), 0, 14);
    int ytop = by - trunkH;
    drawTrunk(cx, ytop, by, tw, trunkH / 7 + 1, L, seed);
    if (s > 6.f) {   // limbs
        int lw = clampi(tw / 2, 0, 4);
        for (int k = -lw; k <= lw; k++) {
            int sw = (int)(L.sway * 0.6f);
            line(cx + k, ytop + trunkH / 6, cx - (int)(crW * s * 0.6f) + k + sw, ytop - (int)(crW * s * 0.4f), barkI(1, L.far));
            line(cx + k, ytop + trunkH / 10, cx + (int)(crW * s * 0.55f) + k + sw, ytop - (int)(crW * s * 0.5f), barkI(2, L.far));
        }
    }
    float Rx = crW * s * 1.15f, Ry = crW * s * 0.85f;
    if (g_blossom) blob(sx + L.sway, ytop - Ry * 0.55f, Rx, Ry, L, seed, s, blossomI);
    else canopy(sx + L.sway, ytop - Ry * 0.55f, Rx, Ry, L, seed, s);
}

static void drawBush(float sx, float baseY, float s, float sz, const Lighting& L, uint32_t seed) {
    float Rx = sz * s * 1.1f, Ry = sz * s * 0.7f;
    if (Rx < 1.f) { put((int)sx, (int)baseY - 1, leafI(2, L.far)); return; }
    canopy(sx + L.sway * 0.3f, baseY - Ry * 0.7f, Rx, Ry, L, seed, s * 0.3f);
}

static void drawFlowers(float sx, float baseY, float s, uint32_t seed) {
    static const uint8_t fam[4][2] = {{A_PINK, A_PINK_L}, {A_BLUE_D, A_BLUE}, {A_LAV, A_BLUE}, {A_YEL, A_WHITE}};
    int f = seed & 3;
    int n = 4 + (int)((seed >> 4) % 6);
    int r = s > 26.f ? 1 : 0;
    uint32_t h = seed;
    for (int i = 0; i < n; i++) {
        h = hash32(h + 0x632BE5ABu);
        int px = (int)(sx + ((h & 0xFF) / 127.5f - 1.f) * s * 0.7f);
        int py = (int)(baseY - ((h >> 8) & 0xFF) / 255.f * s * 0.25f);
        if (s > 14.f) line(px, py, px, py + (int)(s * 0.12f), leafI(2, false));
        splat(px, py, r, R_ACCENT + fam[f][(h >> 20) & 1]);
    }
}

static void drawReeds(float sx, float baseY, float s, float hgt, uint32_t seed, float sway) {
    uint32_t h = seed;
    int n = 6 + (int)(seed % 6u);
    for (int i = 0; i < n; i++) {
        h = hash32(h + 0x7F4A7C15u);
        float ox = ((h & 0xFF) / 127.5f - 1.f) * s * 0.35f;
        float len = (0.6f + ((h >> 8) & 0xFF) / 255.f * 0.4f) * hgt * s;
        float lean = (((h >> 16) & 0xFF) / 127.5f - 1.f) * len * 0.12f + sway * len * 0.06f;
        int x0 = (int)(sx + ox), y0 = (int)baseY, x1 = (int)(sx + ox + lean), y1 = (int)(baseY - len);
        line(x0, y0, x1, y1, leafI(2 + (int)((h >> 24) & 1), false));
        if (((h >> 26) & 3u) == 0 && len > 4.f) {               // a cattail head
            line(x1, y1 + 1, x1, y1 + 1 + (int)fmaxf(1.f, len * 0.12f), R_ACCENT + A_DEER_D);
        }
    }
}

// A stone lantern (toro): base, pillar, firebox, a broad roof and a finial. Lit from dusk.
static void drawLantern(float sx, float baseY, float s, float hgt, const Lighting& L) {
    int cx = (int)sx, by = (int)baseY, h = (int)(hgt * s);
    if (h < 4) { put(cx, by - 1, stoneI(4, L.far)); put(cx, by - 2, stoneI(3, L.far)); return; }
    bool right = L.side >= 0.f;
    auto slab = [&](int y0, int y1, int hw, int sh) {
        for (int y = y0; y <= y1; y++) {
            hspan(cx - hw, cx - 1, y, stoneI(right ? sh - 1 : sh + 1, L.far));
            hspan(cx, cx + hw, y, stoneI(right ? sh + 1 : sh - 1, L.far));
        }
    };
    int w = (int)fmaxf(1.f, h * 0.22f);
    slab(by - h * 12 / 100, by, w, 3);                                   // base
    slab(by - h * 45 / 100, by - h * 12 / 100, w / 2, 3);                // pillar
    slab(by - h * 70 / 100, by - h * 45 / 100, w * 3 / 4, 4);            // firebox
    bool dusk = S.night > 0.15f || g_scene.tod > 0.72f || g_scene.tod < 0.24f;
    int wy0 = by - h * 66 / 100, wy1 = by - h * 50 / 100, ww = (int)fmaxf(1.f, w * 0.4f);
    for (int y = wy0; y <= wy1; y++) hspan(cx - ww, cx + ww, y, dusk ? (uint8_t)(R_ACCENT + A_YEL_L) : stoneI(0, L.far));
    if (dusk && !L.far) {                                                // a soft glow around the window
        for (int k = 0; k < 14; k++) {
            uint32_t g = hash32((uint32_t)k * 0x9E37u + (uint32_t)cx);
            int gx = cx + (int)((g & 15) - 7.5f) * ww / 2, gy = (wy0 + wy1) / 2 + (int)(((g >> 4) & 15) - 7.5f) * (wy1 - wy0 + 2) / 6;
            if (dith(gx, gy) < 0.5f) put(gx, gy, R_ACCENT + A_YEL);
        }
    }
    for (int y = by - h * 84 / 100; y <= by - h * 70 / 100; y++) {      // roof: wide, sloping to upturned eaves
        float u = (float)(y - (by - h * 84 / 100)) / fmaxf(1.f, h * 0.14f);
        int rw = (int)(w * (0.5f + 0.9f * u));
        hspan(cx - rw, cx - 1, y, stoneI(right ? 2 : 5, L.far));
        hspan(cx, cx + rw, y, stoneI(right ? 5 : 2, L.far));
    }
    slab(by - h, by - h * 84 / 100, (int)fmaxf(0.f, w * 0.18f), 4);       // finial
}

// Bamboo: a clump of tall green culms with darker nodes, leaf sprays toward the top, swaying together.
static void drawBamboo(float sx, float baseY, float s, float hgt, float spread, uint32_t seed, float sway) {
    uint32_t h = seed;
    int n = 5 + (int)(seed % 5u);
    for (int i = 0; i < n; i++) {
        h = hash32(h + 0x9E3779B9u);
        float ox = ((h & 0xFF) / 127.5f - 1.f) * spread * s;
        float len = (0.7f + ((h >> 8) & 0xFF) / 255.f * 0.3f) * hgt * s;
        int x0 = (int)(sx + ox), y0 = (int)baseY;
        int x1 = (int)(sx + ox + sway * 0.8f + ((h >> 20) & 7) - 3.5f), y1 = (int)(baseY - len);
        int wpx = s > 22.f ? 1 : 0;
        float node = fmaxf(3.f, s * 0.45f);
        for (int y = y0; y >= y1; y--) {
            float t = (float)(y0 - y) / fmaxf(1.f, (float)(y0 - y1));
            int x = x0 + (int)((x1 - x0) * t * t);
            bool nd = fmodf((float)(y0 - y) + (h & 7), node) < 1.f;
            uint8_t c = nd ? (uint8_t)(R_HILLS + 1) : (uint8_t)(R_GROUND + 2 + ((h >> 28) & 1));
            for (int k = 0; k <= wpx; k++) put(x + k, y, c);
            if (wpx && !nd) put(x + wpx + 1, y, R_HILLS + 2);
        }
        int leaves = 3 + (int)(len / 10.f);                                   // sprays of narrow leaves
        uint32_t q = h;
        for (int k = 0; k < leaves; k++) {
            q = hash32(q + 0x7F4A7C15u);
            float t = 0.45f + (q & 0xFF) / 255.f * 0.55f;
            int lx = x0 + (int)((x1 - x0) * t * t), ly = y0 - (int)((y0 - y1) * t);
            int dir = (q >> 8) & 1 ? 1 : -1;
            int ll = 2 + (int)(s * 0.15f);
            line(lx, ly, lx + dir * ll, ly + ll / 3, (q >> 9) & 1 ? (uint8_t)(R_GROUND + 3) : (uint8_t)(R_HILLS + 4));
        }
    }
}

// A torii: two vermilion pillars, a straight tie beam, and a dark top beam with lifted ends.
static void drawTorii(float sx, float baseY, float s, float wid, float hgt, const Lighting& L) {
    int cx = (int)sx, by = (int)baseY, hw = (int)(wid * 0.5f * s), h = (int)(hgt * s);
    if (h < 4) { put(cx, by - 1, R_ACCENT + A_BLUE_D); return; }
    int pw = (int)fmaxf(0.f, s * 0.12f);
    bool right = L.side >= 0.f;
    for (int side = -1; side <= 1; side += 2) {
        int px = cx + side * (hw - pw - 1);
        for (int y = by - h; y <= by; y++) {
            hspan(px - pw, px, y, R_ACCENT + A_BLUE_D);
            if (pw > 0) put(right ? px + pw : px - pw, y, R_ACCENT + (right ? A_PINK : A_PINK_D));
        }
        hspan(px - pw - 1, px + pw + 1, by, R_ACCENT + A_SIL);                // black foot
    }
    int nb = (int)fmaxf(1.f, s * 0.1f);
    for (int y = by - h * 78 / 100; y < by - h * 78 / 100 + nb; y++) hspan(cx - hw + 1, cx + hw - 1, y, R_ACCENT + A_BLUE_D);   // nuki
    int kb = (int)fmaxf(1.f, s * 0.14f);
    for (int y = by - h - kb; y < by - h; y++) hspan(cx - hw - 2, cx + hw + 2, y, R_ACCENT + A_BLUE_D);                    // shimaki
    for (int x = -hw - 3; x <= hw + 3; x++) {                                                                             // kasagi, ends lifting
        float e = fabsf((float)x) / (hw + 3.f);
        int lift = (int)(e * e * e * s * 0.3f);
        for (int k = 0; k <= kb; k++) put(cx + x, by - h - kb - 1 - k - lift, R_ACCENT + A_SIL);
    }
}

// Saguaro: a ribbed green column with an arm or two turning up; tiny light spines.
static void drawSaguaro(float sx, float baseY, float s, float hgt, float arms, const Lighting& L, uint32_t seed) {
    int cx = (int)sx, by = (int)baseY, h = (int)(hgt * s);
    int w = (int)fmaxf(1.f, s * 0.22f);
    bool right = L.side >= 0.f;
    auto column = [&](int x0, int yTop, int yBot, int hw) {
        for (int y = yTop < 0 ? 0 : yTop; y <= yBot && y < H; y++)
            for (int x = x0 - hw; x <= x0 + hw; x++) {
                float u = (x - x0) / (hw + 0.5f);
                float lu = right ? u : -u;
                int sh = 2 + (int)(clampf(1.f - fabsf(lu - 0.35f), 0.f, 1.f) * 3.f);
                if (hw >= 2 && ((x - x0 + 16) % 2) == 0) sh -= 1;                  // ribs
                if (y - yTop < hw) sh += 1;                                          // the rounded crown
                if (((x * 7 + y * 13 + (int)seed) & 31) == 0) { put(x, y, R_ACCENT + A_WHITE); continue; }   // spines
                put(x, y, leafI(clampi(sh, 0, 7), L.far));
            }
    };
    column(cx, by - h, by, w);
    int na = arms > 0.66f ? 2 : arms > 0.25f ? 1 : 0;
    for (int k = 0; k < na; k++) {
        int side = (k == 0) ? ((seed & 1) ? 1 : -1) : -((seed & 1) ? 1 : -1);
        int ay = by - h * (40 + 15 * k) / 100, aw = (int)fmaxf(1.f, w * 0.7f);
        int ax = cx + side * (w + aw + (int)(s * 0.25f));
        for (int x = cx + side * w; side > 0 ? x <= ax : x >= ax; x += side) for (int y = ay - aw; y <= ay; y++) put(x, y, leafI(2, L.far));
        column(ax, ay - h * 30 / 100, ay, aw);
    }
}
// Prickly pear: a cluster of flat oval pads, a few yellow blooms.
static void drawPear(float sx, float baseY, float s, float sz, uint32_t seed) {
    uint32_t h = seed;
    int n = 3 + (int)(seed % 4u);
    float r = fmaxf(1.f, sz * s * 0.3f);
    for (int i = 0; i < n; i++) {
        h = hash32(h + 0x9E3779B9u);
        float ox = ((h & 0xFF) / 127.5f - 1.f) * sz * s * 0.5f, oy = ((h >> 8) & 0xFF) / 255.f * sz * s * 0.7f;
        int cx = (int)(sx + ox), cy = (int)(baseY - r - oy);
        for (int dy = -(int)r; dy <= (int)r; dy++) {
            int hw = (int)(r * 0.7f * sqrtf(fmaxf(0.f, 1.f - (dy * dy) / (r * r))));
            hspan(cx - hw, cx + hw, cy + dy, leafI(dy < 0 ? 4 : 2, false));
        }
        if (((h >> 20) & 3u) == 0) put(cx, cy - (int)r, R_ACCENT + A_YEL_L);
    }
}

static void drawFern(float sx, float baseY, float s, uint32_t seed, float sway) {
    uint32_t h = seed;
    int n = 4 + (int)(seed % 4u);
    for (int i = 0; i < n; i++) {                                  // arching fronds with leaflets
        h = hash32(h + 0x7F4A7C15u);
        float dir = ((h & 1) ? 1.f : -1.f) * (0.4f + ((h >> 1) & 0xFF) / 255.f * 0.8f);
        float len = (0.5f + ((h >> 9) & 0xFF) / 255.f * 0.5f) * s * 0.9f;
        int steps = (int)fmaxf(3.f, len);
        for (int k = 0; k <= steps; k++) {
            float t = (float)k / steps;
            float x = sx + dir * len * t + sway * t * 0.3f, y = baseY - len * 0.75f * sinf(t * 2.6f) * (1.f - 0.25f * t);
            uint8_t c = R_GROUND + 2 + ((k + i) & 1);
            put((int)x, (int)y, c);
            if (len > 6.f && (k & 1) && t > 0.1f && t < 0.9f) { put((int)x, (int)y - 1, R_GROUND + 3); put((int)x, (int)y + 1, R_GROUND + 1); }
        }
    }
}

// A redwood: a colossal fluted trunk leaving the top of the frame, foliage far above in sparse clumps.
static void drawRedwood(float sx, float baseY, float s, float hgt, float radius, const Lighting& L, uint32_t seed) {
    int cx = (int)lroundf(sx), by = (int)lroundf(baseY);
    int tw = clampi((int)(radius * s), 1, 60);
    int top = by - (int)(hgt * s);
    drawTrunk(cx, top < -40 ? -40 : top, by, tw, (int)(radius * s * 0.9f) + 2, L, seed);
    uint32_t h = seed;
    int clumps = 6 + (int)(hgt * 0.3f);
    for (int k = 0; k < clumps; k++) {                             // foliage high up, mostly above you
        h = hash32(h + 0x9E3779B9u);
        float t = 0.5f + (h & 0xFF) / 255.f * 0.5f;
        int y = by - (int)(hgt * s * t);
        if (y < -20 || y > H) continue;
        int side = (h >> 8) & 1 ? 1 : -1;
        float R = (1.f + ((h >> 9) & 0xFF) / 255.f * 1.5f) * s;
        line(cx + side * tw, y + (int)(R * 0.3f), cx + side * (tw + (int)R), y, barkI(1, L.far));   // a branch
        blob(cx + side * (tw + R * 0.8f), (float)y, R, R * 0.45f, L, h, s * 0.4f, leafI);
    }
}

// The tunnel tree: a redwood so vast an arch has been opened through its base. Walk through it.
static void drawTunnelTree(float sx, float baseY, float s, float hgt, float radius, const Lighting& L, uint32_t seed) {
    int cx = (int)lroundf(sx), by = (int)lroundf(baseY);
    int tw = clampi((int)(radius * s), 2, 140);
    int top = by - (int)(hgt * s);
    // draw the trunk, then carve the opening so the forest beyond shows through
    static uint8_t* keep = nullptr;                                     // runtime buffer (PSRAM): too big for static RAM
    if (!keep) keep = (uint8_t*)memSlow(W * H);
    if (!keep) return;
    int hw = (int)(tw * 0.42f), hh = (int)(radius * 1.15f * s);
    int x0 = clampi(cx - hw, 0, W - 1), x1 = clampi(cx + hw, 0, W - 1), y0 = clampi(by - hh, 0, H - 1), y1 = clampi(by, 0, H - 1);
    for (int y = y0; y <= y1; y++) memcpy(keep + y * W + x0, fb + y * W + x0, x1 - x0 + 1);
    drawTrunk(cx, top < -40 ? -40 : top, by, tw, (int)(radius * s * 0.6f) + 2, L, seed);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            float u = (x - cx) / (float)(hw + 1), v = (by - y) / (float)(hh + 1);
            float archTop = v < 0.6f ? 1.f : sqrtf(fmaxf(0.f, 1.f - ((v - 0.6f) / 0.4f) * ((v - 0.6f) / 0.4f)));
            if (fabsf(u) < archTop) {
                fb[y * W + x] = keep[y * W + x];
                if (fabsf(u) > archTop - 0.12f) fb[y * W + x] = barkI(0, L.far);            // the opening's dark rim
            }
        }
    uint32_t h = seed;
    for (int k = 0; k < 10; k++) {                                       // a few branches far overhead
        h = hash32(h + 0x9E3779B9u);
        int y = by - (int)(hgt * s * (0.55f + (h & 0xFF) / 255.f * 0.4f));
        if (y < -20 || y > H) continue;
        int side = (h >> 8) & 1 ? 1 : -1;
        float R = (1.2f + ((h >> 9) & 0xFF) / 255.f * 1.8f) * s;
        blob(cx + side * (tw + R * 0.8f), (float)y, R, R * 0.45f, L, h, s * 0.4f, leafI);
    }
}

// A giant hollow log lying across the way, seen end-on: walk into the dark and out the far end.
static void drawHollowLog(float sx, float baseY, float s, float diam, const Lighting& L, uint32_t seed) {
    int cx = (int)sx;
    float R = diam * 0.5f * s;
    int cy = (int)(baseY - R * 0.92f);
    if (R < 2.f) { put(cx, (int)baseY - 1, barkI(3, L.far)); return; }
    float rIn = R * 0.66f, rEnd = R * 0.16f;
    float lx = L.side >= 0.f ? 0.6f : -0.6f;
    for (int dy = -(int)R - 1; dy <= (int)R + 1; dy++)
        for (int dx = -(int)R - 1; dx <= (int)R + 1; dx++) {
            float d = sqrtf((float)(dx * dx + dy * dy));
            float a = atan2f((float)dy, (float)dx);
            float ragged = R * (0.94f + 0.06f * vnoiseP(a * 3.f + 10.f, (float)(seed & 255), 64, seed));
            if (d > ragged) continue;
            int x = cx + dx, y = cy + dy;
            uint8_t c;
            if (d > rIn) {                                                // the shell, lit from the sun's side, moss over the top
                float nx = dx / (d + 0.01f), ny = dy / (d + 0.01f);
                int sh = (int)(3.f + (nx * lx - ny * 0.6f) * 2.5f);
                uint32_t g = hash32((uint32_t)(dx + 200) * 0x9E37u ^ (uint32_t)(dy + 200) * 0x85EBu ^ seed);
                if ((g & 15u) == 0) sh -= 2;                              // splits in the bark
                c = barkI(clampi(sh, 0, 7), L.far);
                if (ny < -0.55f && (g & 3u) != 0) c = R_GROUND + 1 + ((g >> 3) & 1) + (ny < -0.8f ? 1 : 0);
                if (d < rIn + 1.2f) c = barkI(1, L.far);                  // the inner lip
            } else if (d > rEnd) {                                        // the hollow: deep dark, the far rim just visible
                c = barkI(0, L.far);
                if (d < rEnd * 2.f && dith(x, y) < 0.35f) c = R_GROUND + 0;
            } else {                                                      // a glimmer of daylight at the far end
                c = d < rEnd * 0.55f ? (uint8_t)(R_GROUND + 3) : (dith(x, y) < 0.5f ? (uint8_t)(R_GROUND + 2) : barkI(0, L.far));
            }
            put(x, y, c);
        }
}

// A fallen log, mossy on top, the cut end showing its rings.
static void drawLog(float sx, float baseY, float s, float len, float diam, const Lighting& L, uint32_t seed) {
    int hl = (int)(len * 0.5f * s), r = (int)fmaxf(1.f, diam * 0.5f * s);
    int cx = (int)sx, cy = (int)baseY - r;
    for (int dy = -r; dy <= r; dy++) {
        float v = (float)dy / (r + 0.5f);
        int sh = (int)(4.f - (v + 0.3f) * 3.f);
        for (int x = cx - hl; x <= cx + hl; x++) {
            uint32_t g = hash32((uint32_t)(x - cx + 1000) * 0x9E37u ^ (uint32_t)dy * 0x85EBu ^ seed);
            uint8_t c = barkI(clampi(sh - ((g & 7u) == 0 ? 2 : 0), 0, 7), L.far);
            if (v < -0.45f && (g & 3u) != 0) c = R_GROUND + 2 + ((g >> 3) & 1);             // moss along the top
            put(x, cy + dy, c);
        }
    }
    int ex = cx + ((seed & 1) ? hl : -hl);                              // the cut end
    for (int dy = -r; dy <= r; dy++) for (int dx = -r / 2; dx <= r / 2; dx++)
        if (dx * dx * 4 + dy * dy <= r * r) put(ex + dx, cy + dy, ((dx * dx * 4 + dy * dy) * 3 / (r * r + 1)) & 1 ? barkI(6, L.far) : barkI(4, L.far));
}

// Vines hanging from a rainforest giant's crown.
static void drawVines(float sx, float baseY, float s, float hgt, float crown, uint32_t seed, float sway) {
    uint32_t h = seed ^ 0x51E5u;
    int n = 1 + (int)(seed % 3u);
    for (int i = 0; i < n; i++) {
        h = hash32(h + 0x9E3779B9u);
        float ox = ((h & 255) / 127.5f - 1.f) * crown * 0.9f * s;
        int y0 = (int)(baseY - hgt * s * 0.72f), len = (int)(hgt * s * (0.15f + ((h >> 8) & 255) / 255.f * 0.3f));
        float swing = (((h >> 16) & 255) / 127.5f - 1.f) * crown * 0.5f * s;      // vines loop and drape, not plumb lines
        for (int k = 0; k < len; k++) {
            float t = (float)k / fmaxf(1.f, (float)len);
            int x = (int)(sx + ox + swing * sinf(t * PI_F) + sway * t * t * 0.8f);
            put(x, y0 + k, leafI((k / 3 + i) & 1 ? 1 : 2, false));
            if (((k + i * 3) % 7) == 0 && s > 14.f) put(x + 1, y0 + k, leafI(4, false));     // a small leaf
        }
    }
}

// Elephant ears: a few huge heart-shaped leaves on long stems.
static void drawBigLeaf(float sx, float baseY, float s, float sz, uint32_t seed, float sway) {
    uint32_t h = seed;
    int n = 3 + (int)(seed % 3u);
    for (int i = 0; i < n; i++) {
        h = hash32(h + 0x7F4A7C15u);
        float dir = ((h & 1) ? 1.f : -1.f) * (0.2f + ((h >> 1) & 255) / 255.f * 0.8f);
        float len = sz * s * (0.6f + ((h >> 9) & 255) / 255.f * 0.5f);
        int tx = (int)(sx + dir * len * 0.6f + sway), ty = (int)(baseY - len);
        line((int)sx, (int)baseY, tx, ty, leafI(2, false));
        float L = len * 0.55f, Wd = len * 0.32f;
        for (int k = 0; k < (int)L; k++) {                                   // the blade hangs from the stem tip
            float t = (float)k / fmaxf(1.f, L);
            int half = (int)(Wd * sinf(fminf(1.f, t * 1.3f) * PI_F * 0.9f) * (1.f - t * 0.4f));
            int y = ty + k, x0 = tx + (int)(dir * k * 0.5f);
            for (int dx = -half; dx <= half; dx++) {
                int sh = dx == 0 ? 6 : (dx < 0 ? 3 : 4) + (k < 2 ? 1 : 0);
                if (abs(dx) == half && half > 1) sh = 2;                         // darker rim
                else if (dx != 0 && ((k + abs(dx)) % 4) == 0) sh += 1;            // veins running out from the midrib
                put(x0 + dx, y, leafI(sh, false));
            }
        }
    }
}

static void drawTuft(float sx, float baseY, float s, uint32_t seed, float sway) {
    if (g_fern > 0.5f) { drawFern(sx, baseY, s, seed, sway); return; }
    uint32_t h = seed;
    int n = 4 + (int)(seed % 5);
    for (int i = 0; i < n; i++) {
        h = hash32(h + 0x7F4A7C15u);
        float ox = ((h & 0xFF) / 127.5f - 1.f) * s * 0.25f;
        float len = (0.25f + ((h >> 8) & 0xFF) / 255.f * 0.45f) * s;
        float lean = (((h >> 16) & 0xFF) / 127.5f - 1.f) * len * 0.5f + sway * len * 0.08f;
        line((int)(sx + ox), (int)baseY, (int)(sx + ox + lean), (int)(baseY - len), leafI(1 + (int)((h >> 24) & 3), false));
    }
}

// Faceted rock: per-pixel normal on an ellipsoid, snapped into hashed facets, lit by the sun side.
static void rockBody(float cx, float cy, float Rx, float Ry, const Lighting& L, uint32_t seed, float facets, bool moss) {
    int x0 = (int)floorf(cx - Rx), x1 = (int)ceilf(cx + Rx);
    int y0 = (int)floorf(cy - Ry), y1 = (int)ceilf(cy + Ry);
    if (x0 < 0) x0 = 0;
    if (x1 > W - 1) x1 = W - 1;
    if (y0 < 0) y0 = 0;
    if (y1 > H - 1) y1 = H - 1;
    const float lx = L.side * 0.75f, ly = -0.55f, lz = 0.45f;   // light: sun side, from above, a bit frontal
    for (int y = y0; y <= y1; y++) {
        float v = (y + 0.5f - cy) / Ry;
        if (v > 1.f) continue;
        for (int x = x0; x <= x1; x++) {
            float u = (x + 0.5f - cx) / Rx;
            // lumpy outline so it doesn't read as an ellipse
            uint32_t ho = hash32(seed ^ (uint32_t)(int)floorf(atan2f(v, u) * 2.2f + 8.f));
            float lump = 0.86f + 0.14f * ((ho & 255) / 255.f);
            float r2 = u * u + v * v;
            if (r2 > lump * lump) continue;
            // facet: Voronoi over jittered cell centres; every pixel takes its nearest centre's normal
            float gu = (u + 2.f) * facets, gv = (v + 2.f) * facets;
            int iu = (int)floorf(gu), iv = (int)floorf(gv);
            float best = 1e9f, second = 1e9f, fu = u, fv = v;
            uint32_t hf = 0;
            for (int ou = -1; ou <= 1; ou++)
                for (int ov = -1; ov <= 1; ov++) {
                    uint32_t hc = hash32(seed ^ (uint32_t)(iu + ou) * 0x9E3779B1u ^ (uint32_t)(iv + ov) * 0x85EBCA77u);
                    float pu = iu + ou + 0.15f + 0.7f * ((hc & 255) / 255.f);
                    float pv = iv + ov + 0.15f + 0.7f * (((hc >> 8) & 255) / 255.f);
                    float dd = (pu - gu) * (pu - gu) + (pv - gv) * (pv - gv);
                    if (dd < best) { second = best; best = dd; hf = hc; fu = pu / facets - 2.f; fv = pv / facets - 2.f; }
                    else if (dd < second) second = dd;
                }
            fu = fu * 0.8f + ((hf >> 16) & 255) / 255.f * 0.7f - 0.35f;     // each chip leans its own way
            fv = fv * 0.8f + ((hf >> 24) & 255) / 255.f * 0.7f - 0.35f;
            float fz2 = 1.f - fu * fu - fv * fv;
            float fz = fz2 > 0.f ? sqrtf(fz2) : 0.f;
            float d = fu * lx + fv * ly + fz * lz;
            float sh = 2.8f + d * 5.f;
            if (v > 0.55f) sh -= (v - 0.55f) * 4.f;                          // grounded, darker underside
            if (sqrtf(second) - sqrtf(best) < 0.09f) sh -= 1.6f;               // crease between chips
            if (L.back && r2 > 0.6f * lump * lump && v < 0.2f) sh = 7.f;      // rim light when backlit
            uint8_t c = stoneI(clampi((int)(sh + dith(x, y) * 0.9f), 0, 7), L.far);
            if (moss && !L.far && v < -0.62f && fv < -0.5f && ((hf >> 20) & 3) == 0) c = leafI(clampi((int)(1.5f + d * 4.f), 1, 5), false);
            put(x, y, c);
        }
    }
}

static void drawBoulder(float sx, float baseY, float s, float sz, const Lighting& L, uint32_t seed) {
    float Rx = sz * s * 1.15f, Ry = sz * s * 0.8f;
    if (Rx < 1.f) { put((int)sx, (int)baseY - 1, stoneI(3, L.far)); return; }
    hspan((int)(sx - Rx * 0.95f), (int)(sx + Rx * 0.95f), (int)baseY, stoneI(0, L.far));     // contact shadow
    rockBody(sx, baseY - Ry * 0.72f, Rx, Ry, L, seed, clampf(Rx / 9.f, 1.5f, 9.f), Rx > 4.f);
}

// Cliff outcrop: flat-topped rock built from ledges of staggered blocks, two faces (sun / shade).
static inline int fx_frozen(int cx, int hw, uint32_t seed) { return cx + (int)((((seed >> 4) & 255) / 255.f - 0.5f) * hw * 0.8f); }
static void drawOutcrop(float sx, float baseY, float s, float halfW, float hgt, const Lighting& L, uint32_t seed, uint8_t var) {
    const bool ruin = (var & 2) != 0;
    const bool karstP = (var & 64) != 0;
    const bool mesaRock = (var & 4) != 0, hoodoo = (var & 8) != 0 || karstP, arch = (var & 16) != 0;
    const bool layered = (mesaRock || (var & 8) || arch) && !karstP;   // sandstone: horizontal strata (limestone pillars are smooth)
    int hw = (int)(halfW * s), ht = (int)(hgt * s);
    int cx = (int)lroundf(sx), by = (int)lroundf(baseY);
    if (hw < 1 || ht < 1) { put(cx, by - 1, stoneI(3, L.far)); return; }
    bool sunRight = L.side >= 0.f;
    int splitX = cx + (sunRight ? -hw / 4 : hw / 4);
    float band = ruin ? fmaxf(3.f, s * 0.7f) : layered ? fmaxf(2.f, s * 0.35f) : fmaxf(6.f, s * 2.4f);    // walls: courses; sandstone: thin strata; rock: rare ledges
    float block = ruin ? fmaxf(3.f, s * 1.1f) : fmaxf(2.f, s * 0.45f);  // rock: narrow basalt columns
    float jagW = fmaxf(1.f, s * 0.35f);
    for (int dx = -hw; dx <= hw; dx++) {
        int x = cx + dx;
        if (x < 0 || x >= W) continue;
        float e = fabsf((float)dx) / hw;
        uint32_t hj = hash32(seed ^ (uint32_t)(int)floorf((dx + 4000) / jagW) * 0x9E37u);
        float jag = (mesaRock || arch) ? (hj & 3) / 3.f * 0.03f * ht                 // mesas: flat tops
                  : hoodoo ? (e < 0.6f ? 0.f : (e - 0.6f) * 0.25f * ht)              // hoodoos: a rounded cap
                  : ruin ? (hj & 7) / 7.f * 0.08f * ht                       // walls: stepped
                         : (vnoiseP((dx + 4000) / (jagW * 4.f), (float)(seed & 1023), 4096, seed) * 0.22f
                            + vnoiseP((dx + 4000) / (jagW * 1.2f), (float)(seed & 511), 4096, seed ^ 0x55u) * 0.06f) * ht;   // rock: a ragged skyline
        float q = e > 0.7f ? (e - 0.7f) / 0.3f : 0.f;
        int yt = (int)(by - ht + jag + q * q * 0.5f * ht);          // rounded shoulders
        bool litFace = (x > splitX) == sunRight;
        int face = L.back ? 2 : (litFace ? 5 : 2);
        float wave = layered ? 0.f : (vnoiseP((dx + 4000) / (block * 1.7f), (float)(seed & 255), 4096, seed) - 0.5f) * band * 1.4f;
        for (int y = yt < 0 ? 0 : yt; y <= by && y < H; y++) {
            if (arch) {                                             // the opening of the arch
                float ox = dx / (hw * 0.62f), oy = (by - y) / (ht * 0.62f);
                if (ox * ox + oy * oy < 1.f) continue;
            }
            float up = by - y + wave, d = y - yt;                   // ledges wander across the face
            int bi = (int)floorf(up / band);
            float inBand = up - bi * band;                           // 0 at the ledge's bottom
            uint32_t hb = hash32(seed ^ (uint32_t)bi * 0x632BE5ABu);
            float bw = block * (0.7f + ((hb >> 8) & 255) / 255.f * 1.3f);   // each ledge has its own block size
            float slant = ruin ? (((hb >> 16) & 255) / 255.f - 0.5f) * 0.8f : (((hb >> 16) & 255) / 255.f - 0.5f) * 0.15f;   // walls lean, columns stand
            float bx = (dx + 4000 + (hb & 255) / 255.f * bw + inBand * slant) / bw;
            int bk = (int)bx;
            float inBlock = bx - bk;
            uint32_t hk = hash32(hb ^ (uint32_t)bk * 0x85EBCA77u);
            int sh = face + (int)((hk & 3) == 0) - (int)((hk & 3) == 1);
            if (inBand > band - 1.2f) sh += 1;                       // ledge top catches light
            else if (inBand < 1.f) sh -= 2;                          // shadow under the ledge above
            if (layered) {                                                                             // sandstone: alternating strata, rare cracks
                sh += (bi & 1) ? 0 : -1;
                if (((hb >> 5) & 7) == 0) sh -= 1;                                                     // a darker band now and then
                if (inBlock < 0.5f / bw && ((hk >> 3) & 7) == 0) sh -= 2;
                if ((var & 8) && d < (hgt * s) * 0.18f) sh -= 2;                                       // the dark caprock
            } else if (ruin) { if (inBlock < 1.f / bw && ((hk >> 3) & 3) != 0) sh -= 2; }              // walls: mortar joints
            else {                                                                                      // rock: columns and fissures
                if (inBlock < 1.f / bw) sh -= ((hk >> 3) & 1) ? 2 : 1;
                else if (inBlock > 1.f - 1.f / bw) sh += 1;                                             // each column's lit edge
            }
            uint32_t g = hash32((uint32_t)x * 0x27D4EB2Du ^ (uint32_t)y * 0x165667B1u ^ seed);
            if ((g & 7u) == 0) sh += ((g >> 3) & 1) ? 1 : -1;        // grain
            if (d < 1.f + s * 0.05f) sh = L.back ? 4 : 6;            // lit rim along the top
            if (up < s * 0.15f) sh -= 1;                             // grounded base
            if (x == splitX && !L.back) sh = litFace ? 6 : 1;        // the edge between the faces
            if (L.back && (dx == -hw || dx == hw)) sh = 7;
            uint8_t c = stoneI(clampi(sh, 0, 7), L.far);
            if (!L.far && ((hk >> 4) & 7) < (ruin ? 1u : 3u) && inBand > band * 0.55f && ((g >> 5) & 1)) c = leafI(2 + (int)((g >> 6) & 1), false);  // moss in cracks (rock is mossier)
            fb[y * W + x] = c;
        }
        if (!L.far && yt >= 1 && yt < H && ((hj >> 5) & 3) != 0) put(x, yt - 1, leafI(2 + (int)((hj >> 8) & 3), false));
    }
    if ((var & 64) && !L.far) {                                    // karst pillars wear a crown of green
        uint32_t h = seed;
        for (int k = 0; k < 4; k++) {
            h = hash32(h + 0x9E3779B9u);
            float ox = ((h & 255) / 127.5f - 1.f) * hw * 0.8f;
            blob(cx + ox, (float)(by - ht + (int)(ht * 0.05f)), fmaxf(1.f, hw * 0.6f), fmaxf(1.f, hw * 0.35f), L, h, s * 0.5f, leafI);
        }
    }
    if (var & 1) {                                                  // a waterfall spilling off the top
        int fw = clampi((int)(s * 0.3f), 1, 8);
        if (g_snow > 0.5f) {                                        // frozen: a still curtain of ice
            int topF = by - ht + (int)(0.1f * ht);
            for (int y = topF < 0 ? 0 : topF; y <= by && y < H; y++)
                for (int x = fx_frozen(cx, hw, seed) - fw; x <= fx_frozen(cx, hw, seed) + fw; x++) {
                    uint32_t g = hash32((uint32_t)x * 0x9E37u ^ (uint32_t)(y / 3) * 0x85EBu ^ seed);
                    put(x, y, (g & 3u) == 0 ? (uint8_t)(R_CLOUD + 15) : (uint8_t)(R_WATER + R_SKY + 11 + (g >> 8) % 4u));
                }
            return;
        }
        int fx = cx + (int)((((seed >> 4) & 255) / 255.f - 0.5f) * hw * 0.8f);
        int top = by - ht + (int)(0.1f * ht);
        int tt = (int)(g_t * 45.f);
        for (int y = top < 0 ? 0 : top; y <= by && y < H; y++)
            for (int x = fx - fw; x <= fx + fw; x++) {
                if ((unsigned)x >= (unsigned)W) continue;
                int v = (y - tt + (int)(hash32((uint32_t)(x - fx) * 0x9E37u ^ seed) & 15u)) & 15;   // streaks falling
                uint8_t c = (x == fx - fw || x == fx + fw) ? (uint8_t)(R_WATER + R_SKY + 6)        // darker edges
                          : v < 3 ? (uint8_t)(R_CLOUD + 15)                                       // white water
                          : v < 9 ? (uint8_t)(R_WATER + R_SKY + 12) : (uint8_t)(R_WATER + R_SKY + 9);
                fb[y * W + x] = c;
            }
        int pool = fw * 2 + (int)(hw * 0.3f);                        // the plunge pool
        for (int y = by - 1; y <= by + 1; y++) hspan(fx - pool, fx + pool, y, R_WATER + R_SKY + 9);
        if (sunVis > 0.5f && S.night < 0.1f && !L.far) {             // a little rainbow standing in the spray
            static const uint8_t RB[5] = {A_PINK_D, A_YEL, A_YEL_L, A_BLUE, A_LAV};
            for (int band = 0; band < 5; band++) {
                float r = fw * 3.f + band;
                for (int k = 0; k < 14; k++) {
                    float t = PI_F * (0.15f + 0.7f * k / 13.f);
                    int x = fx + (int)(cosf(t) * r), y = by - 1 - (int)(sinf(t) * r * 0.8f);
                    if (dith(x, y) < 0.45f) put(x, y, R_ACCENT + RB[band]);
                }
            }
        }
        uint32_t h = seed ^ (uint32_t)(g_t * 8.f);                   // foam and spray at the foot
        for (int k = 0; k < fw * 6 + 4; k++) {
            h = hash32(h + 0x9E3779B9u);
            float u = (h & 0xFFFF) / 32767.5f - 1.f, w = ((h >> 16) & 0xFF) / 255.f;
            put(fx + (int)(u * fw * 3.f), by - (int)(w * fw * 1.6f), (h >> 24) & 1 ? R_CLOUD + 15 : R_CLOUD + 12);
        }
    }
}

// ---------------- critters: one transient encounter at a time ----------------
static const char* DEER[] = {
    ".........d.d",
    "..........d.",
    ".........bb.",
    ".........bbk",
    ".w.....bbb..",
    ".bbbbbbbbb..",
    ".bbbbbbbbb..",
    ".lbbbbbbbl..",
    "..b.b...b.b.",
    "..b.b...b.b.",
    "..d.d...d.d."};
static const char* RABBIT[] = {
    "....g.g",
    "....ggg",
    ".ggggkg",
    "wgggggg",
    ".g..g.."};
// The near deer and rabbit: drawn 1:1 (2x only when very close), lit from above.
static const char* DEER_L[] = {
    "..................a..a..",
    "..................a.aa..",
    "...................aa.a.",
    ".................aaa....",
    "..................bll...",
    ".................bblll..",
    ".................bbbbbk.",
    "................dbbbl...",
    "...............dbbl.....",
    "..............dbbl......",
    ".w...........dbbl.......",
    "wwllllllllllbbbbl.......",
    ".dblllllllbbbbbbl.......",
    ".dbbbbbbbbbbbbbbl.......",
    ".dbbbbbbbbbbbbbd........",
    "..dddbbbbbbbbbdd........",
    "...dd.dd...dd.dd........",
    "...db.db...db.db........",
    "...db..b...db..b........",
    "...dd..d...dd..d........",
    "...d...d...d...d........",
    "...k...k...k...k........"};
static const char* RABBIT_L[] = {
    ".....gg...",
    ".....gh.g.",
    "......ggh.",
    "..hhhhgkg.",
    ".hgggggggg",
    "wggggggg..",
    ".gddgddg..",
    ".k...k...."};
static const char* BUTTER_OPEN[] = {"pp.pp", ".pkp.", "..k.."};
static const char* BUTTER_SHUT[] = {"..p..", ".pkp.", "..k.."};
static const char* DUCK[] = {
    ".....GG.",
    "....GGo.",
    ".wbbbbG.",
    "wbbbbbb.",
    ".dddddd."};
static const char* DUCK_FLY_UP[] = {"b.....b", ".bbbbGo", "..ddd.."};
static const char* DUCK_FLY_DN[] = {".......", "bbbbbGo", "b.ddd.b"};
static const char* HERON[] = {
    "..kk..",
    ".hhho.",
    "..h...",
    "..h...",
    ".hhh..",
    "hhhhh.",
    ".hhhh.",
    "..h...",
    "..h...",
    "..k..."};
static const char* HERON_FLY[] = {"hhh.....", ".hhhhhho", "....hh..", "...k.k.."};
static const char* FOX[] = {
    "......ww.w",
    "......wwkw",
    "wwwwwwwww.",
    ".wwwwwww..",
    "..w.w..w.w"};
static const char* OWL[] = {".ww.", "wkkw", "wwww", "wkww", ".ww."};
static const char* LIZARD[] = {".zzz.zz", "z.z.z.."};
static const char* ROADRUNNER[] = {
    "......kk",
    "...kbbbo",
    "kkkbbbb.",
    ".kbbbb..",
    "...b.b..",
    "..b...b."};
static const char* BEAVER[] = {
    "...nnnn..",
    "ddnnnnnnk",
    "dd.nnnnn.",
    "...n..n.."};
static const char* SQUIRREL[] = {
    "rr....",
    "r..rr.",
    "r.rrrk",
    "rrrrr.",
    ".r.r.."};
static const char* BIRD_UP[] = {"k...k", ".k.k.", "..k.."};
static const char* BIRD_DN[] = {".....", "kkkkk", "..k.."};

static uint8_t g_wingCol = A_YEL_L;
static uint8_t g_variant = 0;
static bool g_duckBody = false;
static uint8_t spriteColor(char ch) {
    switch (ch) {
        case 'd': return R_ACCENT + A_DEER_D;
        case 'b': if (g_variant == 7) return R_ACCENT + A_WHITE;
                  return g_duckBody ? (g_variant == 2 ? R_ACCENT + A_YEL : R_ACCENT + A_DEER_L) : ((g_variant == 1 || g_variant == 3) ? R_ACCENT + A_RAB : g_variant == 4 ? R_ACCENT + A_DEER_D : R_ACCENT + A_DEER);
        case 'l': if (g_variant == 7) return R_CLOUD + 15;
                  return g_variant == 1 ? R_ACCENT + A_RAB_L : g_variant == 2 ? R_ACCENT + A_WHITE : g_variant == 4 ? R_ACCENT + A_DEER : R_ACCENT + A_DEER_L;
        case 'w': return R_ACCENT + A_WHITE;
        case 'g': return g_variant == 1 ? R_ACCENT + A_WHITE : g_variant == 3 ? R_ACCENT + A_DEER_L : R_ACCENT + A_RAB;
        case 'a': return R_ACCENT + A_DEER_L;
        case 'p': return R_ACCENT + g_wingCol;
        case 'G': return g_variant == 2 ? R_ACCENT + A_YEL_L : R_LEAF + 1;
        case 'z': return R_ACCENT + A_DEER_L;
        case 'h': return g_variant == 2 ? R_ACCENT + A_WHITE : R_ACCENT + A_RAB_L;
        case 'o': return R_ACCENT + A_YEL;
        case 'n': return R_ACCENT + A_DEER_D;
        case 'r': return R_ACCENT + A_DEER;
        case 'k': return R_ACCENT + A_SIL;
        default:  return 0;
    }
}
// Keep one pixel grid: sprites draw at their native size (or exactly 2x), never at fractional scales.
static int g_bowRows = 0, g_bowDy = 0;    // a bow: the head and neck rows dip
static void drawSpriteI(const char* const* rows, int nr, float cx, float baseY, int k, bool flip) {
    int nc = (int)strlen(rows[0]);
    int x0 = (int)cx - nc * k / 2, y0 = (int)baseY - nr * k;
    for (int y = 0; y < nr * k; y++)
        for (int x = 0; x < nc * k; x++) {
            int sc = x / k;
            if (flip) sc = nc - 1 - sc;
            char ch = rows[y / k][sc];
            int dy = (y / k < g_bowRows) ? g_bowDy * k : 0;
            if (ch != '.') put(x0 + x, y0 + y + dy, spriteColor(ch));
        }
}
// Deer species are recolours of the same art: the coat and its light tone change; dark, white, eyes and antlers don't.
static inline uint8_t deerRemap(uint8_t v, uint8_t p) {
    if (p == R_ACCENT + A_DEER)   return (v == 1 || v == 3 || v == 5) ? R_ACCENT + A_RAB : v == 4 ? R_ACCENT + A_DEER_D : v == 7 ? R_ACCENT + A_WHITE : p;
    if (p == R_ACCENT + A_DEER_L) return (v == 1 || v == 5) ? R_ACCENT + A_RAB_L : v == 2 ? R_ACCENT + A_WHITE : v == 4 ? R_ACCENT + A_DEER : v == 7 ? R_CLOUD + 15 : p;
    return p;
}
static void blitSpr(const Spr* sp, float cx, float baseY, bool flip, uint8_t variant) {
    if (!sp || !sp->px) return;
    const int k = sp->k, w = sp->w, h = sp->h;
    int x0 = (int)cx - w * k / 2, y0 = (int)baseY - h * k;
    for (int y = 0; y < h * k; y++) {
        int sy = y / k;
        int dy = (sy < g_bowRows) ? g_bowDy * k : 0;                 // the sika's bow
        for (int x = 0; x < w * k; x++) {
            int sx = x / k;
            if (flip) sx = w - 1 - sx;
            uint8_t p = sp->px[sy * w + sx];
            if (p) put(x0 + x, y0 + y + dy, deerRemap(variant, p));
        }
    }
}
static uint8_t spriteColor(char ch);
static uint8_t antlerColour(char ch) { return ch == 'a' ? (uint8_t)(R_FAUNA + 4) : spriteColor(ch); }   // bone, whatever the coat
// A part (a tail) at a body's anchor, the same way round as the body.
static void blitPart(const Spr* part, const Spr* body, float cx, float baseY, bool flip, uint8_t variant) {
    if (!part || !part->px || !body) return;
    const int k = body->k, pk = part->k;
    int x0 = (int)cx - body->w * k / 2, y0 = (int)baseY - body->h * k;
    int axs = x0 + (flip ? (body->w - 1 - body->tx) : body->tx) * k, ays = y0 + body->ty * k;
    for (int y = 0; y < part->h; y++)
        for (int x = 0; x < part->w; x++) {
            uint8_t p = part->px[y * part->w + x];
            if (!p) continue;
            int ox = (x - part->ax) * pk, oy = (y - part->ay) * pk;
            for (int j = 0; j < pk; j++) for (int i = 0; i < pk; i++) put(axs + (flip ? -ox - i : ox + i), ays + oy + j, deerRemap(variant, p));
        }
}
// A deer as it is now: doe, buck, fawn or caribou cow; its pose from its mood; its tail down, flicking or flagged.
static void drawDeer(const FieldCritter& c, float cx, float baseY, float pxTall, bool flip, uint8_t sexOverride = 255) {
    uint8_t sex = sexOverride != 255 ? sexOverride : c.sex;
    if (sex == SEX_DOE && (c.variant == 1 || c.variant == 5)) sex = SEX_COW;        // caribou cows grow antlers too
    bool away = c.fleeing ? c.awayFlee : c.mood == DEER_SHOW;
    uint8_t anim = away ? AN_AWAY : c.fleeing ? AN_BOUND
                 : c.mood == DEER_GRAZE ? AN_GRAZE : c.mood == DEER_ALERT ? AN_ALERT : c.mood == DEER_STOMP ? AN_STOMP
                 : (c.mood == DEER_SNIFF || c.mood == DEER_PET) ? AN_SNIFF : c.mood == DEER_NURSE ? (sex == SEX_FAWN ? AN_NURSE : AN_STAND)
                 : AN_WALK;
    float fps = anim == AN_BOUND || anim == AN_AWAY ? 10.f : anim == AN_WALK ? 6.f : anim == AN_STOMP ? 4.f : 2.f;
    if (anim == AN_WALK && c.mood != DEER_WALK && c.mood != DEER_APPROACH) fps = 0.f;
    int frame = (int)((g_t - c.t0) * fps) + (int)(c.seed & 3);
    float tierPx = pxTall;
    if (sex == SEX_FAWN) pxTall *= 0.6f;
    uint8_t tier = deerTierFor(tierPx);
    const Spr* body = deerFrame(sex, tier, anim, frame);
    blitSpr(body, cx, baseY, flip, c.variant);
    // the tail: does and fawns flag in alarm, flick when at ease, and show it off turned away; bucks keep it down
    uint8_t tail = TAIL_DOWN;
    if (sex != SEX_BUCK) {
        float tt = g_t + (c.seed & 255) * 0.13f;
        if (c.fleeing || c.mood == DEER_STOMP) tail = TAIL_FLAG;
        else if (c.mood == DEER_SHOW) tail = sinf(tt * 2.4f) > 0.f ? TAIL_FLAG : TAIL_FLICK;
        else if (fmodf(tt, 3.7f) < 0.35f || c.mood == DEER_PET) tail = TAIL_FLICK;
    }
    blitPart(deerTail(sex, tier, anim == AN_AWAY, tail), body, cx, baseY, flip, c.variant);
}
static void drawCritterSprite(bool deer, float cx, float baseY, float pxTall, bool flip) {
    const char* const* sm = deer ? DEER : RABBIT;
    const char* const* lg = deer ? DEER_L : RABBIT_L;
    int nsm = deer ? 11 : 5, nlg = deer ? 22 : 8;
    if (pxTall < nsm * 1.4f) drawSpriteI(sm, nsm, cx, baseY, 1, flip);
    else if (pxTall < nlg * 1.6f) drawSpriteI(lg, nlg, cx, baseY, 1, flip);
    else drawSpriteI(lg, nlg, cx, baseY, 2, flip);
}
static void drawSprite(const char* const* rows, int nr, float cx, float baseY, float scale, bool flip) {
    int nc = (int)strlen(rows[0]);
    int dw = (int)(nc * scale + 0.5f), dh = (int)(nr * scale + 0.5f);
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    int x0 = (int)cx - dw / 2, y0 = (int)baseY - dh;
    for (int y = 0; y < dh; y++) {
        int sr = y * nr / dh;
        for (int x = 0; x < dw; x++) {
            int sc = x * nc / dw;
            if (flip) sc = nc - 1 - sc;
            char ch = rows[sr][sc];
            if (ch != '.') put(x0 + x, y0 + y, spriteColor(ch));
        }
    }
}

static inline int spriteK(float pxTall, int rows) { return pxTall >= rows * 1.6f ? 2 : 1; }
static void drawCritter(const FieldCritter& c) {
    float e = g_t - c.t0;
    float fe = c.fleeing ? c.fleeT : 0.f;
    float cxw = c.x - g_camX, cyw = c.y - g_camY;
    float rng = sqrtf(cxw * cxw + cyw * cyw);
    if (rng < 0.5f) return;
    float rel = wrapPi(atan2f(cyw, cxw) - g_yaw);
    if (fabsf(rel) > FOV * 0.5f + 0.1f) return;
    float sx = floorf(W * 0.5f + rel * PXR + 0.5f), by = HOR + ZK / rng, s = PXR / rng;
    bool fleeing = c.fleeing;
    bool faceRight = fleeing && c.kind <= CR_BIRDS ? c.dir > 0 : c.faceRight;
    g_variant = c.variant;
    int mod = c.kind != CR_DEER ? creatureModFor(c.kind, c.variant, c.seed) : -1;   // art from the card, if any
    if (mod >= 0 && !creatureModFlock(mod)) {
        const Spr* m = creatureModSprite(mod, fleeing, s, (int)(e * 8.f) + (int)(c.seed & 3));
        if (m) { blitSpr(m, sx, by - c.h * s, !faceRight, 255); g_variant = 0; return; }
    }
    const Spr* flockBird = mod >= 0 ? creatureModSprite(mod, fleeing, s, (int)(e * 10.f)) : nullptr;   // one bird, for the whole flock
    if (c.kind == CR_DUCK && (c.variant == 1 || (c.variant == 2 && (c.seed & 1)))) {   // a heron in the marsh; a white crane in the garden
        if (c.h > 0.05f) drawSpriteI(HERON_FLY, 4, sx, by - c.h * s, spriteK(0.35f * s, 4), !faceRight);
        else drawSpriteI(HERON, 10, sx, by, spriteK(1.f * s, 10), !faceRight);
        g_variant = 0;
        return;
    }
    if (c.kind == CR_BUTTERFLY && c.variant == 2) {             // a kingfisher: a blue-and-orange dart low over the water
        float y = by - c.h * 0.5f * s;
        put((int)sx, (int)y, R_ACCENT + A_BLUE); put((int)sx + 1, (int)y, R_ACCENT + A_BLUE); put((int)sx + 2, (int)y, R_ACCENT + A_BLUE);
        put((int)sx + 1, (int)y + 1, R_ACCENT + A_YEL);
        g_variant = 0;
        return;
    }
    if (c.kind == CR_BUTTERFLY && c.variant == 3) {             // a blue morpho: big wings flashing electric blue
        float y = by - c.h * s;
        bool open = ((int)(g_t * 6.f + (c.seed & 7))) & 1;
        put((int)sx, (int)y, R_ACCENT + A_SIL);
        if (open) { put((int)sx - 1, (int)y, R_ACCENT + A_BLUE); put((int)sx + 1, (int)y, R_ACCENT + A_BLUE); put((int)sx - 2, (int)y - 1, R_ACCENT + A_BLUE); put((int)sx + 2, (int)y - 1, R_ACCENT + A_BLUE); }
        else { put((int)sx, (int)y - 1, R_ACCENT + A_BLUE_D); }
        g_variant = 0;
        return;
    }
    if (c.variant == 5 && c.kind == CR_RABBIT) {                // an arctic fox
        float hop = fleeing ? fabsf(sinf(fe * 12.f)) * 0.2f * s : 0.f;
        drawSpriteI(FOX, 5, sx, by - hop, spriteK(0.45f * s, 5), !faceRight);
        g_variant = 0;
        return;
    }
    if (c.variant == 5 && c.kind == CR_BIRDS) {                 // a snowy owl on a rock; off on slow wings
        float lift = fleeing ? fe * fe * 1.2f * s : 0.f;
        if (fleeing) { bool up = ((int)(g_t * 4.f)) & 1; uint8_t w = R_ACCENT + A_WHITE; int x = (int)sx, y = (int)(by - 0.5f * s - lift);
            put(x, y, w); put(x - 1, y - (up ? 1 : 0), w); put(x + 1, y - (up ? 1 : 0), w); put(x - 2, y - (up ? 2 : 0), w); put(x + 2, y - (up ? 2 : 0), w); }
        else drawSpriteI(OWL, 5, sx, by - 0.3f * s, spriteK(0.35f * s, 5), false);
        g_variant = 0;
        return;
    }
    if (c.variant == 7 && c.kind == CR_DEER) {                  // the white stag
        float fadeA = fleeing ? clampf(1.f - c.fleeT / 6.f, 0.f, 1.f) : 1.f;
        g_alpha = fadeA;
        g_variant = 7;
        drawDeer(c, sx, by, 1.7f * s, !faceRight, SEX_BUCK);           // the stag
        for (int k = 0; k < 18; k++) {                                // a faint shimmer around it
            uint32_t gg = hash32((uint32_t)k * 0x9E37u + (uint32_t)(g_t * 4.f));
            int gx = (int)sx + (int)(((gg & 255) / 255.f - 0.5f) * 1.8f * s), gy = (int)by - (int)(((gg >> 8) & 255) / 255.f * 1.7f * s);
            if (dith(gx, gy) < 0.3f * fadeA) put(gx, gy, R_ACCENT + A_YEL_L);
        }
        g_alpha = 1.f;
        g_variant = 0;
        return;
    }
    if (c.variant == 5 && c.kind == CR_DEER) {                  // a caribou herd moving through
        for (int k = 0; k < 4; k++) {
            float ox = (k - 1.5f) * 1.6f * s + sinf(k * 2.3f) * 0.4f * s;
            float hop = fleeing ? fabsf(sinf(fe * 9.f + k)) * 0.35f * s : 0.f;
            g_variant = 1;
            drawDeer(c, sx + ox, by - hop - (k & 1), 1.5f * s * (0.85f + 0.05f * k), !faceRight, SEX_BUCK);   // caribou: cows carry antlers too
        }
        g_variant = 0;
        return;
    }
    if (c.variant == 6 && c.kind == CR_BIRDS) {                 // parrots: a bright flock bursting up
        static const uint8_t PC[4] = {A_PINK_D, A_BLUE, A_YEL, A_PINK_D};
        for (int b = 0; b < 4; b++) {
            float ox = (b - 1.5f) * 0.6f * s;
            float lift = fleeing ? fe * fe * (1.f + b * 0.3f) * s : 0.3f * s;
            bool flap = ((int)(g_t * 9.f) + b) & 1;
            int x = (int)(sx + ox), y = (int)(by - lift - 0.6f * s);
            if (flockBird) { blitSpr(creatureModSprite(mod, fleeing, s, (int)(e * 10.f) + b), (float)x, (float)y + 1, b & 1, 255); continue; }
            uint8_t c1 = R_ACCENT + PC[b];
            put(x, y, c1); put(x + 1, y, c1); put(x - 1, y - (flap ? 1 : 0), c1); put(x + 2, y - (flap ? 1 : 0), c1);
            if (!fleeing && flap) put(x, y + 1, R_ACCENT + A_YEL);
        }
        g_variant = 0;
        return;
    }
    if (c.kind == CR_BUTTERFLY && c.variant) {                  // a dragonfly: a blue glint darting low
        float y = by - c.h * 0.6f * s;
        put((int)sx, (int)y, R_ACCENT + A_BLUE); put((int)sx + 1, (int)y, R_ACCENT + A_BLUE);
        if (((int)(g_t * 20.f)) & 1) { put((int)sx, (int)y - 1, R_ACCENT + A_WHITE); put((int)sx + 1, (int)y + 1, R_ACCENT + A_WHITE); }
        g_variant = 0;
        return;
    }
    switch (c.kind) {
        case CR_DEER: {
            float hop = fleeing ? fabsf(sinf(fe * 9.f)) * 0.45f * s : 0.f;
            // in the garden, stand quietly before a sika deer and it bows to you
            if (c.variant == 2 && !fleeing && g_scene.stillFor > 3.f && rng < 12.f && fabsf(rel) < 0.35f) {
                float ph = fmodf(g_scene.stillFor - 3.f, 6.f);
                float b = ph < 1.5f ? ph / 1.5f : ph < 3.f ? 1.f : ph < 4.5f ? 1.f - (ph - 3.f) / 1.5f : 0.f;
                g_bowRows = 11; g_bowDy = (int)(b * 4.f + 0.5f);
            }
            drawDeer(c, sx, by - hop, (c.variant == 4 ? 2.f : 1.5f) * s, !faceRight);   // an elk stands taller
            g_bowRows = 0; g_bowDy = 0;
        } break;
        case CR_LIZARD:
            if (rng < 14.f) drawSpriteI(LIZARD, 2, sx, by, spriteK(0.12f * s, 2), !c.faceRight);
            break;
        case CR_KOI: {
            if (rng > 18.f) break;
            int fx = (int)sx, fy = (int)by;
            int dir = c.faceRight ? 1 : -1;
            put(fx, fy, R_ACCENT + A_YEL); put(fx + dir, fy, R_ACCENT + ((c.seed & 1) ? A_WHITE : A_YEL));
            put(fx - dir, fy, R_ACCENT + A_YEL);
            if (s > 20.f) { put(fx - 2 * dir, fy, R_ACCENT + A_PINK_D); put(fx + 2 * dir, fy, R_ACCENT + A_YEL); }
        } break;
        case CR_RABBIT: {
            float hop = fleeing ? fabsf(sinf(fe * 14.f)) * 0.25f * s : 0.f;
            drawCritterSprite(false, sx, by - hop, 0.45f * s, !faceRight);
        } break;
        case CR_BIRDS:
            if (c.variant == 3) {                                    // the canyon: a roadrunner, sprinting off
                bool stride = fleeing && (((int)(g_t * 14.f)) & 1);
                drawSpriteI(ROADRUNNER, 6, sx + (stride ? 1 : 0), by, spriteK(0.4f * s, 6), !faceRight);
                break;
            }
            for (int b = 0; b < 4; b++) {
                float ox = (b - 1.5f) * 0.5f * s + sinf(b * 2.1f) * 0.2f * s;
                float lift = fleeing ? fe * fe * (1.2f + b * 0.3f) * s : 0.f;
                bool flap = fleeing ? (((int)(g_t * 10.f) + b) & 1) : false;
                float bob = fleeing ? 0.f : (sinf(g_t * 6.f + b) > 0.6f ? 0.05f * s : 0.f);
                if (flockBird) blitSpr(creatureModSprite(mod, fleeing, s, (int)(e * 10.f) + b), sx + ox, by - lift - bob, b & 1, 255);
                else if (fleeing) drawSpriteI(flap ? BIRD_UP : BIRD_DN, 3, sx + ox, by - lift - bob, 1, false);
                else if (!flockBird) put((int)(sx + ox), (int)(by - 1 + bob), R_ACCENT + A_BIRD);
            }
            break;
        case CR_BUTTERFLY: {
            static const uint8_t WING[3] = {A_YEL_L, A_PINK_L, A_LAV};
            g_wingCol = WING[c.seed % 3u];
            bool open = ((int)(g_t * 9.f + (c.seed & 7))) & 1;
            float y = by - c.h * s;
            if (s < 18.f) { put((int)sx, (int)y, R_ACCENT + g_wingCol); if (open) put((int)sx + 1, (int)y, R_ACCENT + g_wingCol); }
            else drawSpriteI(open ? BUTTER_OPEN : BUTTER_SHUT, 3, sx, y, 1, false);
        } break;
        case CR_DUCK: {
            g_duckBody = true;
            if (c.h > 0.05f) {
                bool flap = ((int)(g_t * 8.f)) & 1;
                drawSpriteI(flap ? DUCK_FLY_UP : DUCK_FLY_DN, 3, sx, by - c.h * s, spriteK(0.3f * s, 3), !faceRight);
            } else {
                float bob = sinf(g_t * 2.f + (c.seed & 15)) > 0.f ? 1.f : 0.f;
                drawSpriteI(DUCK, 5, sx, by + 1 - bob, spriteK(0.35f * s, 5), !faceRight);
                hspan((int)sx - 3, (int)sx + 3, (int)by + 1, R_WATER + R_SKY + 13);      // a little wake
            }
            g_duckBody = false;
        } break;
        case CR_BEAVER:
            if (c.splashT > 0.f) {                                   // the plunge: a ring opening on the water
                float k = g_t - c.splashT;
                int r = 1 + (int)(k * 10.f);
                for (int i = 0; i < 12; i++) {
                    float a = i * 0.5236f;
                    put((int)(sx + cosf(a) * r * 1.6f), (int)(by + sinf(a) * r * 0.5f), R_WATER + R_SKY + 14);
                }
                if (k < 0.25f) for (int i = -2; i <= 2; i++) put((int)sx + i, (int)by - 1 - (2 - abs(i)), R_CLOUD + 15);
            } else drawSpriteI(BEAVER, 4, sx, by, spriteK(0.4f * s, 4), !faceRight);
            break;
        case CR_SQUIRREL: {
            float up = c.h * s;
            drawSpriteI(SQUIRREL, 5, sx, by - up, spriteK(0.25f * s, 5), !faceRight);
        } break;
    }
    g_variant = 0;
    (void)e;
}

// Near field, painted far to near. Insertion sort: the order barely changes frame to frame.
static uint16_t s_order[FIELD_N];
static float    s_range[FIELD_N];
static bool     s_orderInit = false;

static void drawObjects() {
    const FieldObj* objs = fieldObjects();
    const FieldCritter* crits = fieldCritters();
    if (!s_orderInit) { for (int i = 0; i < FIELD_N; i++) s_order[i] = (uint16_t)i; s_orderInit = true; }
    for (int i = 0; i < FIELD_N; i++) { float ex = objs[i].x - g_camX, ey = objs[i].y - g_camY; s_range[i] = ex * ex + ey * ey; }
    for (int i = 1; i < FIELD_N; i++) {
        uint16_t k = s_order[i];
        float v = s_range[k];
        int j = i - 1;
        while (j >= 0 && s_range[s_order[j]] < v) { s_order[j + 1] = s_order[j]; j--; }
        s_order[j + 1] = k;
    }
    float critR2[CRIT_N];
    bool critDone[CRIT_N];
    for (int i = 0; i < CRIT_N; i++) {
        critDone[i] = !crits[i].on;
        float ex = crits[i].x - g_camX, ey = crits[i].y - g_camY;
        critR2[i] = ex * ex + ey * ey;
    }

    for (int n = 0; n < FIELD_N; n++) {
        const FieldObj& o = objs[s_order[n]];
        float r2 = s_range[s_order[n]];
        for (int i = 0; i < CRIT_N; i++) if (!critDone[i] && critR2[i] >= r2) { g_alpha = 1.f; drawCritter(crits[i]); critDone[i] = true; }
        if (o.type == OBJ_NONE || o.wet) continue;
        float rng = sqrtf(r2);
        if (rng < 0.35f) continue;
        float app = atan2f(o.y - g_camY, o.x - g_camX);
        float rel = wrapPi(app - g_yaw);
        // angular half-size of the widest part, for culling
        float halfWorld = o.type == OBJ_BROAD ? o.b * 1.3f : o.type == OBJ_OUTCROP ? o.a : o.type == OBJ_PINE ? (o.var == 6 ? o.radius + 3.f : o.a * 0.22f) : o.type == OBJ_LOG ? o.a * 0.5f : o.type == OBJ_TUNNELTREE ? o.radius + 3.f : o.type == OBJ_HOLLOWLOG ? o.a * 0.5f : o.type == OBJ_BIGLEAF ? o.a : o.type == OBJ_TORII ? o.a * 0.6f : 1.f;
        float halfAng = atanf(halfWorld / rng);
        if (fabsf(rel) - halfAng > FOV * 0.5f + 0.02f) continue;
        if (!o.solid && rng < 0.9f) continue;

        float sx = floorf(W * 0.5f + rel * PXR + 0.5f), by = floorf(HOR + ZK / rng + 0.5f), s = PXR / rng;
        if (s < 40.f) s = exp2f(floorf(log2f(s) * 12.f + 0.5f) / 12.f);    // mid/far: size in 6% steps
        float sunRel = wrapPi(SUN_AZ - app);
        g_objBearing = app;
        Lighting Lg;
        Lg.side = sinf(sunRel);
        Lg.back = cosf(sunRel) * lowSun * sunVis > 0.45f;
        Lg.far = rng > 11.f * (1.f - 0.6f * g_scene.fog);
        float gustNow = g_scene.gust * (0.55f + 0.45f * sinf(g_t * (1.1f + (o.seed & 7) * 0.07f) + ((o.seed >> 8) & 255) * 0.025f));
        float hgt = o.type == OBJ_PINE || o.type == OBJ_BROAD ? o.a : 1.f;
        Lg.sway = gustNow * 0.06f * hgt * s * tanhf(g_windLat * 3.f);   // trees lean downwind (a west wind)
        bool giantObj = (o.type == OBJ_PINE && o.var == 6) || o.type == OBJ_TUNNELTREE;
        g_alpha = giantObj ? 1.f : clampf((FIELD_R - rng) / 5.f, 0.f, 1.f);   // re-dreamt things fade in at the edge (giants just loom out of the haze)
        if (g_alpha <= 0.05f) continue;

        switch (o.type) {
            case OBJ_PINE:    if (o.var == 6) { drawRedwood(sx, by, s, o.a, o.radius, Lg, o.seed); break; }
                              g_altFoliage = o.var == 5; drawPine(sx, by, s, o.a, Lg, o.seed); g_altFoliage = false; break;
            case OBJ_BROAD:   g_blossom = o.var == 3; drawBroad(sx, by, s, o.a, o.b, Lg, o.seed); g_blossom = false;
                              if (o.var == 7) { drawVines(sx, by, s, o.a, o.b, o.seed, Lg.sway); }
                              break;
            case OBJ_BIGLEAF: if (rng < 16.f) drawBigLeaf(sx, by, s, o.a, o.seed, gustNow * 2.f); break;
            case OBJ_BUSH:    if (rng < 22.f) drawBush(sx, by, s, o.a, Lg, o.seed); break;
            case OBJ_FLOWERS: if (rng < 10.f) drawFlowers(sx, by, s, o.seed); break;
            case OBJ_TUFT:    if (rng < (g_fern > 0.5f ? 12.f : 5.f)) drawTuft(sx, by, s, o.seed, gustNow * 2.f * tanhf(g_windLat * 3.f)); break;
            case OBJ_LOG:     drawLog(sx, by, s, o.a, o.b, Lg, o.seed); break;
            case OBJ_TUNNELTREE: drawTunnelTree(sx, by, s, o.a, o.radius, Lg, o.seed); break;
            case OBJ_HOLLOWLOG:  drawHollowLog(sx, by, s, o.a, Lg, o.seed); break;
            case OBJ_REEDS:   if (rng < 18.f) drawReeds(sx, by, s, o.a, o.seed, gustNow * 3.f); break;
            case OBJ_LANTERN: drawLantern(sx, by, s, o.a, Lg); break;
            case OBJ_BAMBOO:  drawBamboo(sx, by, s, o.a, o.b, o.seed, Lg.sway); break;
            case OBJ_TORII:   drawTorii(sx, by, s, o.a, o.b, Lg); break;
            case OBJ_SAGUARO: drawSaguaro(sx, by, s, o.a, o.b, Lg, o.seed); break;
            case OBJ_PEAR:    if (rng < 22.f) drawPear(sx, by, s, o.a, o.seed); break;
            case OBJ_BOULDER: drawBoulder(sx, by, s, o.a, Lg, o.seed); break;
            case OBJ_OUTCROP: drawOutcrop(sx, by, s, o.a, o.b, Lg, o.seed, o.var); break;
            default: break;
        }
    }
    g_alpha = 1.f;
    for (int i = 0; i < CRIT_N; i++) if (!critDone[i]) drawCritter(crits[i]);
}

// ---------------- entry ----------------

#ifdef HOST
// ---- desktop-only (mockups and tests): draw props at chosen spots into the current frame ----
void renderDebugBoulder(float sx, float baseY, float s, float sz, uint32_t seed) {
    Lighting L{}; L.side = 1.f; L.far = false; L.back = false;
    drawBoulder(sx, baseY, s, sz, L, seed);
}
void renderDebugCritter(uint8_t kind, float dist, float rel, bool faceRight, uint8_t variant, uint32_t seed, uint8_t sex, uint8_t mood, bool fleeing, bool away) {
    FieldCritter c{}; c.on = true; c.kind = kind; c.variant = variant; c.faceRight = faceRight; c.sex = sex; c.mood = mood; c.mom = -1; c.fleeing = fleeing; c.awayFlee = away; c.fleeT = 0.3f;
    c.x = g_camX + dist * cosf(g_yaw + rel); c.y = g_camY + dist * sinf(g_yaw + rel);
    c.t0 = g_t - 3.f; c.idleT = 0.f; c.seed = seed;
    drawCritter(c);
}
#endif
// A planet mod's way in: the address of one colour (3 floats, 0..255). Groups: accent[16], creature[16],
// aurora[7], base:<rock|rock2|snow|hills|grass|leaf|bark|water|stone>, sky[<key>].<zenith|horizon|glow|
// cloud_dark|cloud_lit|sun|ambient|key|fog>. nullptr if there's no such colour.
static Key* s_keysRW = nullptr;
int renderSkyKeys() { return NKEYS; }
template <class T> static T* writable(const T*& ptr, const T* def, size_t bytes) {   // a planet mod's own copy, made on first change
    if (ptr == def) { T* c = (T*)memSlow(bytes); if (!c) return nullptr; memcpy(c, def, bytes); ptr = c; }
    return const_cast<T*>(ptr);
}
float* renderColourSlot(const char* group, int i, const char* field) {
    if (!strcmp(group, "accent")) { C3* a = i >= 0 && i < 16 ? writable(g_accent, ACCENT_DEF, sizeof ACCENT_DEF) : nullptr; return a ? &a[i].r : nullptr; }
    if (!strcmp(group, "creature")) { C3* a = i >= 0 && i < 16 ? writable(g_fauna, FAUNA_DEF, sizeof FAUNA_DEF) : nullptr; return a ? &a[i].r : nullptr; }
    if (!strcmp(group, "aurora")) { C3* a = i >= 0 && i < 7 ? writable(g_aurora, AURORA_DEF, sizeof AURORA_DEF) : nullptr; return a ? &a[i].r : nullptr; }
    if (!strcmp(group, "base")) {
        static const char* const N[] = {"rock", "rock2", "snow", "hills", "grass", "leaf", "bark", "water", "stone"};
        int k = 0;
        while (k < 9 && strcmp(field, N[k])) k++;
        if (k == 9) return nullptr;
        BaseCols* b = writable(g_base, &BASE_DEF, sizeof BASE_DEF);
        if (!b) return nullptr;
        C3* const V[] = {&b->rock, &b->rock2, &b->snow, &b->hill, &b->grass, &b->leaf, &b->bark, &b->water, &b->stone};
        return &V[k]->r;
    }
    if (!strcmp(group, "sky")) {
        if (i < 0 || i >= NKEYS) return nullptr;
        if (!s_keysRW) {                                              // the first change: our own copy of the keys
            s_keysRW = (Key*)memSlow(sizeof(KEYS));
            if (!s_keysRW) return nullptr;
            memcpy(s_keysRW, KEYS, sizeof(KEYS));
            g_keys = s_keysRW;
        }
        Key& k = s_keysRW[i];
        static const char* const F[] = {"zenith", "horizon", "glow", "cloud_dark", "cloud_lit", "sun", "ambient", "key", "fog"};
        C3* const V[] = {&k.zen, &k.hor, &k.glow, &k.cDark, &k.cLit, &k.sun, &k.amb, &k.key, &k.fog};
        for (int f = 0; f < 9; f++) if (!strcmp(field, F[f])) return &V[f]->r;
        return nullptr;
    }
    return nullptr;
}
float renderSkyKeyTime(int i) { return i >= 0 && i < NKEYS ? g_keys[i].t : 0.f; }

void renderInit() {
    buildGrassTex();
    creaturesBuiltIn(spriteColor, antlerColour);
    if (!s_rayT) s_rayT = (uint8_t*)allocScratch(RAY_NB * RAY_NR);
    for (int r = 0; r <= 16; r++)
        for (int dy = 0; dy <= r; dy++) {
            float v = (r + 0.3f) * (r + 0.3f) - dy * dy;
            HW[r][dy] = v > 0.f ? (uint8_t)sqrtf(v) : 0;
        }
    memset(fb, 0, sizeof(fb));
}

float sunElevation(float tod) { return sampleKey(tod).elev; }

static void drawFishing();
static void drawBugs();
static void drawAlignGrid();
static void drawFlashlight();
void renderFrame(float yaw, float distance, float speed, float timeSec) {
    const Scene& sc = g_scene;
    SUN_AZ = sc.sunAz;
    MOON_AZ = sc.sunAz + PI_F;                                        // the moon rides opposite the sun
    g_windLat = -sinf(yaw);                                           // wind toward the east: right when facing north
    g_pan = (int)lroundf(yaw * PXR);
    g_yaw = g_pan / PXR;                                            // the view scrolls in whole pixels
    g_dist = distance; g_t = timeSec;
    g_camX = CAM_L * cosf(yaw); g_camY = CAM_L * sinf(yaw);
    S = sampleKey(g_scene.tod);
    weatherKey(sc);

    float cover = 1.f - 0.85f * clampf((sc.cloud - 0.6f) / 0.35f, 0.f, 1.f);
    sunVis = clampf((S.elev + 0.12f) / 0.1f, 0.f, 1.f) * cover * (1.f - sc.rain * 0.8f);
    lowSun = clampf(1.f - S.elev / 0.45f, 0.f, 1.f) * sunVis;
    sunX = W * 0.5f + wrapPi(SUN_AZ - yaw) * PXR;
    sunY = HOR - 5.f - sunPathElevation(sc.tod) * PXR;              // its true height: up out of view through the middle of the day
    s_rainbowSwap = sc.rainbow > 0.02f && S.night < 0.3f && fabsf(wrapPi(SUN_AZ - yaw)) > FOV * 0.5f + 0.35f;
    biomeRamps();
    s_flashWarm = s_flashOn && (S.elev < 0.03f || g_scene.cloud > 0.8f || g_scene.storm > 0.4f) && !s_rainbowSwap;
    buildPalette();

    computeColumns();
    drawSky();
    drawNight();
    drawAurora();
    drawMeteors();
    drawSun();
    drawClouds();
    drawRidge(hFar, HOR - 1, R_MTN_FAR, 24.f, 0x6A1u);
    drawRidge(hMid, HOR, R_MTN_NEAR, 17.f, 0x6B2u);
    drawHills(hHF, HOR, 8);
    drawHills(hHN, HOR + 1, 0);
    drawMurmuration();
    drawCanyonLife();
    drawCoastLife();
    drawFloatingLanterns();
    drawMist(HOR - 14 - (int)(sc.fog * 18.f), HOR);
    drawRainbow();
    drawGround();
    shoreline();
    castShadows();
    drawMist(HOR, HOR + 10 + (int)(sc.fog * 14.f));
    drawObjects();
    cleanAndVeil();
    drawCrepuscular();
    drawFireflies();
    drawBolt();
    drawRain();
    drawBugs();
    drawFlashlight();                      // the hand-light, before the rod (the rod is behind it)
    drawFishing();
    drawAlignGrid();                       // calibration overlay, above everything                         // the rod is in your hands: nearest of all

    float walk = clampf(speed * 2.5f, 0.f, 1.f);
    s_bob = (int)(fabsf(sinf(distance * 2.2f)) * 2.f * walk + 0.5f);
}

void renderMuteIcon(bool muted, float alpha) {
    if (alpha <= 0.02f) return;
    static const char* ON[9] = {
        "....k.......", "...kw...w...", "kkkww....w..", "kwwww..w..w.", "kwwww..w..w.",
        "kwwww..w..w.", "kkkww....w..", "...kw...w...", "....k......."};
    static const char* OFF[9] = {
        "....k.......", "...kw.......", "kkkww.w...w.", "kwwww..w.w..", "kwwww...w...",
        "kwwww..w.w..", "kkkww.w...w.", "...kw.......", "....k......."};
    const char* const* ic = muted ? OFF : ON;
    const int x0 = W - 16, y0 = 4;
    for (int y = 0; y < 9; y++)
        for (int x = 0; x < 12; x++) {
            char c = ic[y][x];
            if (c == '.' || dith(x0 + x, y0 + y) >= alpha) continue;
            fb[(y0 + y) * W + x0 + x] = c == 'k' ? R_ACCENT + A_SIL : R_ACCENT + A_YEL_L;
        }
}

static const uint8_t DIG3x5[10][5] = {{7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},{7,4,7,1,7},{7,4,7,5,7},{7,1,1,2,2},{7,5,7,5,7},{7,5,7,1,7}};
static void drawNumber(int x0, int y0, int v, uint8_t col) {
    char buf[6]; int n = 0;
    if (v < 0) v = 0;
    do { buf[n++] = (char)(v % 10); v /= 10; } while (v && n < 5);
    for (int d = 0; d < n; d++) {
        int digit = buf[n - 1 - d];
        for (int y = 0; y < 5; y++) for (int x = 0; x < 3; x++) if (DIG3x5[digit][y] & (4 >> x)) put(x0 + d * 4 + x, y0 + y, col);
    }
}

// ---------------- fishing: the rod in your hands, the line, the bobber, the fight ----------------
static void bezierLine(float x0, float y0, float cx, float cy, float x1, float y1, uint8_t c, float thick0, float thick1, float density) {
    float len = fabsf(x1 - x0) + fabsf(y1 - y0) + fabsf(cx - x0) + fabsf(cy - y0);
    int n = (int)fmaxf(8.f, len);
    for (int i = 0; i <= n; i++) {
        float t = (float)i / n, u = 1.f - t;
        float x = u * u * x0 + 2.f * u * t * cx + t * t * x1, y = u * u * y0 + 2.f * u * t * cy + t * t * y1;
        int th = (int)(thick0 + (thick1 - thick0) * t);
        for (int k = 0; k <= th; k++) if (density >= 1.f || dith((int)x + k, (int)y) < density) put((int)x + k, (int)y, c);
    }
}

static void drawCatch(const FishView& F) __attribute__((unused));
static void drawCatch(const FishView& F) {
    float L = clampf(F.lengthCm * 1.1f, 34.f, 118.f), Hh = L * 0.26f;
    float cx = W * 0.5f, cy = H * 0.52f + sinf(F.t * 3.f) * 1.5f;
    static const uint8_t BODY[4][3] = {{A_RAB_L, A_RAB, A_DEER_D}, {A_YEL, A_DEER_L, A_DEER}, {A_BLUE, A_LAV, A_BLUE_D}, {A_PINK_L, A_LAV, A_PINK_D}};
    const uint8_t* bc = BODY[F.tier];
    bool shimmer = F.tier == TIER_LEGEND;
    for (int y = (int)(-Hh); y <= (int)Hh; y++)
        for (int x = (int)(-L * 0.5f); x <= (int)(L * 0.5f); x++) {
            float u = x / (L * 0.5f), v = y / Hh;
            float prof = sqrtf(fmaxf(0.f, 1.f - u * u)) * (u > 0.f ? 1.f - 0.25f * u : 1.f);
            if (fabsf(v) > prof) continue;
            uint8_t c = R_ACCENT + (v < -0.25f ? bc[2] : v > 0.35f ? (uint8_t)A_WHITE : bc[0]);
            uint32_t g = hash32((uint32_t)(x + 300) * 0x9E37u ^ (uint32_t)(y + 300) * 0x85EBu ^ F.fishSeed);
            if ((g & 15u) == 0 && fabsf(v) < 0.5f) c = R_ACCENT + bc[1];                     // spots
            if (shimmer && ((x + y + (int)(g_t * 20.f)) % 9) == 0) c = R_ACCENT + A_YEL_L;  // iridescence
            put((int)cx + x, (int)cy + y, c);
        }
    for (int y = (int)(-Hh * 1.1f); y <= (int)(Hh * 1.1f); y++) {        // the tail
        int w = (int)(fabsf((float)y) / (Hh * 1.1f) * L * 0.16f) + 1;
        hspan((int)(cx - L * 0.5f) - w, (int)(cx - L * 0.5f), (int)cy + y, R_ACCENT + bc[2]);
    }
    for (int k = 0; k < (int)(L * 0.25f); k++) put((int)(cx - L * 0.1f) + k, (int)(cy - Hh) - (k < L * 0.12f ? k / 3 : (int)(L * 0.25f) / 3 - (k - (int)(L * 0.12f)) / 3), R_ACCENT + bc[2]);   // dorsal fin
    put((int)(cx + L * 0.36f), (int)(cy - Hh * 0.25f), R_ACCENT + A_SIL);              // eye
    for (int k = 0; k < 8; k++) {                                         // drips
        float ph = fmodf(F.t * 1.3f + k * 0.37f, 1.f);
        put((int)(cx - L * 0.4f + k * L * 0.1f), (int)(cy + Hh + ph * 30.f), R_WATER + R_SKY + 13);
    }
    int ty = (int)(cy + Hh + 8);                                          // length in cm and rarity stars
    hspan((int)cx - 22, (int)cx + 22, ty - 2, R_ACCENT + A_SIL);
    for (int y = ty - 1; y < ty + 8; y++) hspan((int)cx - 22, (int)cx + 22, y, R_ACCENT + A_SIL);
    drawNumber((int)cx - 18, ty + 1, (int)(F.lengthCm + 0.5f), R_ACCENT + A_WHITE);
    put((int)cx - 4, ty + 3, R_ACCENT + A_WHITE); put((int)cx - 3, ty + 3, R_ACCENT + A_WHITE); put((int)cx - 4, ty + 5, R_ACCENT + A_WHITE); put((int)cx - 3, ty + 5, R_ACCENT + A_WHITE); put((int)cx - 4, ty + 4, R_ACCENT + A_WHITE);   // c
    for (int st = 0; st <= F.tier; st++) {                                // stars
        int sx0 = (int)cx + 4 + st * 5, sy0 = ty + 3;
        put(sx0, sy0, R_ACCENT + A_YEL_L); put(sx0 - 1, sy0, R_ACCENT + A_YEL); put(sx0 + 1, sy0, R_ACCENT + A_YEL); put(sx0, sy0 - 1, R_ACCENT + A_YEL); put(sx0, sy0 + 1, R_ACCENT + A_YEL);
    }
}

static void drawBugIcon(uint8_t k, int x, int y, float flap) {
    static const uint8_t COL[BAIT_KINDS] = {A_PINK, A_DEER_D, A_YEL_L, A_SIL, A_LAV, A_YEL_L, A_BLUE, A_YEL, A_PINK_D, A_WHITE, A_YEL_L, A_WHITE, A_SIL, A_LAV};
    uint8_t c = R_ACCENT + COL[k];
    bool open = flap > 0.f;
    switch (k) {
        case BAIT_WORM: case BAIT_SLUG: case BAIT_ICEWORM: case BAIT_CATERPILLAR:   // a little wriggle
            put(x - 1, y, c); put(x, y - (open ? 1 : 0), c); put(x + 1, y, c); if (k == BAIT_SLUG) put(x + 2, y - 1, c); break;
        case BAIT_MAYFLY: case BAIT_FIREFLY: case BAIT_SILKMOTH: case BAIT_MOONMOTH:   // wings
            put(x, y, c); if (open) { put(x - 1, y - 1, c); put(x + 1, y - 1, c); } else { put(x - 1, y, c); put(x + 1, y, c); }
            if (k == BAIT_FIREFLY && sinf(g_t * 5.f + x) > 0.f) { put(x, y + 1, R_ACCENT + A_YEL_L); }
            break;
        case BAIT_CRAB: put(x - 1, y, c); put(x, y, c); put(x + 1, y, c); put(x - 2, y - (open ? 1 : 0), c); put(x + 2, y - (open ? 1 : 0), c); break;
        default: put(x, y, c); put(x + 1, y, c); put(x, y - 1, c); break;           // crickets, hoppers, beetles
    }
}

static void drawBugs() {
    const Bug* b = bugs();
    for (int i = 0; i < BUG_N; i++) {
        if (!b[i].on) { bugsNoteScreen(i, -100, -100); continue; }
        float cxw = b[i].x - g_camX, cyw = b[i].y - g_camY;
        float rng = sqrtf(cxw * cxw + cyw * cyw);
        float rel = wrapPi(atan2f(cyw, cxw) - g_yaw);
        if (rng < 0.5f || fabsf(rel) > FOV * 0.5f) { bugsNoteScreen(i, -100, -100); continue; }
        float s = PXR / rng;
        int x = (int)(W * 0.5f + rel * PXR), y = (int)(HOR + ZK / rng - b[i].h * s);
        bugsNoteScreen(i, x * 2, y * 2);
        if (b[i].caught) {                                              // a little burst as it goes in the jar
            float k = (g_t - b[i].caughtT) / 0.6f;
            for (int j = 0; j < 8; j++) { float a = j * 0.785f; put(x + (int)(cosf(a) * k * 6.f), y + (int)(sinf(a) * k * 4.f), R_ACCENT + A_YEL_L); }
            continue;
        }
        drawBugIcon(b[i].kind, x, y, sinf(g_t * 14.f + i));
        if (fmodf(g_t + i * 0.77f, 2.6f) < 0.12f) { put(x, y - 2, R_ACCENT + A_WHITE); put(x - 1, y - 2, R_ACCENT + A_YEL_L); put(x + 1, y - 2, R_ACCENT + A_YEL_L); }   // a glint to catch your eye
    }
}

static void drawFishing() {
    const FishView& F = fishingView();
    if (F.state == FS_OFF && F.rodOut <= 0.f) return;
    if (F.rodOut > 0.9f) {                                              // the bait on the hook, bottom-left
        int sel = baitSelected();
        for (int y = H - 10; y < H - 2; y++) hspan(2, 22, y, R_ACCENT + A_SIL);
        if (sel >= 0) { drawBugIcon((uint8_t)sel, 7, H - 6, 1.f); drawNumber(12, H - 8, baitCount((uint8_t)sel), R_ACCENT + A_WHITE); }
        else { put(6, H - 6, R_CLOUD + 10); put(7, H - 5, R_CLOUD + 10); put(8, H - 6, R_CLOUD + 10); }   // a bare hook
    }
    bool fight = F.state == FS_FIGHT;
    float ang = fight ? F.fishAng : F.castYaw;
    float z = fight ? fmaxf(F.shoreZ, F.lineOut) : F.bobZ;           // the line always cuts the water, never the bank
    float wx = W * 0.5f + wrapPi(ang - g_yaw) * PXR, wy = HOR + ZK / z;
    float out = F.rodOut * F.rodOut * (3.f - 2.f * F.rodOut);
    float bend = fight ? fminf(1.f, F.tension) : (F.biting ? 0.35f : 0.f);
    float baseX = W - 6.f, baseY = H + 6.f;
    float tipX = W * 0.6f + (wx - W * 0.6f) * 0.22f * bend, tipY = 28.f + 26.f * bend;
    if (F.state == FS_CAST && F.t < 0.3f) { tipX += 18.f * (1.f - F.t / 0.3f); tipY -= 10.f * (1.f - F.t / 0.3f); }   // the cast swing
    tipX += (1.f - out) * 40.f; tipY += (1.f - out) * 120.f;
    bool lineOut = out > 0.85f && F.state != FS_STOW && F.state != FS_LANDED;
    // the water end: the bobber, or where the line cuts in during the fight
    if (lineOut) {
        float ex = wx, ey = wy;
        if (F.state == FS_CAST) {                                         // the bobber flying out
            float p = clampf(F.t / 0.8f, 0.f, 1.f);
            ex = tipX + (wx - tipX) * p; ey = tipY + (wy - tipY) * p - sinf(p * PI_F) * 30.f;
        }
        float sag = F.state == FS_LOST ? 40.f : fight ? (1.f - clampf(F.tension * 1.3f, 0.f, 1.f)) * 22.f : 10.f;
        bezierLine(tipX, tipY, (tipX + ex) * 0.5f, (tipY + ey) * 0.5f + sag, ex, ey, R_CLOUD + 13, 0.f, 0.f, F.state == FS_LOST ? 0.5f : 0.85f);
        if (F.state == FS_WAIT) {
            float bob = sinf(g_t * 2.2f) * 0.6f + F.nibble * 1.5f;
            if (F.dip < 0.5f) { put((int)ex, (int)(ey - 1 + bob), R_ACCENT + A_PINK_D); put((int)ex, (int)(ey + bob), R_ACCENT + A_WHITE); }
            float rp = fmodf(g_t * (F.biting ? 2.5f : 0.6f), 1.f);       // rings on the water (fast and many when it bites)
            int rr = 1 + (int)(rp * (F.biting ? 9.f : 5.f));
            for (int k = 0; k < 10; k++) { float a = k * 0.628f; int x = (int)(ex + cosf(a) * rr * 1.6f), y = (int)(ey + 1 + sinf(a) * rr * 0.45f); if (dith(x, y) < 1.f - rp) put(x, y, R_WATER + R_SKY + 14); }
        }
        if (fight) {                                                      // the wake where the line cuts the water
            for (int k = 0; k < 6; k++) {
                float ph = fmodf(g_t * 3.f + k * 0.17f, 1.f);
                int dx = (int)(ph * 6.f) * ((k & 1) ? 1 : -1);
                put((int)ex + dx, (int)ey + (int)(ph * 2.f), R_CLOUD + 14);
            }
            if (F.jump > 0.f) {                                           // the fish in the air
                float p = 1.f - F.jump / 0.9f;
                int jy = (int)(ey - sinf(p * PI_F) * 14.f);
                int len = 3 + (int)(F.tier * 1.5f);
                for (int k = -len; k <= len; k++) put((int)ex + k, jy + (int)(k * k * 0.08f) * (p < 0.5f ? 1 : -1), R_ACCENT + (F.tier >= TIER_RARE ? (int)A_BLUE : (int)A_RAB_L));
                if (p > 0.8f) for (int k = 0; k < 10; k++) put((int)ex + (k - 5), (int)ey - (k & 3), R_CLOUD + 15);
            }
        }
    }
    // the rod: cork handle, reel, a blank that bends with the fight
    float mx = (baseX + tipX) * 0.5f - 6.f - 10.f * bend, my = (baseY + tipY) * 0.5f - 4.f + 18.f * bend;
    bezierLine(baseX, baseY, mx, my, tipX, tipY, R_ACCENT + A_SIL, 3.f, 0.f, 1.f);
    bezierLine(baseX, baseY, baseX + (mx - baseX) * 0.25f, baseY + (my - baseY) * 0.25f, baseX + (tipX - baseX) * 0.2f, baseY + (tipY - baseY) * 0.2f, R_ACCENT + A_DEER_L, 3.f, 3.f, 1.f);
    for (int k = 1; k < 5; k++) { float t = 0.3f + k * 0.15f, u = 1.f - t; put((int)(u * u * baseX + 2 * u * t * mx + t * t * tipX), (int)(u * u * baseY + 2 * u * t * my + t * t * tipY) - 1, R_CLOUD + 12); }   // guides
    int rx = (int)(baseX - 16.f + (1.f - out) * 30.f), ry = (int)(baseY - 22.f + (1.f - out) * 100.f);
    for (int dy = -4; dy <= 4; dy++) for (int dx = -4; dx <= 4; dx++) if (dx * dx + dy * dy <= 16) put(rx + dx, ry + dy, dx * dx + dy * dy <= 4 ? R_CLOUD + 6 : R_CLOUD + 9);
    float spin = g_t * (fight ? 4.f + 14.f * F.reel : 0.f);
    put(rx + (int)lroundf(cosf(spin) * 3.f), ry + (int)lroundf(sinf(spin) * 3.f), R_CLOUD + 15);    // the handle going round
}

float renderFov() { return FOV; }

void renderSetAlign(bool on, float sum) { s_alignOn = on; s_alignSum = sum; }
void renderSetFlashlight(bool on) { s_flashOn = on; }
bool renderFlashlight() { return s_flashOn; }

// A warm hand-light projected from the camera: a pool on the ground a few metres ahead, a softer cone above
// it, falling off with distance. Inside it every surface keeps its own brightness and texture but is re-lit
// in warm light (greens stay greenish, everything else goes amber); rain and snow sparkle as they cross it,
// and in fog or a storm you see the beam itself. In daylight it does nothing you'd notice.
static void drawFlashlight() {
    if (!s_flashOn || !s_flashWarm) return;
    const uint8_t* pal = framePaletteRGB();
    static uint8_t lum8[256]; static uint8_t green[256];
    for (int i = 0; i < 256; i++) {
        const uint8_t* c = pal + i * 3;
        lum8[i] = (uint8_t)((c[0] * 77 + c[1] * 150 + c[2] * 29) >> 8);
        green[i] = c[1] > c[0] + 8 && c[1] >= c[2];
    }
    const Scene& sc = g_scene;
    const float haze = clampf(sc.fog + sc.rain * 0.8f + sc.storm * 0.5f, 0.f, 1.f);
    const float cx = W * 0.5f, cy = HOR + 10.f, rx = 46.f, ry = 34.f;   // one oval of light where you're looking
    for (int y = (int)(cy - ry * 1.5f); y < H; y++) {
        if (y < 0) continue;
        float att = 1.f;
        if (y > HOR) { float z = ZK / (y - HOR + 0.5f); att = clampf(1.5f - z / 12.f, 0.35f, 1.f); }   // the ground: nearer is brighter
        else att = 0.42f;                                                 // above the horizon it's mostly far away
        float vy = (y - cy) / ry;
        for (int x = 0; x < W; x++) {
            float vx = (x - cx) / rx;
            float d = vx * vx + vy * vy;
            float core = d < 1.f ? (1.f - d) * (1.f - d) : 0.f;
            float halo = d < 2.f ? (1.f - d / 2.f) * (1.f - d / 2.f) * 0.3f : 0.f;   // a soft spill around it
            float Lt = fmaxf(core * 1.3f, halo) * att;
            if (Lt < 0.06f) continue;
            uint8_t& p = fb[y * W + x];
            if (p >= R_SUN && p < R_SUN + 16) continue;                 // the moon and stars stay as they are
            bool sky = p < R_CLOUD, cloudy = p >= R_CLOUD && p < R_SUN;
            if (sky || (cloudy && y < HOR && p < R_CLOUD + 8)) {        // air: the beam itself shows in fog and rain
                if (haze > 0.05f && dith(x, y) < Lt * 0.3f * haze) p = R_WATER + R_SUN + 8 + 3;
                continue;
            }
            float l = lum8[p] / 255.f;
            float nl = clampf(l * (1.f + 2.2f * Lt) + 0.3f * Lt + (cloudy ? 0.25f * Lt : 0.f), 0.f, 0.999f);   // rain and snow catch it brightly
            uint8_t warm = (uint8_t)(R_WATER + R_SUN + (green[p] ? 0 : 8) + (int)(nl * 8.f));
            if (Lt >= 0.32f || dith(x, y) < (Lt - 0.06f) / 0.26f) p = warm;   // a soft rim
        }
    }
}

// Two ground grids in flat diagnostic red and green: radial lines every 15 degrees (straight up the
// screen in this projection) and rings at set distances. Green is fixed to the world; red stays where
// the world was when the drag began. Dashed, flat, unlike anything in the art.
static void drawAlignGrid() {
    if (!s_alignOn) return;
    const uint8_t RED = R_ACCENT + A_LAV, GREEN = R_ACCENT + A_BLUE;   // these two slots are recoloured while aligning
    static const float RINGS[] = {2.f, 3.f, 4.5f, 7.f, 11.f, 18.f, 30.f};
    for (int pass = 0; pass < 2; pass++) {                         // red first, green over it
        uint8_t c = pass == 0 ? RED : GREEN;
        float off = pass == 0 ? s_alignSum : 0.f;
        for (int k = 0; k < 24; k++) {
            float a = k * 0.2617994f;
            float rel = wrapPi(a + off - g_yaw);
            if (fabsf(rel) > FOV * 0.5f + 0.02f) continue;
            int x = (int)lroundf(W * 0.5f + rel * PXR);
            for (int y = HOR + 2; y < H; y++) if (((y + pass) & 1) == 0) put(x, y, c);
        }
        for (float z : RINGS) {
            int y = (int)lroundf(HOR + ZK / z);
            if (y <= HOR || y >= H) continue;
            for (int x = 0; x < W; x++) {
                int wx = x + (pass == 0 ? (int)lroundf(-s_alignSum * PXR) : 0) + g_pan;   // dashes travel with their grid
                if (((wx >> 1) & 1) == 0) put(x, y + pass, c);
            }
        }
    }
    float deg = fabsf(s_alignSum) * 57.29578f;                      // how far you've slid it, in degrees
    int n = (int)(deg + 0.5f);
    int w = n >= 100 ? 12 : n >= 10 ? 8 : 4;
    int x0 = W / 2 - (w + 4) / 2;
    for (int y = 1; y < 9; y++) hspan(x0 - 2, x0 + w + 5, y, R_ACCENT + A_SIL);
    hspan(x0, x0 + 2, 4, s_alignSum >= 0.f ? GREEN : RED);           // a sign bar: - in red, + in green
    if (s_alignSum >= 0.f) for (int y = 3; y <= 5; y++) put(x0 + 1, y, GREEN);
    drawNumber(x0 + 4, 2, n, GREEN);
    put(x0 + w + 4, 2, GREEN);                                       // the degree mark
}

void renderFps(int fps) {
    static const uint8_t DIG[10][5] = {{7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},{7,4,7,1,7},{7,4,7,5,7},{7,1,1,2,2},{7,5,7,5,7},{7,5,7,1,7}};
    char buf[4]; int n = 0;
    if (fps > 99) fps = 99;
    if (fps < 0) fps = 0;
    if (fps >= 10) buf[n++] = (char)(fps / 10);
    buf[n++] = (char)(fps % 10);
    int x0 = 3, y0 = 3;
    for (int y = -1; y <= 5; y++) hspan(x0 - 1, x0 + n * 4 - 1, y0 + y, R_ACCENT + A_SIL);
    for (int d = 0; d < n; d++)
        for (int y = 0; y < 5; y++)
            for (int x = 0; x < 3; x++)
                if (DIG[(int)buf[d]][y] & (4 >> x)) put(x0 + d * 4 + x, y0 + y, R_ACCENT + A_YEL_L);
}

const uint8_t*  frameBuffer()     { return fb; }
const uint16_t* framePalette565() { return pal565; }
const uint8_t*  framePaletteRGB() { return palRGB; }
int             frameBob()        { return s_bob; }

}
