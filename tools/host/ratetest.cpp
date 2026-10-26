// Frame-rate independence: walking, turning and the day clock must come out the same at 20 fps and at 40 fps.
#include "harness_core.inc"
using namespace mantis;
static int fails = 0;
static void check(bool ok, const char* what) { printf("  %-60s %s\n", what, ok ? "ok" : "FAIL"); if (!ok) fails++; }
struct R { float dist, yaw, tod, wanted; int bumps; };
static R run(uint32_t us) {
  setup(); for (int i = 0; i < 2000000 / (int)us * 3; i++) frame(0, 0, us);       // settle ~6 s
  setTimeOfDay(0.40f, millis() / 1000.f, totalDistance); frame(0, 0, us);
  float d0 = totalDistance, y0 = currentYaw, t0 = g_scene.tod;
  int n = (int)(3000000 / us);
  float wanted = 0.f; int bumps = 0;
  for (int i = 0; i < n; i++) { frame(0.35f, 0.f, us); wanted += currentSpeed * (us * 1e-6f) * 9.f; bumps += wasBumped; }   // walk 3 s
  for (int i = 0; i < n * 2 / 3; i++) { frame(0.f, 0.f, us); wanted += currentSpeed * (us * 1e-6f) * 9.f; }   // stop (2 s)
  float dist = totalDistance - d0;
  for (int i = 0; i < (int)(2000000 / us); i++) frame(0.f, 45.f, us);             // turn 2 s at 45 deg/s
  for (int i = 0; i < (int)(1000000 / us); i++) frame(0.f, 0.f, us);
  float yaw = (currentYaw - y0) * 57.29578f;
  float tStand = g_scene.tod;                                                       // the day, standing still (no distance in it)
  for (int i = 0; i < (int)(10000000 / us); i++) frame(0.f, 0.f, us);             // 10 s of the day going by
  (void)t0;
  return {dist, yaw, g_scene.tod - tStand, wanted, bumps};
}
int main() {
  printf("frame-rate independence\n");
  R a = run(50000), b = run(25000);
  printf("  (wanted to walk: %.2f m at 20 fps, %.2f m at 40 fps)\n", a.wanted, b.wanted);
  printf("  (20 fps: walked %.2f m, turned %.1f deg, day +%.4f | 40 fps: %.2f m, %.1f deg, +%.4f)\n", a.dist, a.yaw, a.tod, b.dist, b.yaw, b.tod);
  printf("  (frames spent bumped: %d at 20 fps, %d at 40 fps; the world ahead is dreamed up as you go, so the two runs\n"
         "   don't meet the same trees: compare what the controls ask for, and the ground covered when nothing's in the way)\n", a.bumps, b.bumps);
  check(fabsf(a.wanted - b.wanted) < 0.01f * a.wanted, "the same tilt asks for the same walk at 20 and 40 fps");
  if (!a.bumps && !b.bumps) check(fabsf(a.dist - b.dist) < 0.01f * a.dist, "unobstructed, it covers the same ground");
  check(fabsf(a.yaw - b.yaw) < 1.5f, "the same turn turns the same amount");
  check(fabsf(a.tod - b.tod) < 0.0002f, "the day passes at the same pace (standing, 10 s)");
  printf(fails ? "FAIL\n" : "PASS\n");
  return fails;
}
