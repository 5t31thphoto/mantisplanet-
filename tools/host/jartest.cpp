// The bug jar: opens over the scene with every caught bug alive inside; tapping a bug picks the bait,
// the lure tap picks the plain lure; the collection counts kinds found; closing clears the overlay.
#include "harness_core.inc"
#include "../../src/jar.h"
#include "../../src/bugs.h"
#include "../../src/display.h"
#include "../../src/journey.h"
using namespace mantis;
static int fails = 0;
static void check(bool ok, const char* what) { printf("  %-58s %s\n", what, ok ? "ok" : "FAIL"); if (!ok) fails++; }
static void shot(const char* path) {
  FILE* o = fopen(path, "wb"); fprintf(o, "P6 320 240 255\n"); uint16_t row[320];
  for (int y = 0; y < 240; y++) { compositeRow(y, row); for (int x = 0; x < 320; x++) { uint16_t v = row[x]; unsigned char c[3] = {(unsigned char)((v >> 11) << 3), (unsigned char)(((v >> 5) & 63) << 2), (unsigned char)((v & 31) << 3)}; fwrite(c, 1, 3, o); } }
  fclose(o);
}
int main() {
  setup(); for (int i = 0; i < 60; i++) frame(0, 0);
  printf("bug jar\n");
  setTimeOfDay(0.70f, millis() / 1000.f, totalDistance); for (int i = 0; i < 30; i++) frame(0, 0);
  // an empty jar first
  jarOpen(); for (int i = 0; i < 5; i++) { jarUpdate(1.f / 30.f); frame(0, 0); }
  shot("/tmp/jar_empty.ppm");
  jarClose(); frame(0, 0);
  check(!jarActive() && !overlayEnabled(), "closing the jar clears it");
  // a jar with every kind
  for (int k = 0; k < BAIT_KINDS; k++) baitAdd((uint8_t)k, 1 + (k % 3));
  int found = 0; for (int k = 0; k < BAIT_KINDS; k++) found += baitFound((uint8_t)k);
  check(found == BAIT_KINDS, "every kind counts as found once caught");
  float d0 = totalDistance;
  jarOpen();
  check(jarActive() && overlayEnabled(), "tap B: the jar opens over the scene");
  for (int i = 0; i < 90; i++) { jarUpdate(1.f / 30.f); frame(0.4f, 0.f); }
  check(fabsf(totalDistance - d0) < 0.01f, "the world waits while the jar is open (no walking)");
  shot("/tmp/jar_full.ppm");
  // tap on the lure, then on a bug (scan the screen for a bug's colour: a firefly glow / a beetle sheen is hard
  // to aim at blind, so tap every sprite cell until one picks something)
  jarTap(300, 232);
  check(baitSelected() < 0, "tapping the lure puts the plain lure on");
  bool picked = false;
  for (int y = 70; y < 170 && !picked; y += 4) for (int x = 110; x < 210 && !picked; x += 4) { jarTap(x, y); if (baitSelected() >= 0) picked = true; }
  check(picked, "tapping a bug puts that kind on the hook");
  printf("  (picked: %s)\n", picked ? baitName((uint8_t)baitSelected()) : "-");
  for (int i = 0; i < 10; i++) { jarUpdate(1.f / 30.f); frame(0, 0); }
  shot("/tmp/jar_picked.ppm");
  jarClose(); frame(0, 0);
  check(!overlayEnabled(), "and it closes cleanly");
  printf(fails ? "FAIL\n" : "PASS\n");
  return fails;
}
