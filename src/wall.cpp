#include "wall.h"
#include "display.h"
#include "storage.h"
#include "fishgen.h"
#include "ui.h"
#include "mem.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

namespace mantis {

static bool s_on = false, s_viewing = false;
static int s_page = 0;
static constexpr int W = 320, H = 240, PER = 6, COLS = 3;
static constexpr int FW = 90, FH = 82, PX = 12, PY = 12, GX = (W - (COLS * FW + (COLS - 1) * PX)) / 2, GY = 26;   // frame size, gaps, a centred grid

static inline uint32_t hsh(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
static inline int clampi(int v, int a, int b) { return v < a ? a : (v > b ? b : v); }

static void wood(uint16_t* ov) {                                     // warm planks with grain and seams
    for (int y = 0; y < H; y++) {
        int plank = y / 30, py = y % 30;
        for (int x = 0; x < W; x++) {
            float g = sinf((x + plank * 97) * 0.035f + sinf((y + plank * 13) * 0.21f + x * 0.011f) * 1.8f);
            float k = 0.78f + 0.12f * g + 0.06f * ((hsh((uint32_t)(x / 3) * 977u ^ (uint32_t)plank * 31u) & 255) / 255.f);
            if (py == 0) k = 0.42f;                                  // the seam between planks
            else if (py == 1) k += 0.12f;
            int r = (int)(126 * k), gg = (int)(80 * k), b = (int)(50 * k);
            ov[y * W + x] = rgb565(r, gg, b);
        }
    }
}

static void frameAt(uint16_t* ov, int fx, int fy, const CatchRec& c) {
    for (int y = fy - 4; y < fy + FH + 4; y++)                       // dark wood frame with a bevel
        for (int x = fx - 4; x < fx + FW + 4; x++) {
            if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) continue;
            bool inner = x >= fx && x < fx + FW && y >= fy && y < fy + FH;
            if (inner) continue;
            bool lit = (y < fy) || (x < fx);
            ov[y * W + x] = lit ? rgb565(92, 58, 36) : rgb565(46, 28, 20);
        }
    for (int y = fy; y < fy + FH; y++)                               // the mat: cool deep water blue, darker at the edges
        for (int x = fx; x < fx + FW; x++) {
            float e = fminf(fminf((float)(x - fx), (float)(fx + FW - 1 - x)), fminf((float)(y - fy), (float)(fy + FH - 1 - y)));
            float k = fminf(1.f, 0.65f + e * 0.04f);
            ov[y * W + x] = rgb565((int)(34 * k), (int)(58 * k), (int)(78 * k));
        }
    FishSpec f;
    fishMake(f, c.seed, c.water, c.biome, c.tier);
    static uint16_t* cell = nullptr;                                   // runtime buffer (PSRAM)
    if (!cell) cell = (uint16_t*)memSlow(FW * 64 * sizeof(uint16_t));
    if (!cell) return;
    memset(cell, 0, FW * 64 * sizeof(uint16_t));
    fishRender(f, cell, FW, 64, FW / 2, 30, FW - 12);
    for (int y = 0; y < 64; y++) for (int x = 0; x < FW; x++) if (cell[y * FW + x]) ov[(fy + 2 + y) * W + fx + x] = cell[y * FW + x];
    // the brass plaque
    int py = fy + FH - 16;
    for (int y = py; y < py + 13; y++) for (int x = fx + 6; x < fx + FW - 6; x++) ov[y * W + x] = (y == py) ? rgb565(250, 222, 140) : rgb565(176, 140, 70);
    char nm[24]; int n = 0;
    const char* src = strlen(f.name) <= 14 ? f.name : f.familyName;    // the plaque: its name, or its species if that's too long
    for (const char* p = src; *p && n < 14; p++) nm[n++] = (char)toupper((unsigned char)*p);
    nm[n] = 0;
    uiText(ov, W, H, fx + (FW - uiTextWidth(nm, 1)) / 2, py + 3, nm, rgb565(44, 30, 14), 1, false);
    char len[12]; snprintf(len, sizeof len, "%d", (int)(c.lengthCm + 0.5f));
    for (int st = 0; st <= c.tier && st < 4; st++) {                 // rarity: small stars in the frame corner
        int sx = fx + FW - 10 - st * 7, sy = fy + 4;
        uiText(ov, W, H, sx, sy, "*", rgb565(255, 210, 90), 1, true);
    }
    uiText(ov, W, H, fx + 4, fy + 4, len, rgb565(220, 230, 240), 1, true);
}

static void draw() {
    uint16_t* ov = overlayBuffer();
    if (!ov) return;
    wood(ov);
    int n = catchCount();
    int pages = n ? (n + PER - 1) / PER : 1;
    s_page = clampi(s_page, 0, pages - 1);
    char title[24]; snprintf(title, sizeof title, "CATCHES %d", n);
    uiText(ov, W, H, (W - uiTextWidth(title, 1)) / 2, 8, title, rgb565(250, 226, 160), 1);
    if (!n) { const char* t = "NO CATCHES YET"; uiText(ov, W, H, (W - uiTextWidth(t, 2)) / 2, 110, t, rgb565(240, 226, 196), 2); }
    for (int k = 0; k < PER; k++) {
        int i = n - 1 - (s_page * PER + k);                          // newest first
        if (i < 0) break;
        int fx = GX + (k % COLS) * (FW + PX), fy = GY + (k / COLS) * (FH + PY);
        frameAt(ov, fx, fy, catchAt(i));
    }
    if (pages > 1) {
        char pg[32]; snprintf(pg, sizeof pg, "%d/%d", s_page + 1, pages);
        uiText(ov, W, H, (W - uiTextWidth(pg, 1)) / 2, H - 12, pg, rgb565(240, 226, 196), 1);
        if (s_page > 0) uiText(ov, W, H, 1, H / 2 - 7, "<", rgb565(250, 226, 160), 2);
        if (s_page < pages - 1) uiText(ov, W, H, W - 11, H / 2 - 7, ">", rgb565(250, 226, 160), 2);
    }
    overlayEnable(true);
}

bool wallActive() { return s_on; }
void wallOpen() { s_on = true; s_viewing = false; s_page = 0; draw(); }
void wallClose() { s_on = false; s_viewing = false; overlayClear(); overlayEnable(false); }

void wallTap(int x, int y) {
    if (!s_on) return;
    if (s_viewing) { s_viewing = false; draw(); return; }            // back from a trophy shot to the wall
    int n = catchCount();
    if (x < GX + 4) { s_page--; draw(); return; }
    if (x > W - GX - 4) { s_page++; draw(); return; }
    for (int k = 0; k < PER; k++) {
        int i = n - 1 - (s_page * PER + k);
        if (i < 0) break;
        int fx = GX + (k % COLS) * (FW + PX), fy = GY + (k / COLS) * (FH + PY);
        if (x >= fx && x < fx + FW && y >= fy && y < fy + FH) {
            if (storageLoadTrophy(catchAt(i).idx, overlayBuffer())) { s_viewing = true; overlayEnable(true); }
            return;
        }
    }
}

}
