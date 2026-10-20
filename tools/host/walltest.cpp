// The catch wall: catches logged, the wall drawn from the log, paging, opening a trophy shot.
#include "harness_core.inc"
#include "../../src/storage.h"
#include "../../src/wall.h"
#include "../../src/trophy.h"
#include "../../src/display.h"
#include "../../src/journey.h"
#include "../../src/fishing.h"
using namespace mantis;
static int fails = 0;
static void check(bool ok, const char* what) { printf("  %-56s %s\n", what, ok ? "ok" : "FAIL"); if (!ok) fails++; }
static void shot(const char* path) {
  FILE* o = fopen(path, "wb"); fprintf(o, "P6 320 240 255\n");
  uint16_t row[320];
  for (int y = 0; y < 240; y++) { compositeRow(y, row); for (int x = 0; x < 320; x++) { uint16_t v = row[x]; unsigned char c[3] = {(unsigned char)((v >> 11) << 3), (unsigned char)(((v >> 5) & 63) << 2), (unsigned char)((v & 31) << 3)}; fwrite(c, 1, 3, o); } }
  fclose(o);
}
int main() {
  setup(); for (int i = 0; i < 60; i++) frame(0, 0);
  printf("catch wall\n");
  int before = catchCount();
  struct { uint8_t w, b, t; } cs[8] = {{WATER_CREEK, B_MEADOW, 1}, {WATER_LAKE, B_JAPAN, 1}, {WATER_SEA, B_COAST, 2}, {WATER_LAKE, B_RAINFOREST, 1},
                                       {WATER_ICE, B_TUNDRA, 2}, {WATER_LAKE, B_LAKES, 0}, {WATER_CREEK, B_GROVE, 3}, {WATER_LAKE, B_MARSH, 1}};
  for (int i = 0; i < 8; i++) {
    FishSpec f; fishMake(f, 1000u + i * 7919u, cs[i].w, cs[i].b, cs[i].t);
    trophyBuild(f); overlayEnable(true); frame(0, 0);
    storageSaveTrophy(f);
    overlayEnable(false);
  }
  check(catchCount() == before + 8, "eight catches logged");
  wallOpen(); frame(0, 0);
  check(wallActive(), "the right button opens the wall");
  shot("/tmp/wall_p1.ppm");
  float d0 = totalDistance; for (int i = 0; i < 20; i++) frame(0.5f, 0.f);
  check(fabsf(totalDistance - d0) < 0.01f, "no walking while the wall is up");
  wallTap(316, 120); frame(0, 0); shot("/tmp/wall_p2.ppm");
  wallTap(4, 120); frame(0, 0);
  wallTap(60, 60); frame(0, 0);                             // the newest catch: top-left frame
  shot("/tmp/wall_trophy.ppm");
  wallTap(160, 120); frame(0, 0);                           // back to the wall
  wallClose(); frame(0, 0);
  check(!wallActive() && !overlayEnabled(), "closing it clears the overlay");
  // the log survives a restart
  storageBegin();
  check(catchCount() >= 8, "the catch list is read back from the card at boot");
  printf(fails ? "FAIL\n" : "PASS\n");
  return fails;
}
