// Drag-to-align: sliding the world back into line teaches the drift tracker, so each stretch drifts less.
#include "harness_core.inc"
using namespace mantis;
static int fails = 0;
static void check(bool ok, const char* what) { printf("  %-60s %s\n", what, ok ? "ok" : "FAIL"); if (!ok) fails++; }
static float wrapA(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }
static float walkStretch(float secs) {              // walk, swaying the view +-25 deg/s (net zero), whole periods
  float y0 = currentYaw;
  int n = (int)(secs * 30.f) / 120 * 120;
  for (int i = 0; i < n; i++) frame(0.25f, 25.f * sinf(i * 6.2831853f / 120.f));
  return wrapA(currentYaw - y0);
}
static void drag(float dyaw, bool snap = false) {     // a finger sliding the world by dyaw radians
  float dxTotal = -dyaw * 320.f / renderFov();
  int steps = 20;
  M5.Touch.x = 160; M5.Touch.y = 120; M5.Touch.down = true; frame(0, 0);
  for (int i = 1; i <= steps; i++) {
    float swing = (i <= steps / 2) ? 30.f * sinf(3.14159f * i / steps) : 30.f * sinf(3.14159f * i / steps) * 0.f;   // a finger swings out a little, then settles
    M5.Touch.x = 160 + (int)lroundf(dxTotal * i / steps + (i < steps ? swing : 0.f));
    frame(0, 0);
    if (snap && i == steps / 2) writePNGish("/tmp/align_mid.ppm");
  }
  M5.Touch.down = false; frame(0, 0);
}
int main() {
  setup(); for (int i = 0; i < 120; i++) frame(0, 0);
  printf("drag to align\n");
  controlsResetDriftLearning();
  controlsTestDrift(0.2f);                          // drift the gyro learners can't see
  float e1 = walkStretch(120.f);
  drag(-e1, true);
  float r1 = controlsDriftRate();
  float e2 = walkStretch(120.f);
  drag(-e2);
  float e3 = walkStretch(120.f);
  drag(-e3);
  float e4 = walkStretch(120.f);
  printf("  (drift per 2 min: %.1f, %.1f, %.1f, %.1f deg; learned %.3f deg/s after the first)\n", e1 * 57.3f, e2 * 57.3f, e3 * 57.3f, e4 * 57.3f, r1);
  check(fabsf(e1) * 57.3f > 10.f, "uncorrected, the heading drifts (the problem)");
  check(fabsf(e2) < fabsf(e1) * 0.5f, "after one alignment it drifts less than half as much");
  check(fabsf(e4) < fabsf(e1) * 0.15f, "after three, it holds its heading");
  // a quick swipe soon after an alignment doesn't teach anything
  drag(-e4);
  float before = controlsDriftRate();
  for (int i = 0; i < 90; i++) frame(0.25f, 25.f * sinf(i * 0.05f));
  printf("  (rate after aligning %.4f, before the swipe %.4f)\n", before, controlsDriftRate());
  drag(0.6f);
  printf("  (after the swipe %.4f)\n", controlsDriftRate());
  check(fabsf(controlsDriftRate() - before) < 1e-6f, "a quick swipe just moves the view: no learning");
  // standing still the correction doesn't creep the view
  for (int i = 0; i < 400; i++) frame(0, 0);
  float ys = currentYaw; for (int i = 0; i < 1800; i++) frame(0, 0);
  check(fabsf(wrapA(currentYaw - ys)) * 57.3f < 0.5f, "standing still, the view doesn't creep");
  controlsTestDrift(0.f);
  printf(fails ? "FAIL\n" : "PASS\n");
  return fails;
}
