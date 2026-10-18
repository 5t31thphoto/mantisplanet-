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
static constexpr float SUN_AZ = 0.0f, MOON_AZ = 2.3f, PLANET_AZ = 4.0f;

// ---------------- palette layout: 16 ramps x 16, dark -> light ----------------
enum : uint8_t {
    R_SKY = 0, R_CLOUD = 16, R_SUN = 32, R_MTN_FAR = 48, R_MTN_NEAR = 64, R_HILLS = 80,
    R_GROUND = 96, R_LEAF = 112, R_BARK = 128, R_ACCENT = 144, R_WATER = 160
};
static constexpr int N_REFLECT = 96;   // ramps 0..5 have darkened water twins at R_WATER + i
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
static inline float dith(int x, int y) { return (BAYER[((y & 3) << 2) | (x & 3)] + 0.5f) * (1.f / 16.f); }
static inline float ign(int x, int y) {     // interleaved gradient noise: organic, no lattice
    float f = 0.06711056f * x + 0.00583715f * y;
    f = 52.9829189f * (f - floorf(f));
    return f - floorf(f);
}
// Pixel-art banding: clean bands with a thin checkered seam, instead of dithering whole gradients.
static inline int band(float f, int x, int y) {
    int i = (int)f;
    float fr = f - i;
    if (fr > 0.86f) return i + 1;
    if (fr > 0.62f && ((x ^ y) & 1)) return i + 1;
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

static Key sampleKey(float tod) {
    int k = 0;
    while (k < NKEYS - 2 && tod >= KEYS[k + 1].t) k++;
    const Key &A = KEYS[k], &B = KEYS[k + 1];
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

static const C3 ROCK = {112, 98, 128}, ROCK2 = {88, 80, 102}, SNOW = {240, 240, 252};
static const C3 HILL = {30, 66, 46}, GRASS = {78, 130, 50}, LEAF = {40, 102, 44}, BARK = {84, 56, 36};
static const C3 WATER = {18, 40, 74}, STONE = {120, 118, 122};
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
static C3 rampAt(const Ramp4& r, float s) {
    s = clampf(s, 0.f, 1.f) * 3.f;
    int i = (int)s;
    if (i >= 3) return r.k[3];
    return mix(r.k[i], r.k[i + 1], s - i);
}
static const C3 ACCENT[16] = {
    {14, 10, 22}, {70, 40, 22}, {130, 78, 40}, {195, 145, 95}, {120, 110, 100}, {190, 180, 165},
    {170, 40, 100}, {240, 90, 160}, {255, 175, 215}, {50, 60, 170}, {95, 135, 250}, {175, 135, 245},
    {240, 170, 40}, {255, 232, 120}, {240, 240, 235}, {30, 28, 40}};

static bool s_rainbowSwap = false;
static float s_occ[256];           // how much of the sun's light each palette entry blocks per ray step
static float s_fogAmt[256];        // how readily each entry steps into its hazier twin

static C3 spectrum(float t) {      // 0 violet (inner) .. 1 red (outer)
    static const C3 K[7] = {{150, 90, 230}, {80, 120, 255}, {70, 200, 235}, {90, 225, 110}, {250, 235, 90}, {255, 160, 60}, {240, 70, 60}};
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
    auto grassAt = [&](float s) -> C3 { return mix(rampAt(GRASS_R, s), rampAt(WETG_R, s), sc.wet * 0.7f); };
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
        C3 mf = mix(rampAt(ROCK_R, t), SNOW, clampf((t - 0.74f) / 0.18f, 0.f, 1.f));
        P[R_MTN_FAR + i] = fogc(add(scale(mul(mf, S.amb), 0.55f + 0.5f * t), scale(mul(mf, S.key), t * t * 0.45f)), 0.5f);
        C3 mn = mix(rampAt(ROCK2_R, t), SNOW, clampf((t - 0.84f) / 0.14f, 0.f, 1.f));
        P[R_MTN_NEAR + i] = fogc(add(scale(mul(mn, S.amb), 0.55f + 0.5f * t), scale(mul(mn, S.key), t * t * 0.45f)), 0.28f);
        // half-ramps: near 0..7, fogged 8..15
        float s = (i & 7) / 7.f;
        bool far = i >= 8;
        P[R_HILLS + i] = fogc(litR(HILL_R, s), far ? 0.44f : 0.16f);
        C3 lf = scale(litR(LEAF_R, s), far ? 1.f : shade);
        if ((i & 7) == 7) lf = add(lf, scale(S.key, 0.08f));               // warm rim
        P[R_LEAF + i] = fogc(lf, far ? 0.36f : 0.03f);
        if (!far) {
            C3 bk = scale(litR(BARK_R, s), shade);
            if (i == 7) bk = add(bk, scale(S.key, 0.1f));
            P[R_BARK + i] = fogc(bk, 0.04f);
        } else {
            C3 st = scale(litR(STONE_R, s), shade);
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
    P[R_ACCENT + A_YEL_L] = C3{215, 255, 120};                     // firefly light is its own
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
        else if (i >= R_ACCENT && i < R_WATER) oc = 0.3f;
        s_occ[i] = oc;
        float f = 0.f;
        if (i >= R_MTN_FAR && i < R_HILLS) f = 0.6f;
        else if (i >= R_HILLS && i < R_HILLS + 8) f = 0.45f;
        else if (i >= R_HILLS + 8 && i < R_GROUND) f = 0.6f;
        else if (i >= R_GROUND && i < R_LEAF) f = 0.15f + 0.15f * ((i - R_GROUND) >> 2);
        else if (i >= R_LEAF && i < R_LEAF + 8) f = 0.1f;
        else if (i >= R_LEAF + 8 && i < R_BARK) f = 0.55f;
        else if (i >= R_BARK && i < R_ACCENT) f = 0.12f;
        else if (i >= R_WATER) f = 0.4f;
        s_fogAmt[i] = f * sc.fog;
    }
    if (sc.flash > 0.01f)
        for (int i = 0; i < 256; i++) P[i] = mix(P[i], C3{225, 230, 255}, sc.flash * ((i < R_GROUND || i >= R_WATER) ? 0.6f : 0.3f));
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

        float m2 = stretch(fbmP(angU(a, 7), D * 0.0025f, 7, 2, 0xB0u), 0.3f, 0.7f);
        float pk2 = fbmP(angU(a, 22), D * 0.003f, 22, 3, 0xB1u);
        pk2 = 1.f - fabsf(2.f * pk2 - 1.f);
        hMid[i] = -3.f + 10.f * m2 + 14.f * m2 * pk2 * pk2;

        hHF[i] = 1.f + 8.f * stretch(fbmP(angU(a, 11), D * 0.006f, 11, 3, 0xC0u), 0.3f, 0.7f)
                 + spikes(a, 300, 4.f, D * 0.006f, 0xC1u);
        hHN[i] = -1.f + 6.f * stretch(fbmP(angU(a, 17), D * 0.012f, 17, 3, 0xD0u), 0.35f, 0.7f)
                 + spikes(a, 180, 6.f, D * 0.012f, 0xD1u);
        if (i >= 1 && i <= W) {
            int x = i - 1;
            colAng[x] = a;
            sideCol[x] = sinf(wrapPi(SUN_AZ - a));      // + : sun to the right of this column
            mistN[x] = fbmP(angU(a, 30), g_t * 0.08f + D * 0.02f, 30, 2, 0xE0u);
        }
    }
    for (int y = HOR; y < H; y++)
        xoffRow[y] = (int)lroundf(sinf(y * 0.9f + g_t * 1.7f + g_dist * 0.3f) * (0.3f + (y - HOR) * 0.045f));
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
            uint32_t thr = (uint32_t)(n * (5.f + band * 24.f) * (1.f - g_scene.cloud * 0.8f));
            if ((h & 1023u) < thr) {
                int b = 8 + (int)((h >> 10) % 8);
                if ((hash32(h ^ tw) & 7u) == 0) b -= 5;
                p = R_SUN + clampi(b, 0, 15);
            }
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
}

static void drawClouds() {
    constexpr int CR = (HOR - 6) / 2, CW = W / 2 + 1;
    const Scene& sc = g_scene;
    const float cov = sc.cloud;
    const float thresh = 0.92f - 0.62f * cov + 0.12f * S.night * (1.f - sc.cloud) + 0.25f * sc.aurora * S.night;
    const float drift = g_t * 0.006f * (1.f + 2.f * sc.wind);
    const float centre = CR * (0.42f + 0.2f * sc.storm);
    static float* dens = nullptr;                                  // CW x (CR + 2), on the heap
    static float prox[CW];
    if (!dens) dens = (float*)allocScratch(sizeof(float) * CW * (CR + 2));
    if (!dens) return;
    auto DN = [&](int cx, int cyp1) -> float& { return dens[cx * (CR + 2) + cyp1]; };
    for (int cx = 0; cx < CW; cx++) {
        float a = g_yaw + (cx * 2 - W * 0.5f) / PXR + drift;
        float u = angU(a, 36);
        for (int cy = -1; cy <= CR; cy++) {
            float n = fbmP(u, cy * 0.15f + g_dist * 0.004f + g_t * 0.01f, 36, 4, 0xC1u);
            float prof = clampf(1.f - fabsf(cy - centre) / (CR * 0.6f), 0.f, 1.f);
            DN(cx, cy + 1) = n * 0.8f + prof * 0.35f - thresh;
        }
        float rel = wrapPi(SUN_AZ - a);
        prox[cx] = sunVis / (1.f + rel * rel * 6.f);
    }
    auto D = [&](float fx, float fy) -> float {           // bilinear density at cell coords
        fx = clampf(fx, 0.f, CW - 1.001f); fy = clampf(fy, -1.f, CR - 0.001f);
        int ix = (int)fx, iy = (int)floorf(fy);
        int ix1 = ix + 1 < CW ? ix + 1 : ix;
        float tx = fx - ix, ty = fy - iy;
        float a0 = DN(ix, iy + 1) + (DN(ix1, iy + 1) - DN(ix, iy + 1)) * tx;
        float a1 = DN(ix, iy + 2) + (DN(ix1, iy + 2) - DN(ix, iy + 2)) * tx;
        return a0 + (a1 - a0) * ty;
    };
    for (int y = 0; y < CR * 2; y++) {
        float fy = y * 0.5f - 0.25f;
        for (int x = 0; x < W; x++) {
            float fx = x * 0.5f;
            float d = D(fx, fy);
            if (d <= 0.f) continue;
            float alpha = clampf(d * 11.f, 0.f, 1.f);
            if (alpha < 0.45f || (alpha < 0.7f && ((x ^ y) & 1))) continue;        // crisp 1 px edge, thin seam
            float dn = D(fx, fy + 1.f), up = D(fx, fy - 1.f);
            float bottom = clampf((d - dn) * 6.f, 0.f, 1.f), top = clampf((d - up) * 6.f, 0.f, 1.f);
            float light = 0.28f + bottom * 0.62f * lowSun + top * 0.45f * (1.f - lowSun) + prox[x >> 1] * 0.3f + (1.f - alpha) * 0.18f;
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
        float u = angU(colAng[x], 640);
        int y0 = clampi((int)ceilf(top), 0, H);
        float snowDepth = h > snowLine ? (h - snowLine) * 0.55f : -1.f;
        for (int y = y0; y < baseY && y < H; y++) {
            float d = y - top, depth = d / h;
            // erosion gullies: diagonal streaks anchored to the panorama, lit on the sun side
            float g = vnoiseP(u + d * 0.55f * (sideCol[x] >= 0.f ? 1.f : -1.f), d * 0.22f, 640, salt);
            // spur: shade from a column displaced by depth so the lit/shadow split runs down diagonally
            int xs = clampi(x + (int)((g - 0.5f) * d * 1.4f), 0, W - 1);
            float fs = -(hh[xs + 2] - hh[xs]) * 0.5f * sideCol[x];
            float ls = 0.5f + 0.3f * fs / (1.f + fabsf(fs));
            float s = (lit + ls) * 0.5f + (g - 0.5f) * 0.55f * (0.4f + 0.6f * (1.f - depth)) - depth * 0.3f;
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
            uint32_t hs = hash32((uint32_t)(x * 2654435761u) ^ (uint32_t)(y * 40503) ^ (uint32_t)half);
            float s = 2.4f + face * 2.2f - d * 0.18f + ((int)(hs & 3) - 1.5f) * 0.35f;
            if (d < 1.2f && face > 0.35f) s += 1.2f * lowSun + 0.5f;
            fb[y * W + x] = R_HILLS + half + clampi((int)(s + dith(x, y)), 0, 7);
        }
    }
}

// Index-space fog: step a pixel to its more-fogged palette twin. No new colours, no dots.
static inline uint8_t fogStep(uint8_t p) {
    if (p >= R_WATER) {
        uint8_t q = p - R_WATER;
        uint8_t f = fogStep(q);
        return f < N_REFLECT ? (uint8_t)(R_WATER + f) : p;
    }
    if (p >= R_HILLS && p < R_HILLS + 8) return p + 8;                                   // near hills -> far hills
    if (p >= R_HILLS + 8 && p < R_GROUND) return R_MTN_NEAR + 1 + (p & 7);                // far hills -> mountain haze
    if (p >= R_MTN_NEAR && p < R_HILLS) return R_MTN_FAR + (p & 15);                     // near range -> far range
    if (p >= R_MTN_FAR && p < R_MTN_NEAR) return R_SKY + 7 + ((p & 15) >> 3);            // far range -> sky
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
            float r = ign(x + (int)(g_t * 7.f), y);
            if (r < amt) p = fogStep(p);
            if (r < amt - 0.6f) p = fogStep(p);
        }
    }
}

