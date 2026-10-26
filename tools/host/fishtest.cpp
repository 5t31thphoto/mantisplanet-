// Fishing, end to end on the desktop: cast, wait, strike, fight, land; and the ways it goes wrong.
#include "harness_core.inc"
#include "../../src/fishing.h"
#include "../../src/storage.h"
#include "../../src/bugs.h"
#include "../../src/jar.h"
#include <sys/stat.h>
#include "../../src/journey.h"
using namespace mantis;
static int fails = 0;
static bool validMG(const char* path) {
  FILE* f = fopen(path, "rb"); if (!f) return false;
  unsigned char h[8]; size_t n = fread(h, 1, 8, f); fseek(f, 0, SEEK_END); long sz = ftell(f); fclose(f);
  const unsigned char want[8] = {0x4D, 0x47, 0x30, 0x31, 0x40, 0x01, 0xF0, 0x00};
  return n == 8 && memcmp(h, want, 8) == 0 && sz == 153608;
}
static int countFiles(const char* dir, const char* prefix) { int n = 0; for (int i = 1; i < 999; i++) { char p[128]; snprintf(p, sizeof p, "%s/%s_%04d.mg", dir, prefix, i); FILE* f = fopen(p, "rb"); if (f) { n = i; fclose(f); } } return n; }
static void check(bool ok, const char* what) { printf("  %-56s %s\n", what, ok ? "ok" : "FAIL"); if (!ok) fails++; }
static float g_waterYaw = 0.f;
static void aim() { setYaw(g_waterYaw); currentYaw = g_waterYaw; frame(0, 0); }
static void tap() { M5.Touch.down = true; frame(0.f, 0.f); M5.Touch.down = false; frame(0.f, 0.f); }
static bool faceWater() {                            // stand just short of a lake and face it
  for (float D = 300.f; D < 60000.f; D += 25.f) {
    if (biomeAt(D).snow > 0.5f || biomeAt(D).ocean > 0.5f) continue;
    for (int k = 0; k < 16; k++) {
      float a = k * 0.3927f;
      bool dry = true; for (float z = 0; z < 3.5f; z += 0.5f) if (lakeField(a, D + z) > LAKE_T - 0.03f || inStream(a, D + z) || paddyAt(a, D + z)) dry = false;
      bool wet = true; for (float z = 6.f; z < 10.f; z += 1.f) if (lakeField(a, D + z) <= LAKE_T) wet = false;
      if (dry && wet) { g_waterYaw = a; totalDistance = D; frame(0, 0); setYaw(a); currentYaw = a; fieldInit(a, D); for (int i = 0; i < 5; i++) frame(0, 0); return true; }
    }
  }
  return false;
}
// a sensible angler: reel, but ease off when the line starts to stutter, and face the fish
static int g_dbg = 0;
static long g_fightFrames = 0, g_onLand = 0;
static int fightOnce(int& tierOut, int maxFrames = 4000) {
  int i = 0;
  for (; i < maxFrames; i++) {
    const FishView& F = fishingView();
    if (F.state != FS_FIGHT) break;
    tierOut = F.tier;
    g_fightFrames++;
    if (!fishingWaterAt(F.fishAng, totalDistance + fmaxf(F.shoreZ, F.lineOut) + 0.05f) && !fishingWaterAt(F.fishAng, totalDistance + fmaxf(F.shoreZ, F.lineOut) - 0.35f)) g_onLand++;
    if (getenv("FTRACE") && F.tier == 1 && (i % 30) == 0 && i < 900) printf("      t%3d out %5.1f T %.2f reel %.2f ease %.2f spd %+.2f sta %.2f pitch %.1f rel %.2f\n", i / 30, F.lineOut, F.tension, F.reel, F.ease, F.lineSpeed, F.stamina, controlsPitchDeg(), atan2f(sinf(F.fishAng - currentYaw), cosf(F.fishAng - currentYaw)));
    // aim for steady pressure around 80%: ease when it stutters, crank when it goes slack
    float want = F.breakRisk > 0.05f || F.tension > 0.95f ? 14.f : F.tension < 0.6f ? -22.f : F.tension < 0.8f ? -12.f : -4.f;
    g_holdPitchDeg = want;
    float rel = atan2f(sinf(F.fishAng - currentYaw), cosf(F.fishAng - currentYaw));
    frame(0.f, -rel * 40.f);                // YAW_SIGN: turning toward the fish
  }
  g_holdPitchDeg = -999.f;
  if (g_dbg) printf("    tier %d end %d why %d after %.1fs lineOut %.1f stamina %.2f\n", tierOut, fishingView().state, fishingView().lostWhy, i / 30.f, fishingView().lineOut, fishingView().stamina);
  return fishingView().state;
}
int main() {
  setup(); for (int i = 0; i < 120; i++) frame(0, 0);
  printf("fishing\n");
  check(faceWater(), "found a lake to fish");
  tap();
  check(fishingActive(), "a tap facing water casts");
  for (int i = 0; i < 40; i++) frame(0, 0);
  check(fishingView().state == FS_WAIT, "the bobber is out, waiting");
  float d0 = totalDistance; for (int i = 0; i < 30; i++) frame(0.5f, 0.f);
  check(fabsf(totalDistance - d0) < 0.01f, "tilting with the rod out doesn't walk");
  // a bite, and the strike
  fishingForceBite(); frame(0, 0);
  check(fishingView().biting, "a bite: the bobber goes under");
  g_flickDps = 260.f; frame(0, 0);
  check(fishingView().state == FS_FIGHT, "a flick sets the hook");
  int tier = 0;
  int end = fightOnce(tier);
  printf("  (tier %d fight ended in state %d, length %.0f cm)\n", tier, end, fishingView().lengthCm);
  check(end == FS_LANDED, "a sensible angler lands it");
  for (int n = 0; n < 6; n++) {                               // more fights, watching where the fish goes
    for (int i = 0; i < 260; i++) frame(0, 0);
    aim(); tap(); for (int i = 0; i < 40; i++) frame(0, 0);
    if (fishingView().state != FS_WAIT) continue;
    fishingForceBite(); frame(0, 0); g_flickDps = 260.f; frame(0, 0);
    int t = 0; fightOnce(t);
  }
  printf("  (%ld fight frames, %ld with the fish out of the water)\n", g_fightFrames, g_onLand);
  check(g_onLand == 0, "the fish never leaves the water during a fight");
  for (int i = 0; i < 40; i++) frame(0, 0);                  // the trophy card is saved a moment after landing
  int nf = countFiles("sdcard/images/fish", "fish");
  char tp[128]; snprintf(tp, sizeof tp, "sdcard/images/fish/fish_%04d.mg", nf);
  check(nf > 0 && validMG(tp), "the trophy shot is saved as a valid .mg");
  printf("  (%s: %s)\n", tp, fishingSpec().name);
  FILE* lf = fopen("sdcard/images/fish/catches.csv", "rb"); char last[256] = {0}; if (lf) { while (fgets(last, sizeof last, lf)) {} fclose(lf); }
  check(strstr(last, fishingSpec().name) != nullptr, "and logged in catches.csv");
  printf("  (log: %s", last);
  check(storageScreenshot(), "a screenshot saves");
  int np = countFiles("sdcard/images/planet", "planet");
  char pp[128]; snprintf(pp, sizeof pp, "sdcard/images/planet/planet_%04d.mg", np);
  check(validMG(pp), "the screenshot is a valid .mg");
  for (int i = 0; i < 260; i++) frame(0, 0);
  check(!fishingActive(), "after admiring the catch, the rod is put away");
  // (fight balance per tier and play style is checked by fishsim, thousands of fights in a second)
  // bait bugs: one turns up near you, a tap catches it, it goes on the hook, and it's used when the hook sets
  {
    int before = 0; for (int k = 0; k < BAIT_KINDS; k++) before += baitCount((uint8_t)k);
    bool seen = false;
    for (int i = 0; i < 2400 && !seen; i++) { frame(0, 0); for (int b = 0; b < BUG_N; b++) if (bugs()[b].on && bugs()[b].sx > 0) seen = true; }
    check(seen, "a bait bug turns up near you while you stand still");
    int bi = 0; for (; bi < BUG_N; bi++) if (bugs()[bi].on && bugs()[bi].sx > 0) break;
    uint8_t kind = bugs()[bi].kind;
    bool caught = bugsTryCatch(bugs()[bi].sx + 3, bugs()[bi].sy - 2);
    int after = 0; for (int k = 0; k < BAIT_KINDS; k++) after += baitCount((uint8_t)k);
    check(caught && after == before + 1, "tapping it catches it");
    printf("  (caught a %s)\n", baitName(kind));
    check(baitSelected() == kind, "the first bait goes straight on the hook");
    aim(); tap(); for (int i = 0; i < 40; i++) frame(0, 0);
    fishingForceBite(); frame(0, 0); g_flickDps = 260.f; frame(0, 0);
    check(baitCount(kind) == 0 && baitSelected() < 0, "setting the hook uses the bait");
    int t2 = 0; fightOnce(t2);
    for (int i = 0; i < 260; i++) frame(0, 0);
  }
  // slow drift must not put the rod away (the view creeping 60 degrees over half a minute)
  aim(); tap(); for (int i = 0; i < 40; i++) frame(0, 0);
  for (int i = 0; i < 900 && fishingActive(); i++) frame(0.f, 2.f);
  check(fishingActive(), "slow drift doesn't put the rod away (only turning away does)");
  bugsInit();                                       // (a bug that wandered under the finger would take the tap)
  if (fishingActive()) { tap(); for (int i = 0; i < 60; i++) frame(0, 0); }
  // missing the bite, and turning away
  bugsInit();
  aim(); tap(); for (int i = 0; i < 40; i++) frame(0, 0);
  fishingForceBite(); frame(0, 0);
  bool bit = fishingView().biting, gotAway = false, stillFishing = true;
  for (int i = 0; i < 60; i++) {                     // don't strike: the bite should end on its own, rod still out
    frame(0, 0);
    if (bit && !fishingView().biting) gotAway = true;
    if (fishingView().state != FS_WAIT) stillFishing = false;
  }
  check(bit && gotAway && stillFishing, "too slow to strike: the fish gets away, keep waiting");
  for (int i = 0; i < 30; i++) frame(0.f, 90.f);
  for (int i = 0; i < 40; i++) frame(0, 0);
  check(!fishingActive(), "turning away from the water puts the rod away");
  // no water, no cast
  for (float D = totalDistance + 200.f; ; D += 50.f) {
    bool dry = true; for (float z = 0; z < 20.f; z += 0.5f) if (lakeField(currentYaw, D + z) > LAKE_T - 0.05f || inStream(currentYaw, D + z) || paddyAt(currentYaw, D + z)) dry = false;
    if (dry) { totalDistance = D; frame(0, 0); break; }
  }
  tap();
  check(!fishingActive(), "a tap with no water ahead does nothing");
  printf(fails ? "FAIL\n" : "PASS\n");
  return fails ? 1 : 0;
}
