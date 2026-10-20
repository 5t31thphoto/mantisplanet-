// A contact sheet of generated fish, to judge the art.
#include "../../src/fishgen.h"
#include "../../src/fishing.h"
#include "../../src/journey.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <vector>
using namespace mantis;
int main(int argc, char** argv) {
  const int CW = 320, CH = 130, COLS = 3, ROWS = 7;
  std::vector<uint16_t> sheet(CW * COLS * CH * ROWS, 0);
  struct { uint8_t water, biome, tier; } cases[ROWS * COLS] = {
    {WATER_CREEK, B_MEADOW, 0}, {WATER_CREEK, B_FOREST, 1}, {WATER_CREEK, B_MEADOW, 2},
    {WATER_LAKE, B_LAKES, 0}, {WATER_LAKE, B_LAKES, 1}, {WATER_LAKE, B_LAKES, 2},
    {WATER_LAKE, B_MARSH, 1}, {WATER_LAKE, B_JAPAN, 0}, {WATER_LAKE, B_JAPAN, 2},
    {WATER_SEA, B_COAST, 0}, {WATER_SEA, B_COAST, 1}, {WATER_SEA, B_COAST, 2},
    {WATER_PADDY, B_KARST, 0}, {WATER_PADDY, B_KARST, 1}, {WATER_ICE, B_TUNDRA, 1},
    {WATER_LAKE, B_RAINFOREST, 1}, {WATER_LAKE, B_RAINFOREST, 2}, {WATER_CREEK, B_REDWOODS, 2},
    {WATER_LAKE, B_LAKES, 3}, {WATER_CREEK, B_GROVE, 3}, {WATER_LAKE, B_MARSH, 2},
  };
  uint32_t base = argc > 1 ? (uint32_t)atoi(argv[1]) : 1u;
  std::vector<uint16_t> cell(CW * CH);
  for (int i = 0; i < ROWS * COLS; i++) {
    FishSpec f;
    fishMake(f, base * 7919u + i * 104729u, cases[i].water, cases[i].biome, cases[i].tier);
    for (auto& v : cell) v = 0;
    int len = (int)fminf(290.f, fmaxf(150.f, f.lengthCm * 2.4f));
    fishRender(f, cell.data(), CW, CH, CW / 2, CH / 2, len);
    int ox = (i % COLS) * CW, oy = (i / COLS) * CH;
    for (int y = 0; y < CH; y++) for (int x = 0; x < CW; x++) sheet[(oy + y) * CW * COLS + ox + x] = cell[y * CW + x];
    printf("%-2d %-28s %-14s %5.0f cm %6.2f kg tier %d\n", i, f.name, f.familyName, f.lengthCm, f.weightKg, f.tier);
  }
  FILE* o = fopen("/tmp/fishsheet.ppm", "wb");
  fprintf(o, "P6 %d %d 255\n", CW * COLS, CH * ROWS);
  for (size_t k = 0; k < sheet.size(); k++) {
    uint16_t v = sheet[k];
    unsigned char rgb[3];
    if (!v) { int x = (k % (CW * COLS)), y = (int)(k / (CW * COLS)); bool chk = ((x / 8) + (y / 8)) & 1; rgb[0] = rgb[1] = rgb[2] = chk ? 52 : 44; rgb[2] += 10; }
    else { rgb[0] = (unsigned char)((v >> 11) << 3); rgb[1] = (unsigned char)(((v >> 5) & 63) << 2); rgb[2] = (unsigned char)((v & 31) << 3); }
    fwrite(rgb, 1, 3, o);
  }
  fclose(o);
}