// ---------------- ground + lake ----------------
static void drawGround() {
    const uint32_t seed = worldSeed();
    const float daylight = sunVis * clampf((S.elev + 0.05f) / 0.12f, 0.f, 1.f) * (1.f - S.night);
    const uint32_t tglit = (uint32_t)(g_t * 6.f);
    const int tstreak = (int)(g_t * 2.f);
    for (int by = HOR; by < H; by += 2)
        for (int bx = 0; bx < W; bx += 2) {
            float zc = ZK / (by - HOR + 1.f);
            if (zc > 70.f) zc = 70.f;
            bool lake = lakeField(colAng[bx] + 0.5f / PXR, g_dist + zc) > LAKE_T;
            // dappled light: crisp-edged pools of sun under the canopy, cloud shadows in the open
            int lightAt = 2;
            if (!lake && daylight > 0.1f) {
                float canopy = clampf((g_scene.forestNear - 0.3f) * 2.5f, 0.f, 1.f);
                if (canopy > 0.f) {
                    float n = fbmP(angU(colAng[bx], 160), (g_dist + zc) * 0.55f, 160, 2, 0xD4A1u);
                    float thr = 0.4f + 0.2f * canopy + 0.25f * (1.f - daylight);
                    lightAt = n > thr + 0.08f ? 3 : n > thr - 0.04f ? 2 : n > thr - 0.16f ? 1 : 0;
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
                        int sx = clampi(px + xoffRow[py], 0, W - 1);
                        uint8_t src = fb[sy * W + sx];
                        idx = src < N_REFLECT ? (uint8_t)(R_WATER + src) : (uint8_t)(R_WATER + R_HILLS);
                        if (((py + tstreak) % 5) == 0 && src < N_REFLECT) idx = (uint8_t)((idx + 2) > (idx | 15) ? (idx | 15) : idx + 2);
                        // the sun's path on the water: broken horizontal glints, densest at its centre
                        float wcol = 2.f + (py - HOR) * (0.18f + 0.3f * lowSun);
                        float gl = sunVis * (1.f - g_scene.rain) * (0.35f + 0.65f * lowSun);
                        float cf = 1.f - fabsf(px - sunX) / wcol;
                        if (gl > 0.05f && cf > 0.f && !s_rainbowSwap && ((py + tstreak) % 3) != 0) {
                            uint32_t g = hash32((uint32_t)((px + py * 5) >> 2) * 131u ^ (uint32_t)py * 977u ^ tglit);
                            if ((g & 255u) < (uint32_t)(gl * 200.f * cf * cf)) idx = R_SUN + 9 + (uint8_t)(cf * 5.f + ((g >> 9) & 1u));
                        }
                    } else {
                        float z = ZK / (py - HOR + 0.5f);
                        float f = 1.f - (py - HOR) / (float)(H - HOR);
                        int bnd = (int)(f * f * 3.99f);
                        // grass in clusters: blades ~2 px wide, 2-3 rows tall, anchored to the world
                        int gx = ((int)floorf(angU(colAng[px], 520)) % 520 + 520) % 520;
                        static const float BLADE_K[4] = {3.6f, 1.2f, 0.45f, 0.15f};  // ~3 rows per blade in every band
                        int gz = (int)floorf((g_dist + z) * BLADE_K[bnd]);
                        uint32_t h = hash32((uint32_t)gx * 0x9E3779B1u ^ (uint32_t)gz * 0x85EBCA77u ^ seed);
                        int sh = lightAt;                                   // dappled light for this block
                        if ((h & 3u) == 0 && sh < 3) sh++;                  // blades catching the light
                        else if ((h & 15u) == 1 && sh > 0) sh--;            // the odd blade in its own shade
                        idx = R_GROUND + bnd * 4 + sh;
                        if (z < 9.f && (h & 255u) < 3u) {
                            static const uint8_t fl[4] = {A_PINK_L, A_BLUE, A_YEL, A_LAV};
                            idx = R_ACCENT + fl[(h >> 8) & 3u];
                        }
                    }
                    fb[py * W + px] = idx;
                }
        }
}

