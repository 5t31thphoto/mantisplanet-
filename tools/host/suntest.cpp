// The sun's arc reads as the time of day: low in the east at dawn, up out of view through the middle of the
// day (crossing the south), down in the west at dusk; facing south it moves left to right.
#include "harness_core.inc"
using namespace mantis;
static int fails = 0;
static void check(bool ok, const char* what) { printf("  %-62s %s\n", what, ok ? "ok" : "FAIL"); if (!ok) fails++; }
static float sunScreenY(float tod) { return 70 - 5.f - sunPathElevation(tod) * (160.f / renderFov()); }
static float relToSun(float yaw) { return atan2f(sinf(g_scene.sunAz - yaw), cosf(g_scene.sunAz - yaw)); }
int main() {
  setup(); for (int i = 0; i < 60; i++) frame(0, 0);
  printf("the sun's arc\n");
  check(sunScreenY(0.27f) > 30.f && sunScreenY(0.27f) < 70.f, "just after sunrise: low over the horizon, in view");
  check(sunScreenY(0.715f) > 30.f && sunScreenY(0.715f) < 70.f, "before sunset: low over the horizon, in view");
  check(sunScreenY(0.40f) < 0.f && sunScreenY(0.49f) < 0.f && sunScreenY(0.58f) < 0.f, "mid-morning to mid-afternoon: up out of view");
  setTimeOfDay(0.27f, millis() / 1000.f, totalDistance); frame(0, 0);
  check(fabsf(atan2f(sinf(g_scene.sunAz - AZ_EAST), cosf(g_scene.sunAz - AZ_EAST))) < 0.3f, "it rises in the east");
  setTimeOfDay(0.49f, millis() / 1000.f, totalDistance); frame(0, 0);
  check(fabsf(atan2f(sinf(g_scene.sunAz - AZ_SOUTH), cosf(g_scene.sunAz - AZ_SOUTH))) < 0.15f, "at noon it's due south (overhead, out of view)");
  setTimeOfDay(0.72f, millis() / 1000.f, totalDistance); frame(0, 0);
  check(fabsf(atan2f(sinf(g_scene.sunAz - AZ_WEST), cosf(g_scene.sunAz - AZ_WEST))) < 0.3f, "it sets in the west");
  setTimeOfDay(0.36f, millis() / 1000.f, totalDistance); frame(0, 0);
  float am = relToSun(AZ_SOUTH);
  setTimeOfDay(0.62f, millis() / 1000.f, totalDistance); frame(0, 0);
  float pm = relToSun(AZ_SOUTH);
  check(am < 0.f && pm > 0.f, "facing south: on your left in the morning, your right in the afternoon");
  // a strip of the day, facing the sun
  const float T[6] = {0.27f, 0.31f, 0.36f, 0.49f, 0.66f, 0.72f};
  for (int k = 0; k < 6; k++) {
    setTimeOfDay(T[k], millis() / 1000.f, totalDistance); frame(0, 0);
    float yaw = g_scene.sunAz; setYaw(yaw); currentYaw = yaw;
    for (int i = 0; i < 20; i++) frame(0, 0);
    char p[64]; snprintf(p, sizeof p, "/tmp/sun_%d.ppm", k); writePNGish(p);
  }
  printf(fails ? "FAIL\n" : "PASS\n");
  return fails;
}
