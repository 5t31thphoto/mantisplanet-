#include "../../src/fishing.h"
#include "../../src/audio.h"
#include "../../src/haptics.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
namespace mantis { void sfxFish(uint8_t, float) {} }
namespace hx { void fishTick(float) {} void fishBite() {} void fishHookSet() {} void fishKick(float) {} void fishSnap() {} void fishTension(float, float, bool) {} void swell() {} }
namespace mantis { Scene g_scene; }
using namespace mantis;
static float wrapPi(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }
enum { CAREFUL, HAULER, PASSIVE };
struct Res { int landed, snapped, freed, timeout; float secs; };
static Res run(int tier, int style, int n) {
  Res r = {0, 0, 0, 0, 0.f};
  Scene sc = Scene(); sc.tod = 0.5f;
  for (int k = 0; k < n; k++) {
    float yaw = 0.f;
    fishingTestFight((uint8_t)tier, 8.f + (k % 5), yaw, 1000u + k * 7919u + tier * 31u + style * 101u);
    float t = 0.f, react = 0.f;
    while (fishingView().state == FS_FIGHT && t < 240.f) {
      const FishView& F = fishingView();
      float pitch = 0.f;
      if (style == CAREFUL) {
        static float noise = 0.f; static uint32_t rs = 1u;
        rs = rs * 1664525u + 1013904223u; noise += (((rs >> 8) & 1023) / 511.5f - 1.f) * 0.03f; noise *= 0.97f;
        float feel = F.tension + noise;
        bool danger = F.breakRisk > 0.15f || feel > 0.98f;      // the stutter, or a hard pull
        react = danger ? react + 1.f / 30.f : fmaxf(0.f, react - 1.f / 30.f);
        float want = react > 0.3f ? 14.f : feel < 0.6f ? -22.f : feel < 0.8f ? -12.f : -4.f;
        static float hand = 0.f; hand += (want - hand) * 0.12f;  // hands don't move instantly
        pitch = hand;
        yaw += wrapPi(F.fishAng - yaw) * 0.08f;                 // turns to face the fish
      } else if (style == HAULER) pitch = -40.f;
      fishingUpdate(1.f / 30.f, yaw, 0.f, pitch, 0.f, sc);
      t += 1.f / 30.f;
    }
    uint8_t st = fishingView().state;
    if (st == FS_LANDED) { r.landed++; r.secs += t; }
    else if (st == FS_LOST && fishingView().lostWhy == 1) r.snapped++;
    else if (st == FS_LOST) r.freed++;
    else r.timeout++;
  }
  if (r.landed) r.secs /= r.landed;
  return r;
}
int main(int argc, char** argv) {
  if (argc > 1) { int tr = atoi(argv[1]); fishingTune(tr, atof(argv[2]), atof(argv[3]), atof(argv[4]), atof(argv[5]), atof(argv[6]), atof(argv[7]), atof(argv[8])); }
  const char* TN[4] = {"common", "uncommon", "rare", "legendary"};
  const char* SN[3] = {"careful", "hauler", "passive"};
  int bad = 0;
  // targets: careful lands common ~all, uncommon 90%+, rare 70-90%, legendary 45-75%; hauling: common usually,
  // uncommon about half, rare sometimes, legendary rarely
  const float careLo[4] = {0.97f, 0.88f, 0.65f, 0.40f}, careHi[4] = {1.01f, 1.01f, 0.95f, 0.80f};
  const float haulLo[4] = {0.65f, 0.30f, 0.08f, 0.f},  haulHi[4] = {1.01f, 0.70f, 0.45f, 0.20f};
  for (int tier = 0; tier < 4; tier++) {
    for (int st = 0; st < 3; st++) {
      Res r = run(tier, st, 400);
      float land = r.landed / 400.f;
      bool ok = true;
      if (st == CAREFUL) ok = land >= careLo[tier] && land <= careHi[tier];
      if (st == HAULER) ok = land >= haulLo[tier] && land <= haulHi[tier];
      if (!ok) bad++;
      printf("%-9s %-8s landed %5.1f%%  snapped %5.1f%%  hook free %5.1f%%  stuck %4.1f%%  avg fight %5.1fs %s\n", TN[tier], SN[st],
             land * 100.f, r.snapped / 4.f, r.freed / 4.f, r.timeout / 4.f, r.secs, (st == PASSIVE) ? "" : (ok ? "ok" : "<-- off target"));
    }
  }
  printf(bad ? "FAIL\n" : "PASS\n");
  return bad ? 1 : 0;
}