// ---------------- pixel care: waterline, cast shadows, stray pixels ----------------
static inline bool isWater(uint8_t p) { return p >= R_WATER; }
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
    for (int i = 0; i < FIELD_N; i++) {
        const FieldObj& o = objs[i];
        if (!o.solid) continue;
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
                    if (isGround(p)) p = groundShade(p, 0);
                }
        }
    }
}

// A pixel that differs from all four neighbours, which agree, is noise not detail: fold it in.
// Terrain, ground and plants only; stars, glints, rain, motes and fireflies are deliberate.
static void cleanOrphans() {
    for (int y = 1; y < H - 1; y++) {
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
                * (1.f + sc.fog * 1.3f) * clampf(1.45f - fabsf(rel), 0.f, 1.f) * (1.f - sc.intro);
    if (str < 0.04f || !s_rayT) return;
#ifdef HOST
    if (getenv("NORAYS")) return;
    if (getenv("RAYDBG")) fprintf(stderr, "rays: str %.2f sun (%.0f,%.0f) elev %.2f\n", str, sunX, sunY, S.elev);
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
    const uint32_t drift = (uint32_t)(g_t * 5.f), drift2 = (uint32_t)(g_t * 2.f);
    for (int y = 0; y < H; y++) {
        float dy = y - sunY;
        for (int x = 0; x < W; x++) {
            float dx = x - sunX;
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
            uint8_t& p = fb[y * W + x];
            // dust motes drifting in the light
            if (B > 0.32f && !s_rainbowSwap && y > HOR - 24 && (hash32((uint32_t)(x + drift) * 0x9E3779B1u ^ (uint32_t)(y - drift2) * 0x85EBCA77u) & 1023u) < (uint32_t)(B * 7.f)) {
                p = R_SUN + 13;
                continue;
            }
            int b = (int)(B * 5.5f + dith(x, y) - 0.5f);
            if (b <= 0) continue;
            if (p < R_SUN || (p >= R_MTN_FAR && p < R_HILLS)) { int lim = p | 15; p = (uint8_t)(p + b > lim ? lim : p + b); }
            else if (p >= R_WATER) {
                int q = p - R_WATER;
                if (q >= R_SUN && q < R_SUN + 16) continue;
                int lim = (q >= R_HILLS && q < R_GROUND) ? (p | 7) : (p | 15);
                p = (uint8_t)(p + b > lim ? lim : p + b);
            } else if (isGround(p)) {
                int sh = (p - R_GROUND) & 3;
                p = groundShade(p, sh + b > 3 ? 3 : sh + b);                            // a shaft lands on the ground
            } else if ((p >= R_HILLS && p < R_ACCENT)) {
                if (b > 2) b = 2;
                for (int k = 0; k < b; k++) p = fogStep(p);    // light scattering in front of solid things reads as haze
            }
        }
    }
}

// ---------------- weather & sky moments ----------------
static void fogVeil() {
    if (g_scene.fog < 0.03f) return;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint8_t& p = fb[y * W + x];
            float a = s_fogAmt[p];
            if (a <= 0.f) continue;
            float r = ign(x, y);
            if (r < a) p = fogStep(p);
            if (r < a - 0.5f) p = fogStep(p);
        }
}

