// Quality soaks: placement in every biome, and way-finding in every direction. No rendering: fast.
#include "harness_core.inc"
#include "../../src/journey.h"
#include "../../src/bugs.h"
using namespace mantis;
static int fails = 0;
static void check(bool ok, const char* what) { printf("  %-60s %s\n", what, ok ? "ok" : "FAIL"); if (!ok) fails++; }
static bool waterAt(float a, float D) { return lakeField(a, D) > LAKE_T || inStream(a, D) || paddyAt(a, D); }
int main(int argc, char** argv) {
  setup(); for (int i = 0; i < 30; i++) frame(0, 0);
  const char* BN[17] = {"meadow","forest","lakes","highland","falls","autumn","marsh","birch","taiga","japan","canyon","redwoods","coast","karst","tundra","rainforest","grove"};
  // ---------------- placement ----------------
  printf("placement (17 biomes x 40 spots)\n");
  long objs = 0, inWater = 0, buried = 0, solid = 0;
  int worstB = -1; long worstW = 0;
  for (int b = 0; b < 17; b++) {
    long wb = 0;
    for (int k = 0; k < 40; k++) {
      float D = 100000.f + b * 20000.f + k * 137.f;
      journeyInit(9000u + b, 0.f);
      for (float d = D - 2500.f; d < D + 2500.f; d += 150.f) journeyForce(d, (uint8_t)b);
      float yaw = k * 0.7f;
      fieldInit(yaw, D);
      const FieldObj* o = fieldObjects();
      for (int i = 0; i < FIELD_N; i++) {
        if (o[i].type == OBJ_NONE) continue;
        objs++;
        float r = sqrtf(o[i].x * o[i].x + o[i].y * o[i].y), a = atan2f(o[i].y, o[i].x);
        bool allowedWet = o[i].type == OBJ_REEDS || (o[i].type == OBJ_OUTCROP && (o[i].var & 32));
        if (!allowedWet && waterAt(a, D + r)) { inWater++; wb++; }
        if (!o[i].solid) continue;
        solid++;
        for (int j = i + 1; j < FIELD_N; j++) {
          if (!o[j].solid || o[j].type == OBJ_NONE) continue;
          float dx = o[i].x - o[j].x, dy = o[i].y - o[j].y;
          if (dx * dx + dy * dy < (o[i].radius + o[j].radius) * (o[i].radius + o[j].radius) * 0.25f) buried++;
        }
      }
    }
    if (wb > worstW) { worstW = wb; worstB = b; }
  }
  printf("  (%ld objects; %ld standing in water; %ld solid pairs buried in each other of %ld solid)\n", objs, inWater, buried, solid);
  if (worstB >= 0 && worstW) printf("  (most in water: %s, %ld)\n", BN[worstB], worstW);
  check(inWater * 1000 <= objs, "nothing stands in water that shouldn't (<0.1%)");
  check(buried * 100 <= solid, "solid things aren't buried in each other (<1%)");
  // ---------------- way-finding ----------------
  printf("way-finding (8 headings x 20 simulated minutes)\n");
  const char* DIRN[8] = {"E","SE","S","SW","W","NW","N","NE"};
  int stuckRuns = 0;
  for (int dIdx = 0; dIdx < 8; dIdx++) {
    journeyInit(777u + dIdx * 13u, 0.f);
    float H = dIdx * 0.7853982f, yaw = H, D = 50.f, dt = 1.f / 30.f;
    fieldInit(yaw, D);
    float since = 0.f, lastD = D, worstStall = 0.f, bumpT = 0.f, side = 1.f;
    int bumps = 0; uint8_t lastB = 255; char seq[96] = {0}; int ns = 0; bool sea = false;
    for (int st = 0; st < 30 * 60 * 20; st++) {
      bool bumped = false;
      float moved = fieldStep(yaw, 5.f * dt, D, &bumped);
      D += moved;
      journeyUpdate(D, yaw, moved);
      fieldTick(yaw, D, st * dt, dt, moved / dt, 0.f);
      if (bumped && moved < 5.f * dt * 0.35f) {
        if (bumpT <= 0.f) { bumps++; side = ((bumps * 2654435761u) >> 31) ? 1.f : -1.f; }
        bumpT = 0.5f;
      }
      if (bumpT > 0.f) { yaw += side * 1.4f * dt; bumpT -= dt; }                        // turn away from what's in the way
      else { float e = atan2f(sinf(H - yaw), cosf(H - yaw)); yaw += fmaxf(-0.6f * dt, fminf(0.6f * dt, e)); }   // then drift back to the heading
      since += dt;
      if (D - lastD >= 5.f) { lastD = D; since = 0.f; }
      worstStall = fmaxf(worstStall, since);
      uint8_t b = biomeIdAt(D);
      if (b != lastB && ns < 90) { ns += snprintf(seq + ns, sizeof seq - ns, "%s%s", lastB == 255 ? "" : ">", BN[b] ); lastB = b; }
      if (oceanShore(yaw, D) < 1e8f && oceanShore(yaw, D) - D < 3.f) sea = true;
    }
    bool stuck = worstStall > 30.f && !(dIdx == 4 && sea);
    if (stuck) stuckRuns++;
    printf("  %-2s %6.0f m  bumps %4d  worst stall %4.0fs  %s%s\n", DIRN[dIdx], D - 50.f, bumps, worstStall, sea ? "[reached the sea] " : "", seq);
  }
  check(stuckRuns == 0, "no heading gets you stranded (no 30 s dead-ends)");
  printf(fails ? "FAIL\n" : "PASS\n");
  return fails;
}
