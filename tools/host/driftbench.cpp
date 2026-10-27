// Controls-only drift bench (no rendering): the trembling-hand scenario from walktest, many seeds, fast.
#include "M5Unified.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
static uint64_t g_us = 1000000;
uint32_t millis() { return (uint32_t)(g_us / 1000); }
uint32_t micros() { return (uint32_t)g_us; }
void delay(uint32_t ms) { g_us += (uint64_t)ms * 1000; }
M5Class M5;
M5Canvas* g_hostLastCanvas = nullptr;
#include "../../src/sim.h"
using namespace mantis;
static float yaw = 0.f, spd = 0.f; static bool tr = false;
static void f(float ax, float ay, float az, float gx, float gy, float gz) {
  g_us += 33333; M5.Imu.data.accel = {ax, ay, az}; M5.Imu.data.gyro = {gx, gy, gz};
  updateControls(0.0333f, yaw, spd, tr);
}
int main(int argc, char** argv) {
  const float D2R = 0.0174533f;
  double sum = 0; int N = argc > 1 ? atoi(argv[1]) : 12;
  for (int seed = 1; seed <= N; seed++) {
    srand(seed);
    initControls(); f(0, 0, 1, 0, 0, 0);
    for (int i = 0; i < 300; i++) f(0, 0, 1, 0.3f, -0.4f, 1.2f);          // learn on the table
    for (int i = 0; i < 1800; i++) f(0, 0, 1, 0.3f, -0.4f, 1.2f);
    float yh = yaw;
    for (int i = 0; i < 1800; i++) {                                       // a minute in a trembling hand
      float n1 = ((rand() % 2001) - 1000) / 1000.f, n2 = ((rand() % 2001) - 1000) / 1000.f, n3 = ((rand() % 2001) - 1000) / 1000.f;
      f(0.01f * n1, 0.12f + 0.01f * n2, 0.99f, 0.3f + 2.5f * n1, -0.4f + 2.5f * n2, 1.5f + 2.5f * n3);
    }
    sum += fabsf((yaw - yh) / D2R);
  }
  // a slow deliberate turn in the same trembling hand: 1.5 deg/s for 30 s (45 deg) must survive
  double kept = 0;
  for (int seed = 1; seed <= N; seed++) {
    srand(seed + 1000);
    initControls(); f(0, 0, 1, 0, 0, 0);
    for (int i = 0; i < 300; i++) f(0, 0, 1, 0.3f, -0.4f, 1.2f);
    for (int i = 0; i < 900; i++) {                                         // settle in the hand first
      float n1 = ((rand() % 2001) - 1000) / 1000.f, n2 = ((rand() % 2001) - 1000) / 1000.f, n3 = ((rand() % 2001) - 1000) / 1000.f;
      f(0.01f * n1, 0.12f + 0.01f * n2, 0.99f, 0.3f + 2.5f * n1, -0.4f + 2.5f * n2, 1.2f + 2.5f * n3);
    }
    float y0 = yaw;
    for (int i = 0; i < 900; i++) {                                         // turning slowly about vertical (gravity ~ z)
      float n1 = ((rand() % 2001) - 1000) / 1000.f, n2 = ((rand() % 2001) - 1000) / 1000.f, n3 = ((rand() % 2001) - 1000) / 1000.f;
      f(0.01f * n1, 0.12f + 0.01f * n2, 0.99f, 0.3f + 2.5f * n1, -0.4f + 0.12f * 1.5f + 2.5f * n2, 1.2f + 0.99f * 1.5f + 2.5f * n3);
    }
    kept += fabsf((yaw - y0) / D2R) / 45.f;
  }
  printf("average hand wander %.2f deg/min, slow turn kept %.0f%% (%d seeds)\n", sum / N, 100.0 * kept / N, N);
  bool ok = sum / N < 1.6 && kept / N > 0.95;
  printf("%s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