static void drawRain() {
    const Scene& sc = g_scene;
    if (sc.rain < 0.03f) return;
    int N = (int)(sc.rain * 230.f + sc.storm * 140.f);
    float slant = 0.12f + sc.wind * 0.55f;
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
        if (p >= R_WATER) {
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
        float d = wrapPi(a - PI_F);
        float wgt = expf(-d * d / 0.9f);
        if (wgt < 0.03f) continue;
        float n = fbmP(angU(a, 48), g_t * 0.06f, 48, 3, 0xA0A0u);
        float I = clampf((n - 0.36f) / 0.3f, 0.f, 1.f);
        I = I * I * (3.f - 2.f * I) * wgt * au;
        if (I < 0.03f) continue;
        float yb = 46.f + 6.f * sinf(a * 3.f + g_t * 0.12f) + 10.f * (fbmP(angU(a, 8), g_t * 0.03f, 8, 2, 0xA1A1u) - 0.5f);
        float hgt = 30.f + 22.f * fbmP(angU(a, 12), g_t * 0.05f, 12, 2, 0xA2A2u);
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
static inline uint8_t leafI(int s, bool far) { return R_LEAF + (far ? 8 : 0) + clampi(s, 0, 7); }
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
                if (!L.far && up < flareRows + 2 && lu < -0.25f && ((hs >> 5) & 3u) == 0) { put(x, y, leafI(2 + (int)((hs >> 8) & 1u), false)); continue; }
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
    canopy(sx + L.sway, ytop - Ry * 0.55f, Rx, Ry, L, seed, s);
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

static void drawTuft(float sx, float baseY, float s, uint32_t seed, float sway) {
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
static void drawOutcrop(float sx, float baseY, float s, float halfW, float hgt, const Lighting& L, uint32_t seed) {
    int hw = (int)(halfW * s), ht = (int)(hgt * s);
    int cx = (int)lroundf(sx), by = (int)lroundf(baseY);
    if (hw < 1 || ht < 1) { put(cx, by - 1, stoneI(3, L.far)); return; }
    bool sunRight = L.side >= 0.f;
    int splitX = cx + (sunRight ? -hw / 4 : hw / 4);
    float band = fmaxf(3.f, s * 0.7f);            // ledge height in px
    float block = fmaxf(3.f, s * 1.1f);           // block width in px
    float jagW = fmaxf(1.f, s * 0.35f);
    for (int dx = -hw; dx <= hw; dx++) {
        int x = cx + dx;
        if (x < 0 || x >= W) continue;
        float e = fabsf((float)dx) / hw;
        uint32_t hj = hash32(seed ^ (uint32_t)(int)floorf((dx + 4000) / jagW) * 0x9E37u);
        float jag = (hj & 7) / 7.f * 0.08f * ht;
        float q = e > 0.7f ? (e - 0.7f) / 0.3f : 0.f;
        int yt = (int)(by - ht + jag + q * q * 0.5f * ht);          // rounded shoulders
        bool litFace = (x > splitX) == sunRight;
        int face = L.back ? 2 : (litFace ? 5 : 2);
        float wave = (vnoiseP((dx + 4000) / (block * 1.7f), (float)(seed & 255), 4096, seed) - 0.5f) * band * 1.4f;
        for (int y = yt < 0 ? 0 : yt; y <= by && y < H; y++) {
            float up = by - y + wave, d = y - yt;                   // ledges wander across the face
            int bi = (int)floorf(up / band);
            float inBand = up - bi * band;                           // 0 at the ledge's bottom
            uint32_t hb = hash32(seed ^ (uint32_t)bi * 0x632BE5ABu);
            float bw = block * (0.7f + ((hb >> 8) & 255) / 255.f * 1.3f);   // each ledge has its own block size
            float slant = (((hb >> 16) & 255) / 255.f - 0.5f) * 0.8f;        // joints lean
            float bx = (dx + 4000 + (hb & 255) / 255.f * bw + inBand * slant) / bw;
            int bk = (int)bx;
            float inBlock = bx - bk;
            uint32_t hk = hash32(hb ^ (uint32_t)bk * 0x85EBCA77u);
            int sh = face + (int)((hk & 3) == 0) - (int)((hk & 3) == 1);
            if (inBand > band - 1.2f) sh += 1;                       // ledge top catches light
            else if (inBand < 1.f) sh -= 2;                          // shadow under the ledge above
            if (inBlock < 1.f / bw && ((hk >> 3) & 3) != 0) sh -= 2; // joint (some blocks fused)
            uint32_t g = hash32((uint32_t)x * 0x27D4EB2Du ^ (uint32_t)y * 0x165667B1u ^ seed);
            if ((g & 7u) == 0) sh += ((g >> 3) & 1) ? 1 : -1;        // grain
            if (d < 1.f + s * 0.05f) sh = L.back ? 4 : 6;            // lit rim along the top
            if (up < s * 0.15f) sh -= 1;                             // grounded base
            if (x == splitX && !L.back) sh = litFace ? 6 : 1;        // the edge between the faces
            if (L.back && (dx == -hw || dx == hw)) sh = 7;
            uint8_t c = stoneI(clampi(sh, 0, 7), L.far);
            if (!L.far && ((hk >> 4) & 7) == 0 && inBand > band * 0.55f && ((g >> 5) & 1)) c = leafI(2 + (int)((g >> 6) & 1), false);  // moss in cracks
            fb[y * W + x] = c;
        }
        if (!L.far && yt >= 1 && yt < H && ((hj >> 5) & 3) != 0) put(x, yt - 1, leafI(2 + (int)((hj >> 8) & 3), false));
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
static const char* BIRD_UP[] = {"k...k", ".k.k.", "..k.."};
static const char* BIRD_DN[] = {".....", "kkkkk", "..k.."};

static uint8_t spriteColor(char ch) {
    switch (ch) {
        case 'd': return R_ACCENT + A_DEER_D;
        case 'b': return R_ACCENT + A_DEER;
        case 'l': return R_ACCENT + A_DEER_L;
        case 'w': return R_ACCENT + A_WHITE;
        case 'g': return R_ACCENT + A_RAB;
        case 'h': return R_ACCENT + A_RAB_L;
        case 'a': return R_ACCENT + A_DEER_L;
        case 'k': return R_ACCENT + A_SIL;
        default:  return 0;
    }
}
// Keep one pixel grid: sprites draw at their native size (or exactly 2x), never at fractional scales.
static void drawSpriteI(const char* const* rows, int nr, float cx, float baseY, int k, bool flip) {
    int nc = (int)strlen(rows[0]);
    int x0 = (int)cx - nc * k / 2, y0 = (int)baseY - nr * k;
    for (int y = 0; y < nr * k; y++)
        for (int x = 0; x < nc * k; x++) {
            int sc = x / k;
            if (flip) sc = nc - 1 - sc;
            char ch = rows[y / k][sc];
            if (ch != '.') put(x0 + x, y0 + y, spriteColor(ch));
        }
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

static void drawCritter(const FieldCritter& c) {
    float e = g_t - c.t0;
    float fe = e > c.idleT ? e - c.idleT : 0.f;
    float cxw = c.x - g_camX, cyw = c.y - g_camY;
    float rng = sqrtf(cxw * cxw + cyw * cyw);
    if (rng < 0.5f) return;
    float rel = wrapPi(atan2f(cyw, cxw) - g_yaw);
    float sx = W * 0.5f + rel * PXR, by = HOR + ZK / rng, s = PXR / rng;
    bool fleeing = fe > 0.f;
    bool faceRight = fleeing ? c.dir > 0 : c.faceRight;
    if (c.kind == 0) {
        float hop = fleeing ? fabsf(sinf(fe * 9.f)) * 0.45f * s : 0.f;
        drawCritterSprite(true, sx, by - hop, 1.5f * s, !faceRight);
    } else if (c.kind == 1) {
        float hop = fleeing ? fabsf(sinf(fe * 14.f)) * 0.25f * s : 0.f;
        drawCritterSprite(false, sx, by - hop, 0.45f * s, !faceRight);
    } else {
        for (int b = 0; b < 4; b++) {
            float ox = (b - 1.5f) * 0.5f * s + sinf(b * 2.1f) * 0.2f * s;
            float lift = fleeing ? fe * fe * (1.2f + b * 0.3f) * s : 0.f;
            bool flap = fleeing ? (((int)(g_t * 10.f) + b) & 1) : false;
            float bob = fleeing ? 0.f : (sinf(g_t * 6.f + b) > 0.6f ? 0.05f * s : 0.f);
            if (fleeing) drawSprite(flap ? BIRD_UP : BIRD_DN, 3, sx + ox, by - lift - bob, fmaxf(0.35f * s / 5.f, 0.6f), false);
            else put((int)(sx + ox), (int)(by - 1 + bob), R_ACCENT + A_BIRD);
        }
    }
}

// Near field, painted far to near. Insertion sort: the order barely changes frame to frame.
static uint16_t s_order[FIELD_N];
static float    s_range[FIELD_N];
static bool     s_orderInit = false;

static void drawObjects() {
    const FieldObj* objs = fieldObjects();
    const FieldCritter& crit = fieldCritter();
    if (!s_orderInit) { for (int i = 0; i < FIELD_N; i++) s_order[i] = (uint16_t)i; s_orderInit = true; }
    for (int i = 0; i < FIELD_N; i++) { float ex = objs[i].x - g_camX, ey = objs[i].y - g_camY; s_range[i] = ex * ex + ey * ey; }
    for (int i = 1; i < FIELD_N; i++) {
        uint16_t k = s_order[i];
        float v = s_range[k];
        int j = i - 1;
        while (j >= 0 && s_range[s_order[j]] < v) { s_order[j + 1] = s_order[j]; j--; }
        s_order[j + 1] = k;
    }
    float critR2 = crit.on ? (crit.x - g_camX) * (crit.x - g_camX) + (crit.y - g_camY) * (crit.y - g_camY) : -1.f;
    bool critDone = !crit.on;

    for (int n = 0; n < FIELD_N; n++) {
        const FieldObj& o = objs[s_order[n]];
        float r2 = s_range[s_order[n]];
        if (!critDone && critR2 >= r2) { g_alpha = 1.f; drawCritter(crit); critDone = true; }
        if (o.type == OBJ_NONE) continue;
        float rng = sqrtf(r2);
        if (rng < 0.35f) continue;
        float app = atan2f(o.y - g_camY, o.x - g_camX);
        float rel = wrapPi(app - g_yaw);
        // angular half-size of the widest part, for culling
        float halfWorld = o.type == OBJ_BROAD ? o.b * 1.3f : o.type == OBJ_OUTCROP ? o.a : o.type == OBJ_PINE ? o.a * 0.22f : 1.f;
        float halfAng = atanf(halfWorld / rng);
        if (fabsf(rel) - halfAng > FOV * 0.5f + 0.02f) continue;
        if (!o.solid && rng < 0.9f) continue;

        float sx = W * 0.5f + rel * PXR, by = HOR + ZK / rng, s = PXR / rng;
        float sunRel = wrapPi(SUN_AZ - app);
        Lighting Lg;
        Lg.side = sinf(sunRel);
        Lg.back = cosf(sunRel) * lowSun * sunVis > 0.45f;
        Lg.far = rng > 11.f * (1.f - 0.6f * g_scene.fog);
        float gustNow = g_scene.gust * (0.55f + 0.45f * sinf(g_t * (1.1f + (o.seed & 7) * 0.07f) + ((o.seed >> 8) & 255) * 0.025f));
        float hgt = o.type == OBJ_PINE || o.type == OBJ_BROAD ? o.a : 1.f;
        Lg.sway = gustNow * 0.06f * hgt * s;
        g_alpha = clampf((FIELD_R - rng) / 5.f, 0.f, 1.f);       // re-dreamt things fade in at the edge
        if (g_alpha <= 0.05f) continue;

        switch (o.type) {
            case OBJ_PINE:    drawPine(sx, by, s, o.a, Lg, o.seed); break;
            case OBJ_BROAD:   drawBroad(sx, by, s, o.a, o.b, Lg, o.seed); break;
            case OBJ_BUSH:    if (rng < 22.f) drawBush(sx, by, s, o.a, Lg, o.seed); break;
            case OBJ_FLOWERS: if (rng < 10.f) drawFlowers(sx, by, s, o.seed); break;
            case OBJ_TUFT:    if (rng < 5.f) drawTuft(sx, by, s, o.seed, gustNow * 2.f); break;
            case OBJ_BOULDER: drawBoulder(sx, by, s, o.a, Lg, o.seed); break;
            case OBJ_OUTCROP: drawOutcrop(sx, by, s, o.a, o.b, Lg, o.seed); break;
            default: break;
        }
    }
    g_alpha = 1.f;
    if (!critDone) drawCritter(crit);
}

// ---------------- entry ----------------
void renderInit() {
    if (!s_rayT) s_rayT = (uint8_t*)allocScratch(RAY_NB * RAY_NR);
    for (int r = 0; r <= 16; r++)
        for (int dy = 0; dy <= r; dy++) {
            float v = (r + 0.3f) * (r + 0.3f) - dy * dy;
            HW[r][dy] = v > 0.f ? (uint8_t)sqrtf(v) : 0;
        }
    memset(fb, 0, sizeof(fb));
}

float sunElevation(float tod) { return sampleKey(tod).elev; }

void renderFrame(float yaw, float distance, float speed, float timeSec) {
    const Scene& sc = g_scene;
    g_yaw = yaw; g_dist = distance; g_t = timeSec;
    g_camX = CAM_L * cosf(yaw); g_camY = CAM_L * sinf(yaw);
    S = sampleKey(g_scene.tod);
    weatherKey(sc);

    float cover = 1.f - 0.85f * clampf((sc.cloud - 0.6f) / 0.35f, 0.f, 1.f);
    sunVis = clampf((S.elev + 0.12f) / 0.1f, 0.f, 1.f) * cover * (1.f - sc.rain * 0.8f);
    lowSun = clampf(1.f - S.elev / 0.45f, 0.f, 1.f) * sunVis;
    sunX = W * 0.5f + wrapPi(SUN_AZ - yaw) * PXR;
    sunY = HOR - 5.f - S.elev * 80.f;
    s_rainbowSwap = sc.rainbow > 0.02f && S.night < 0.3f && fabsf(wrapPi(SUN_AZ - yaw)) > FOV * 0.5f + 0.35f;
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
    drawMist(HOR - 14 - (int)(sc.fog * 18.f), HOR);
    drawRainbow();
    drawGround();
    shoreline();
    castShadows();
    drawMist(HOR, HOR + 10 + (int)(sc.fog * 14.f));
    drawObjects();
    cleanOrphans();
    fogVeil();
    drawCrepuscular();
    drawFireflies();
    drawBolt();
    drawRain();

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

const uint8_t*  frameBuffer()     { return fb; }
const uint16_t* framePalette565() { return pal565; }
const uint8_t*  framePaletteRGB() { return palRGB; }
int             frameBob()        { return s_bob; }

}
