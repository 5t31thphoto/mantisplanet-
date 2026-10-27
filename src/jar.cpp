#include "jar.h"
#include "jar_art.h"
#include "bugs.h"
#include "display.h"
#include "ui.h"
#include "mem.h"
#include "audio.h"
#include "haptics.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

namespace mantis {

// ---------------- the bugs, as little sprites (2 frames each, drawn at 2x) ----------------
struct PalE { char c; uint8_t r, g, b; };
struct BugArt { int w, h; const char* f[2][8]; PalE pal[8]; bool flies; };
static const BugArt ART[BAIT_KINDS] = {
    // worm
    {10, 3, {{"..pppp....", ".p....pp..", "l.......pd"}, {"l.pp......", ".p..pp..p.", "......ppd."}},
     {{'p', 226, 120, 132}, {'l', 250, 180, 180}, {'d', 160, 70, 90}}, false},
    // cricket
    {9, 5, {{"a......a.", ".a....a..", "..bbbbbh.", ".Bbbbbbbh", "L..l.l.L."}, {"a.....a..", ".a...a...", "..bbbbbh.", ".Bbbbbbbh", ".L.l.l..L"}},
     {{'a', 120, 96, 70}, {'b', 70, 48, 30}, {'B', 110, 80, 46}, {'h', 40, 30, 24}, {'l', 60, 45, 30}, {'L', 100, 74, 44}}, false},
    // grasshopper
    {10, 5, {{"........aa", ".......a..", "..ggggggh.", ".GGgggggge", "L...l.l.L."}, {".......a.a", "......a...", "..ggggggh.", ".GGgggggge", ".L..l.l..L"}},
     {{'g', 120, 180, 64}, {'G', 80, 130, 40}, {'h', 96, 156, 52}, {'e', 30, 30, 20}, {'a', 130, 160, 80}, {'l', 70, 110, 40}, {'L', 90, 140, 50}}, false},
    // beetle
    {7, 6, {{".a...a.", "..hhh..", ".SSsSS.", ".SsSSS.", ".SSSSS.", "l.l.l.l"}, {".a...a.", "..hhh..", ".SSsSS.", ".SsSSS.", ".SSSSS.", ".l.l.l."}},
     {{'a', 70, 70, 84}, {'h', 40, 40, 52}, {'S', 32, 34, 52}, {'s', 130, 140, 190}, {'l', 44, 44, 56}}, false},
    // mayfly
    {9, 6, {{"....ww...", "...www...", "...ww....", "tbbbbbh..", "t........", "t........"}, {".........", "...www...", "..wwww...", "tbbbbbh..", "t........", "t........"}},
     {{'w', 214, 228, 238}, {'b', 206, 186, 116}, {'h', 150, 120, 60}, {'t', 186, 176, 146}}, true},
    // firefly
    {7, 4, {{"..ww...", ".wwww..", "GGbbbh.", "..l.l.."}, {"..ww...", ".wwww..", "ggbbbh.", "..l.l.."}},
     {{'w', 176, 176, 188}, {'G', 230, 255, 120}, {'g', 96, 104, 54}, {'b', 64, 54, 42}, {'h', 210, 80, 54}, {'l', 44, 38, 32}}, true},
    // jewel beetle
    {7, 6, {{".a...a.", "..hhh..", ".SSsSS.", ".SsSSS.", ".SSSSS.", "l.l.l.l"}, {".a...a.", "..hhh..", ".SSsSS.", ".SsSSS.", ".SSSSS.", ".l.l.l."}},
     {{'a', 60, 110, 100}, {'h', 30, 96, 96}, {'S', 40, 146, 94}, {'s', 236, 222, 96}, {'l', 30, 80, 70}}, false},
    // banana slug
    {11, 4, {{"........e.e", "........y.y", "..yyyyyyyy.", "YYyykyyyky."}, {".......e.e.", ".......y.y.", ".yyyyyyyy..", "YYyyyykyy.."}},
     {{'y', 234, 212, 74}, {'Y', 202, 182, 52}, {'k', 112, 92, 32}, {'e', 44, 40, 30}}, false},
    // sand crab
    {9, 5, {{".c.e.e.c.", "cc.....cc", ".ooooooo.", "ooOOOOOoo", "l.l...l.l"}, {"...e.e...", ".c.....c.", "coooooooc", "ooOOOOOoo", ".l.l.l.l."}},
     {{'c', 232, 112, 82}, {'o', 242, 152, 112}, {'O', 212, 112, 82}, {'e', 30, 30, 30}, {'l', 202, 102, 72}}, false},
    // ice worm
    {10, 3, {{"..pppp....", ".p....pp..", "l.......pd"}, {"l.pp......", ".p..pp..p.", "......ppd."}},
     {{'p', 200, 228, 252}, {'l', 250, 255, 255}, {'d', 140, 178, 220}}, false},
    // caterpillar
    {10, 4, {{".g.g.g.g..", "GGGGGGGGh.", "yGyGyGyGhe", ".l.l.l.l.."}, {"...g.g.g..", "..GGGGGGh.", "GG.yGyGGhe", "l...l.l..."}},
     {{'g', 124, 204, 84}, {'G', 82, 172, 62}, {'y', 242, 222, 84}, {'h', 30, 30, 26}, {'e', 210, 210, 210}, {'l', 62, 112, 42}}, false},
    // silk moth
    {9, 6, {{"a.......a", ".a.....a.", "WWW.b.WWW", "WWWWbWWWW", ".WW.b.WW.", "....b...."}, {"a.WW.WW.a", ".aWWbWWa.", "WWWWbWWWW", ".WW.b.WW.", "....b....", "........."}},
     {{'W', 242, 238, 228}, {'a', 182, 162, 132}, {'b', 212, 202, 182}}, true},
    // desert beetle
    {8, 5, {{".a....a.", "..hhhh..", ".BBBBBB.", "BBbBBbBB", "l..l..l."}, {".a....a.", "..hhhh..", ".BBBBBB.", "BBbBBbBB", ".l..l..l"}},
     {{'B', 42, 38, 36}, {'b', 74, 68, 62}, {'h', 30, 28, 26}, {'a', 64, 58, 52}, {'l', 52, 46, 44}}, false},
    // moon moth
    {11, 8, {{"..a.....a..", "...a...a...", "LLLL.b.LLLL", "LLeL.b.LeLL", ".LLL.b.LLL.", "..LL.b.LL..", "..T.....T..", ".T.......T."},
             {"..a.....a..", "...aLbLa...", "..LLLbLLL..", "..LeLbLeL..", "...LLbLL...", "...L.b.L...", "...T...T...", "..T.....T.."}},
     {{'L', 172, 232, 172}, {'e', 232, 202, 92}, {'b', 242, 242, 222}, {'a', 222, 212, 162}, {'T', 142, 202, 152}}, true},
};

// ---------------- the jar's contents ----------------
static constexpr int MAXB = 16;
struct JBug { uint8_t kind; float x, y, vx, vy, phase, hopT; bool flip, onGlass; };
static inline float hopOff(const JBug& b) { return b.hopT > 0.f ? sinf(3.14159f * b.hopT / 0.5f) * 7.f : 0.f; }   // a hop: up and down in half a second
static JBug s_b[MAXB];
static int s_nb = 0;
static bool s_on = false;
static float s_t = 0.f, s_msgT = 0.f;
static char s_msg[40] = {0};
static uint16_t* s_bg = nullptr;                     // the dimmed snapshot behind the jar (PSRAM)
static constexpr int SW = 320, SH = 240, SC = 2;      // screen, sprite scale
static constexpr int JX = (SW - JAR_W * SC) / 2, JY = 24;
static constexpr float AIR_X0 = 18.f, AIR_X1 = 64.f, AIR_Y0 = 30.f, AIR_Y1 = 58.f, FLOOR_Y = 67.f;
static uint32_t s_rng = 0x1AB5u;
static float frand() { s_rng = s_rng * 1664525u + 1013904223u; return (s_rng >> 8) * (1.f / 16777216.f); }

bool jarActive() { return s_on; }

static inline uint16_t blend565(uint16_t d, uint16_t s, int a) {     // a: 0..255
    int dr = (d >> 11) & 31, dg = (d >> 5) & 63, db = d & 31;
    int sr = (s >> 11) & 31, sg = (s >> 5) & 63, sb = s & 31;
    int r = dr + ((sr - dr) * a >> 8), g = dg + ((sg - dg) * a >> 8), b = db + ((sb - db) * a >> 8);
    uint16_t v = (uint16_t)((r << 11) | (g << 5) | b);
    return v ? v : 0x0821;
}
static void layer(uint16_t* ov, const uint16_t* c, const uint8_t* a) {
    for (int y = 0; y < JAR_H; y++)
        for (int x = 0; x < JAR_W; x++) {
            int al = a[y * JAR_W + x];
            if (!al) continue;
            uint16_t col = c[y * JAR_W + x];
            for (int dy = 0; dy < SC; dy++) {
                uint16_t* p = ov + (JY + y * SC + dy) * SW + JX + x * SC;
                for (int dx = 0; dx < SC; dx++) p[dx] = al >= 250 ? (col ? col : 0x0821) : blend565(p[dx], col, al);
            }
        }
}
static void sprite(uint16_t* ov, const BugArt& A, int frame, int sx, int sy, bool flip) {
    for (int y = 0; y < A.h; y++)
        for (int x = 0; x < A.w; x++) {
            char ch = A.f[frame][y][flip ? A.w - 1 - x : x];
            if (ch == '.') continue;
            uint16_t col = 0;
            for (const PalE& e : A.pal) if (e.c == ch) { col = rgb565(e.r, e.g, e.b); break; }
            if (!col) continue;
            for (int dy = 0; dy < SC; dy++) for (int dx = 0; dx < SC; dx++) {
                int px = JX + (sx + x) * SC + dx, py = JY + (sy + y) * SC + dy;
                if ((unsigned)px < (unsigned)SW && (unsigned)py < (unsigned)SH) ov[py * SW + px] = col;
            }
        }
}

static void populate() {
    s_nb = 0;
    uint8_t kinds[MAXB];
    int nk = 0;
    for (int round = 0; round < 3 && nk < MAXB; round++)                 // one of each kind first, then seconds, thirds
        for (int k = 0; k < BAIT_KINDS && nk < MAXB; k++)
            if (baitCount((uint8_t)k) > round) kinds[nk++] = (uint8_t)k;
    int nGround = 0; for (int i = 0; i < nk; i++) if (!ART[kinds[i]].flies) nGround++;
    int gi = 0;
    for (int i = 0; i < nk; i++) {
        JBug& b = s_b[s_nb++];
        b.kind = kinds[i];
        const BugArt& A = ART[b.kind];
        if (A.flies) {
            b.x = AIR_X0 + frand() * (AIR_X1 - AIR_X0 - A.w);
            b.y = AIR_Y0 + frand() * (AIR_Y1 - AIR_Y0 - 6.f);
        } else {
            bool worm = b.kind == BAIT_WORM || b.kind == BAIT_ICEWORM;
            b.onGlass = !worm && (gi % 3) != 0;                             // most crawlers climb the glass; worms keep to the moss
            if (b.onGlass) {
                b.x = AIR_X0 + frand() * (AIR_X1 - AIR_X0 - A.w);
                b.y = AIR_Y0 + 2.f + frand() * (FLOOR_Y - AIR_Y0 - A.h - 8.f);
            } else {                                                       // on the moss, spread out, some further back
                float slot = (gi / 3 + 0.5f) / (float)((nGround + 2) / 3);
                b.x = AIR_X0 + slot * (AIR_X1 - AIR_X0 - A.w) + (frand() - 0.5f) * 6.f;
                b.y = FLOOR_Y - A.h - 2.f + frand() * 6.f;
            }
            gi++;
        }
        b.vx = (frand() < 0.5f ? -1.f : 1.f) * (A.flies ? 6.f : 1.5f + frand() * 2.f);
        b.vy = b.onGlass && !A.flies ? (frand() - 0.5f) * 2.f : 0.f;
        if (A.flies) b.onGlass = false;
        b.phase = frand() * 6.28f; b.hopT = 0.f; b.flip = b.vx < 0.f;
    }
}

static void say(const char* m) { snprintf(s_msg, sizeof s_msg, "%s", m); s_msgT = 2.5f; }

static void baitLine(char* out, int n) {
    int sel = baitSelected();
    if (sel < 0) snprintf(out, n, "ON THE HOOK: LURE");
    else {
        char nm[24]; int k = 0; for (const char* p = baitName((uint8_t)sel); *p && k < 23; p++) nm[k++] = (char)toupper((unsigned char)*p); nm[k] = 0;
        snprintf(out, n, "ON THE HOOK: %s X%d", nm, baitCount((uint8_t)sel));
    }
}

static void lureIcon(uint16_t* ov, int x0, int y0) {                 // a little spoon lure, bottom-right
    for (int y = 0; y < 12; y++) for (int x = 0; x < 34; x++) ov[(y0 + y) * SW + x0 + x] = rgb565(30, 26, 40);
    for (int k = 0; k < 6; k++) { ov[(y0 + 3 + k / 2) * SW + x0 + 3 + k] = rgb565(220, 220, 228); ov[(y0 + 6 + k / 3) * SW + x0 + 4 + k] = rgb565(170, 170, 184); }
    ov[(y0 + 9) * SW + x0 + 10] = rgb565(230, 70, 60);
    uiText(ov, SW, SH, x0 + 13, y0 + 3, "LURE", rgb565(236, 226, 196), 1, false);
}

static void draw() {
    uint16_t* ov = overlayBuffer();
    if (!ov || !s_bg) return;
    memcpy(ov, s_bg, SW * SH * sizeof(uint16_t));
    layer(ov, JAR_BACK_565, JAR_BACK_a);
    int order[MAXB];
    for (int i = 0; i < s_nb; i++) order[i] = i;
    for (int i = 1; i < s_nb; i++) {                                      // back to front: flyers last, crawlers by depth
        int v = order[i], j = i - 1;
        auto key = [&](int k) { return (ART[s_b[k].kind].flies ? 1000.f : s_b[k].onGlass ? -500.f : 0.f) + s_b[k].y + ART[s_b[k].kind].h; };   // the glass behind, the moss, the air
        while (j >= 0 && key(order[j]) > key(v)) { order[j + 1] = order[j]; j--; }
        order[j + 1] = v;
    }
    for (int oi = 0; oi < s_nb; oi++) {
        const JBug& b = s_b[order[oi]];
        const BugArt& A = ART[b.kind];
        int frame = A.flies ? ((int)(s_t * 9.f + b.phase) & 1) : (fabsf(b.vx) > 0.2f ? ((int)(s_t * 4.f + b.phase) & 1) : 0);
        if (b.kind == BAIT_FIREFLY) {
            frame = sinf(s_t * 2.2f + b.phase) > 0.f ? 0 : 1;          // 0 = glowing
            if (frame == 0) {                                           // a soft glow around its tail
                for (int dy = -3; dy <= 3; dy++) for (int dx = -3; dx <= 3; dx++) {
                    if (dx * dx + dy * dy > 9 || ((dx + dy) & 1)) continue;
                    int px = JX + ((int)b.x + (b.flip ? A.w - 1 : 0) + dx) * SC, py = JY + ((int)b.y + 2 + dy) * SC;
                    if ((unsigned)px < SW - 1 && (unsigned)py < SH - 1) ov[py * SW + px] = blend565(ov[py * SW + px], rgb565(200, 255, 120), 110);
                }
            }
        }
        sprite(ov, A, frame, (int)b.x, (int)(b.y - hopOff(b)), b.flip);
        if (baitSelected() == b.kind && ((int)(s_t * 3.f) & 1)) {
            int px = JX + ((int)b.x + A.w / 2) * SC, py = JY + ((int)(b.y - hopOff(b)) - 2) * SC;
            for (int k = -2; k <= 2; k++) if ((unsigned)(px + k) < SW && (unsigned)py < SH) ov[py * SW + px + k] = rgb565(255, 220, 110);
        }
    }
    layer(ov, JAR_FRONT_565, JAR_FRONT_a);
    int found = 0; for (int k = 0; k < BAIT_KINDS; k++) if (baitFound((uint8_t)k)) found++;
    char t[48]; snprintf(t, sizeof t, "BUG JAR   KINDS FOUND %d/%d", found, BAIT_KINDS);
    uiText(ov, SW, SH, (SW - uiTextWidth(t, 1)) / 2, 6, t, rgb565(250, 226, 160), 1);
    if (!s_nb) { const char* e = "EMPTY: TAP BUGS OUT THERE TO CATCH THEM"; uiText(ov, SW, SH, (SW - uiTextWidth(e, 1)) / 2, 112, e, rgb565(236, 226, 196), 1); }
    char bl[48]; baitLine(bl, sizeof bl);
    uiText(ov, SW, SH, 6, SH - 13, bl, rgb565(236, 226, 196), 1);
    lureIcon(ov, SW - 40, SH - 16);
    if (s_msgT > 0.f) uiText(ov, SW, SH, (SW - uiTextWidth(s_msg, 1)) / 2, 18, s_msg, rgb565(255, 255, 255), 1);
    overlayEnable(true);
}

void jarOpen() {
    if (!s_bg) s_bg = (uint16_t*)memSlow(SW * SH * sizeof(uint16_t));
    if (!s_bg) return;
    uint16_t row[SW];
    for (int y = 0; y < SH; y++) {
        compositeRow(y, row);
        for (int x = 0; x < SW; x++) { uint16_t v = row[x]; int r = ((v >> 11) & 31) * 9 >> 4, g = ((v >> 5) & 63) * 9 >> 4, b = (v & 31) * 9 >> 4; uint16_t d = (uint16_t)((r << 11) | (g << 5) | b); s_bg[y * SW + x] = d ? d : 0x0821; }
    }
    s_rng ^= (uint32_t)(s_t * 1000.f) + 0x9E37u;
    populate();
    s_on = true; s_msgT = 0.f;
    sfxFish(FISH_LIGHT, 0.f);
    draw();
}

void jarClose() { s_on = false; overlayClear(); overlayEnable(false); sfxFish(FISH_LIGHT, 0.f); }

void jarTap(int x, int y) {
    if (!s_on) return;
    if (x >= SW - 40 && y >= SH - 16) { baitSelect(-1); say("THE LURE IT IS"); hx::fishTick(0.3f); draw(); return; }
    for (int i = s_nb - 1; i >= 0; i--) {
        const JBug& b = s_b[i]; const BugArt& A = ART[b.kind];
        int x0 = JX + (int)b.x * SC - 6, y0 = JY + (int)(b.y - hopOff(b)) * SC - 6;
        if (x >= x0 && x < x0 + A.w * SC + 12 && y >= y0 && y < y0 + A.h * SC + 12) {
            baitSelect(b.kind);
            char nm[24]; int k = 0; for (const char* p = baitName(b.kind); *p && k < 23; p++) nm[k++] = (char)toupper((unsigned char)*p); nm[k] = 0;
            char m[40]; snprintf(m, sizeof m, "%s ON THE HOOK", nm); say(m);
            sfxFish(FISH_BUG, 0.f); hx::fishTick(0.4f);
            draw();
            return;
        }
    }
}

void jarUpdate(float dt) {
    if (!s_on) return;
    s_t += dt;
    if (s_msgT > 0.f) s_msgT -= dt;
    for (int i = 0; i < s_nb; i++) {
        JBug& b = s_b[i]; const BugArt& A = ART[b.kind];
        if (A.flies) {                                                     // fluttering through the air
            b.x += b.vx * dt + sinf(s_t * 1.7f + b.phase) * 4.f * dt;
            b.y += cosf(s_t * 1.3f + b.phase) * 5.f * dt;
            b.y = fminf(AIR_Y1, fmaxf(AIR_Y0, b.y));
        } else {
            if (b.onGlass) {                                                    // creeping over the glass, any which way
                if (sinf(s_t * 0.5f + b.phase) > -0.2f) { b.x += b.vx * 0.6f * dt; b.y += b.vy * dt; }
                if (frand() < dt * 0.3f) { b.vy = (frand() - 0.5f) * 2.f; }
                float ymax = FLOOR_Y - A.h - 6.f;
                if (b.y < AIR_Y0) { b.y = AIR_Y0; b.vy = fabsf(b.vy); }
                if (b.y > ymax) { b.y = ymax; b.vy = -fabsf(b.vy); }
                float x1g = AIR_X1 - A.w;
                if (b.x < AIR_X0) { b.x = AIR_X0; b.vx = fabsf(b.vx); }
                if (b.x > x1g) { b.x = x1g; b.vx = -fabsf(b.vx); }
                b.flip = b.vx < 0.f;
                continue;
            }
            bool hopper = b.kind == BAIT_HOPPER || b.kind == BAIT_CRICKET;
            if (hopper && b.hopT <= 0.f && frand() < dt * 0.25f) b.hopT = 0.001f;   // a hop now and then
            if (b.hopT > 0.f) {
                b.hopT += dt;
                if (b.hopT >= 0.5f) b.hopT = 0.f;
                else b.x += b.vx * 4.f * dt;                                          // and a little way along
            }
            else if (sinf(s_t * 0.6f + b.phase) > -0.3f) b.x += b.vx * dt;           // crawl, pause, crawl
        }
        float x1 = AIR_X1 - A.w;
        if (b.x < AIR_X0) { b.x = AIR_X0; b.vx = fabsf(b.vx); }
        if (b.x > x1) { b.x = x1; b.vx = -fabsf(b.vx); }
        b.flip = b.vx < 0.f;
    }
    draw();
}

}
