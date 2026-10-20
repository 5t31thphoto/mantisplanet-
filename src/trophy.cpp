#include "trophy.h"
#include "display.h"
#include "ui.h"
#include "storage.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

namespace mantis {

void trophyBuild(const FishSpec& f) {
    uint16_t* ov = overlayBuffer();
    if (!ov) return;
    overlayClear();
    const int W = 320, H = 240;
    // the fish, big
    int len = (int)fminf(292.f, fmaxf(170.f, f.lengthCm * 2.6f));
    fishRender(f, ov, W, H, W / 2, 92, len);
    // the banner: a dark translucent band (a fine checker over the scene) with a gold rule
    for (int y = 170; y < H; y++)
        for (int x = 0; x < W; x++)
            if (((x + y) & 1) == 0 || y < 172) ov[y * W + x] = y < 172 ? rgb565(214, 176, 92) : rgb565(14, 16, 30);
    char name[48]; int n = 0;
    for (const char* p = f.name; *p && n < 47; p++) name[n++] = (char)toupper((unsigned char)*p);
    name[n] = 0;
    int sc = uiTextWidth(name, 2) <= W - 16 ? 2 : 1;
    static const int TC[4][3] = {{232, 232, 224}, {150, 220, 140}, {120, 190, 255}, {255, 200, 90}};
    uiText(ov, W, H, (W - uiTextWidth(name, sc)) / 2, 178, name, rgb565(TC[f.tier][0], TC[f.tier][1], TC[f.tier][2]), sc);
    char line[64];
    static const char* TIER[4] = {"COMMON", "UNCOMMON", "RARE", "LEGENDARY"};
    char stars[5] = {0}; for (int i = 0; i <= f.tier && i < 4; i++) stars[i] = '*';
    if (f.weightKg < 1.f) snprintf(line, sizeof line, "%.0f CM  %.0f G  %s %s", f.lengthCm, f.weightKg * 1000.f, stars, TIER[f.tier]);
    else snprintf(line, sizeof line, "%.0f CM  %.1f KG  %s %s", f.lengthCm, f.weightKg, stars, TIER[f.tier]);
    uiText(ov, W, H, (W - uiTextWidth(line, 1)) / 2, 202, line, rgb565(236, 226, 196), 1);
    char where[64], when[24];
    storageDate(when, sizeof when);
    if (when[0]) snprintf(where, sizeof where, "%s  %s", biomeName(f.biome), when);
    else snprintf(where, sizeof where, "%s", biomeName(f.biome));
    for (char* p = where; *p; p++) *p = (char)toupper((unsigned char)*p);
    uiText(ov, W, H, (W - uiTextWidth(where, 1)) / 2, 220, where, rgb565(150, 156, 176), 1);
}

}
