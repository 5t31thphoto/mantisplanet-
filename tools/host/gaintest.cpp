#include "harness_core.inc"
#include "../../src/journey.h"
#include <chrono>
namespace mantis { extern bool g_noGroundCache; }
using namespace mantis;
static double timeFrames(int n, bool cache, float speed) {
  g_noGroundCache = !cache;
  double tot = 0;
  for (int i = 0; i < n; i++) {
    float t = millis() / 1000.f + i * 0.033f;
    auto t0 = std::chrono::high_resolution_clock::now();
    renderFrame(currentYaw, totalDistance, speed, t);
    tot += std::chrono::duration<double, std::micro>(std::chrono::high_resolution_clock::now() - t0).count();
  }
  g_noGroundCache = false;
  return tot / n;
}
int main() {
  setup(); for (int i = 0; i < 120; i++) frame(0, 0);
  const int ids[5] = {B_FOREST, B_AUTUMN, B_JAPAN, B_RAINFOREST, B_LAKES};
  const char* N[5] = {"forest", "autumn", "japan", "rainforest", "lakes"};
  printf("standing still (fishing, watching):\n");
  for (int j = 0; j < 5; j++) {
    float D = 60000.f + j * 9000.f;
    totalDistance = D; frame(0, 0);
    for (float d = D - 3000.f; d < D + 3000.f; d += 200.f) journeyForce(d, (uint8_t)ids[j]);
    setTimeOfDay(0.45f, millis() / 1000.f, totalDistance); fieldInit(currentYaw, totalDistance);
    for (int i = 0; i < 20; i++) frame(0, 0);
    double off = 0, on = 0; for (int r = 0; r < 6; r++) { off += timeFrames(20, false, 0.f); on += timeFrames(20, true, 0.f); } off /= 6; on /= 6;
    printf("  %-10s %6.0f us -> %6.0f us  (%.0f%% less)\n", N[j], off, on, 100.0 * (off - on) / off);
  }
  // at a biome border: the blend memo
  journeyInit(4242u, 0.f);
  float Db = biomeStart(20000.f) - 5.f;
  totalDistance = Db; frame(0, 0); fieldInit(currentYaw, totalDistance);
  for (int i = 0; i < 10; i++) frame(0, 0);
  printf("at a biome border, walking: %.0f us\n", timeFrames(60, false, 0.6f));
}
