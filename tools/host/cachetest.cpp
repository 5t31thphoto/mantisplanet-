// The ground cache must be exact: the same moment rendered cold and warm, every pixel identical.
#include "harness_core.inc"
#include "../../src/journey.h"
namespace mantis { extern bool g_noGroundCache; }
using namespace mantis;
int main() {
  setup(); for (int i = 0; i < 120; i++) frame(0, 0);
  int bad = 0, total = 0;
  const int ids[6] = {B_FOREST, B_LAKES, B_JAPAN, B_KARST, B_COAST, B_REDWOODS};
  for (int j = 0; j < 6; j++) {
    float D = 60000.f + j * 9000.f;
    totalDistance = D; frame(0, 0);
    for (float d = D - 3000.f; d < D + 3000.f; d += 200.f) journeyForce(d, (uint8_t)ids[j]);
    setTimeOfDay(0.45f, millis() / 1000.f, totalDistance); fieldInit(currentYaw, totalDistance);
    for (int i = 0; i < 20; i++) frame(0, 0);
    for (int k = 0; k < 3; k++) {
      float t = millis() / 1000.f + k * 0.37f;
      g_noGroundCache = true;  renderFrame(currentYaw, totalDistance, 0.f, t);
      static uint8_t cold[160 * 120]; memcpy(cold, frameBuffer(), sizeof cold);
      g_noGroundCache = false; renderFrame(currentYaw, totalDistance, 0.f, t);     // fills the cache
      renderFrame(currentYaw, totalDistance, 0.f, t);                               // uses it
      const uint8_t* warm = frameBuffer();
      int diff = 0; for (int p = 0; p < 160 * 120; p++) if (cold[p] != warm[p]) diff++;
      total++; if (diff) { bad++; printf("  biome %d: %d pixels differ\n", ids[j], diff); }
    }
  }
  printf("  ground cache: %d of %d frames pixel-identical  %s\n", total - bad, total, bad ? "FAIL" : "ok");
  printf(bad ? "FAIL\n" : "PASS\n");
  return bad;
}
