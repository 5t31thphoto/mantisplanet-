// Rules of the walk, checked against the real firmware loop:
//   * held level, you never move (nothing walks for you)
//   * head-on into a trunk / rock / cliff you stop dead and stay put until you turn - no autopilot
//   * glancing off the side of one you slide past
//   * you never end up inside anything solid, never step into a lake, nothing goes NaN
// plus a wandering soak that reports how a blind walker fares (information only).
#include "harness_core.inc"
#include <algorithm>
#include "../../src/journey.h"
#include "../../src/fishing.h"
using namespace mantis;

static int fails = 0;
static void check(bool ok, const char* what) { printf("  %-58s %s\n", what, ok ? "ok" : "FAIL"); if (!ok) fails++; }

static void clearAhead() {
  FieldObj* o = const_cast<FieldObj*>(fieldObjects());
  for (int i = 0; i < FIELD_N; i++) {
    float d = sqrtf(o[i].x * o[i].x + o[i].y * o[i].y);
    if (d < 14.f) o[i].type = OBJ_NONE, o[i].solid = false;
  }
}
static FieldObj& place(float x, float y, float r) {
  FieldObj& t = const_cast<FieldObj*>(fieldObjects())[0];
  t.x = x; t.y = y; t.type = OBJ_BOULDER; t.a = r / 0.9f; t.radius = r; t.solid = true; t.seed = 1;
  return t;
}
static void dryStart() {        // a stretch with no lake ahead
  for (int k = 0; k < 400; k++) {
    bool wet = false;
    for (float z = 0.f; z < 14.f; z += 0.5f) if (lakeField(currentYaw, totalDistance + z) > LAKE_T - 0.05f) wet = true;
    if (!wet) return;
    totalDistance += 3.f;
  }
}

int main(int argc, char** argv) {
  int seed = argc > 1 ? atoi(argv[1]) : 1;
  float minutes = argc > 2 ? (float)atof(argv[2]) : 2.f;
  srand(seed);
  setup();
  universeInit((uint32_t)seed * 7919u);
  dryStart();
  fieldInit(currentYaw, totalDistance);
  printf("seed %d\n", seed);

  // 1. level = standing still
  float d0 = totalDistance;
  for (int f = 0; f < 300; f++) frame(0.f, (f % 60) < 30 ? 25.f : -25.f);
  check(fabsf(totalDistance - d0) < 1e-4f, "held level (turning around): never moves");

  // 2. head-on: stop dead, no creeping round it
  clearAhead();
  FieldObj& rock = place(4.f, 0.05f, 0.8f);
  for (int f = 0; f < 120; f++) frame(0.35f, 0.f);
  float stopD = totalDistance, ry = rock.y;
  for (int f = 0; f < 240; f++) frame(0.35f, 0.f);
  check(totalDistance - stopD < 0.02f && fabsf(rock.y - ry) < 0.02f, "pushing head-on into a rock: stays stopped, no autopilot");
  bool bumpedNow = wasBumped;
  check(bumpedNow, "...and it registers as a bump");
  // turn away and walk on
  for (int f = 0; f < 30; f++) frame(0.f, 90.f);
  for (int k = 0; k < 130; k++) {               // look for somewhere dry, like a player would
    bool wetAhead = false;
    for (int a = -3; a <= 3; a++) for (float z = 0.3f; z < 6.f; z += 0.4f) if (lakeField(currentYaw + a * 0.07f, totalDistance + z) > LAKE_T - 0.02f) wetAhead = true;
    if (!wetAhead) break;
    frame(0.f, 90.f);
  }
  float before = totalDistance;
  for (int f = 0; f < 60; f++) frame(0.35f, 0.f);
  if (totalDistance - before <= 1.f) {
    const FieldObj* oo = fieldObjects(); float nearest = 1e9f; int ni = -1;
    for (int i = 0; i < FIELD_N; i++) if (oo[i].solid) { float d = sqrtf(oo[i].x*oo[i].x+oo[i].y*oo[i].y) - oo[i].radius; if (d < nearest) { nearest = d; ni = i; } }
    printf("  debug: yaw %.2f moved %.3f bumped %d nearest %.2f at (%.2f,%.2f) r %.2f speed %.2f\n", currentYaw, totalDistance - before, wasBumped, nearest, ni>=0?oo[ni].x:0, ni>=0?oo[ni].y:0, ni>=0?oo[ni].radius:0, currentSpeed);
  }
  check(totalDistance - before > 1.f, "turn away and walk: free again");

  // 3. glancing: slide past
  clearAhead();
  currentYaw = 0.f; initControls(); frame(0.f, 0.f);
  FieldObj& r2 = place(3.f, 1.05f, 0.8f);       // off to the side, clipping the path
  before = totalDistance;
  for (int f = 0; f < 120; f++) frame(0.35f, 0.f);
  check(totalDistance - before > 4.f && r2.x < 0.f, "clipping the side of a rock: slide past it");

  // 4. long press mutes the speaker, the motor keeps going
  {
    auto hold = [&](int frames, int dx) { M5.Touch.x = 160; for (int f = 0; f < frames; f++) { M5.Touch.down = true; M5.Touch.x = 160 + dx * f / frames; frame(0.3f, 0.f); } M5.Touch.down = false; frame(0.3f, 0.f); };
    auto peak = [&](int frames) { g_wav.clear(); g_recordAudio = true; for (int f = 0; f < frames; f++) frame(0.3f, 0.f); g_recordAudio = false; int p = 0; for (int16_t v : g_wav) p = std::max(p, v < 0 ? -v : (int)v); return p / 32768.f; };
    bool was = audioMuted();
    hold(5, 0);
    check(audioMuted() == was, "short tap: no change");
    if (fishingActive()) { hold(5, 0); for (int f = 0; f < 60 && fishingActive(); f++) frame(0.f, 0.f); }   // that tap cast a line (water ahead): put the rod away
    hold(30, 60);
    check(audioMuted() == was, "drag: no change");
    hold(25, 0);
    check(audioMuted() && !was, "long press: muted");
    clearAhead();
    for (int k = 0; k < 40; k++) {               // face somewhere dry so there are footsteps to feel
      bool wetAhead = false;
      for (int a = -3; a <= 3; a++) for (float z = 0.3f; z < 6.f; z += 0.4f) if (lakeField(currentYaw + a * 0.07f, totalDistance + z) > LAKE_T - 0.02f) wetAhead = true;
      if (!wetAhead) break;
      frame(0.f, 90.f);
    }
    long vib0 = M5.Power.changes;
    float d0m = totalDistance;
    float pk = peak(60);
    printf("  (walked %.1f m while muted)\n", totalDistance - d0m);
    check(pk < 0.001f, "muted: speaker silent");
    check(M5.Power.changes > vib0 + 4, "muted: the motor keeps going (footsteps, kick)");
    hold(25, 0);
    pk = peak(45);
    check(!audioMuted() && pk > 0.01f, "long press again: sound back");
  }

  // 5. the window: yaw is rotation about true vertical, bias doesn't drift, hands don't wander
  {
    const float D2R = 0.0174533f;
    initControls(); frame(0.f, 0.f);
    float y0 = currentYaw;
    // pitch the unit forward 40 deg and back, then roll it 30 deg and back: the gyro sees it, the view must not
    for (int f = 0; f < 60; f++) { float a = 40.f * D2R * sinf(f / 60.f * 3.14159f); float r = (f < 30 ? 40.f : -40.f) * 3.14159f / 2.f;
      frameImu(0.f, sinf(a), cosf(a), r, 0.f, 0.f); }
    for (int f = 0; f < 60; f++) { float a = 30.f * D2R * sinf(f / 60.f * 3.14159f); float r = (f < 30 ? 30.f : -30.f) * 3.14159f / 2.f;
      frameImu(sinf(a), 0.f, cosf(a), 0.f, r, 0.f); }
    check(fabsf(currentYaw - y0) < 1.5f * D2R, "pitching and rolling the unit: the view doesn't swing");
    // tipped forward 30 deg and held tilted 20 deg sideways, turn 90 deg about true vertical
    initControls(); frameImu(0.f, 0.f, 1.f, 0.f, 0.f, 0.f);
    float up[3] = {sinf(20.f * D2R) * cosf(30.f * D2R), sinf(30.f * D2R), cosf(20.f * D2R) * cosf(30.f * D2R)};
    for (int f = 0; f < 45; f++) frameImu(up[0], up[1], up[2], 0.f, 0.f, 0.f);     // settle into the pose
    float yS = currentYaw;
    for (int f = 0; f < 60; f++) frameImu(up[0], up[1], up[2], 45.f * up[0], 45.f * up[1], 45.f * up[2]);
    for (int f = 0; f < 10; f++) frameImu(up[0], up[1], up[2], 0.f, 0.f, 0.f);
    float turned = fabsf(currentYaw - yS) / D2R;
    printf("  (turned %.1f deg for a 90 deg turn while tilted)\n", turned);
    check(fabsf(turned - 90.f) < 3.f, "turning while tipped and tilted: 1:1 about true vertical");
    // a gyro with 1.2 deg/s of bias, unit set down: after it learns, the view holds still
    initControls(); frameImu(0.f, 0.f, 1.f, 0.f, 0.f, 0.f);
    for (int f = 0; f < 30 * 10; f++) frameImu(0.f, 0.f, 1.f, 0.3f, -0.4f, 1.2f);
    float yb = currentYaw;
    for (int f = 0; f < 30 * 60; f++) frameImu(0.f, 0.f, 1.f, 0.3f, -0.4f, 1.2f);
    float drift = (currentYaw - yb) / D2R;
    printf("  (drift over a minute with a 1.2 deg/s biased gyro: %.2f deg)\n", drift);
    check(fabsf(drift) < 1.f, "biased gyro, unit still: the view holds");
    // held in a trembling hand (noise + 0.3 deg/s residual bias): the view doesn't wander
    float yh = currentYaw;
    for (int f = 0; f < 30 * 60; f++) {
      float n1 = ((rand() % 2001) - 1000) / 1000.f, n2 = ((rand() % 2001) - 1000) / 1000.f, n3 = ((rand() % 2001) - 1000) / 1000.f;
      frameImu(0.01f * n1, 0.12f + 0.01f * n2, 0.99f, 0.3f + 2.5f * n1, -0.4f + 2.5f * n2, 1.5f + 2.5f * n3);
    }
    float wander = (currentYaw - yh) / D2R;
    printf("  (wander over a minute in a trembling hand: %.2f deg)\n", wander);
    check(fabsf(wander) < 6.f, "trembling hand: the view doesn't wander off");
    // a slow, deliberate pan by hand (3 deg/s for 30 s) is a turn, not bias
    initControls(); frameImu(0.f, 0.f, 1.f, 0.f, 0.f, 0.f);
    float ys = currentYaw;
    for (int f = 0; f < 30 * 30; f++) {                // by hand: a little tremor rides on the pan
      float n1 = ((rand() % 2001) - 1000) / 1000.f, n2 = ((rand() % 2001) - 1000) / 1000.f;
      frameImu(0.f, 0.f, 1.f, 1.2f * n1, 1.2f * n2, 3.f + 1.2f * n1 * n2);
    }
    for (int f = 0; f < 15; f++) frameImu(0.f, 0.f, 1.f, 0.f, 0.f, 0.f);
    float slow = fabsf(currentYaw - ys) / D2R;
    printf("  (slow pan: %.1f deg of 90)\n", slow);
    check(fabsf(slow - 90.f) < 4.f, "a slow deliberate pan is honoured");
    initControls(); frame(0.f, 0.f);
  }

  // 6. the journey: hold an easterly heading (steering a little as you go) and the garden comes
  {
    journeyInit(1234u + seed, 0.f);
    float D = 0.f; int seen[B_ALL_N] = {0}; int order[12]; int no = 0; uint8_t last = 255;
    for (int st = 0; st < 60000 && no < 12; st++) {
      float yaw = 0.25f * sinf(st * 0.01f);                     // weaving around trees, still heading east
      journeyUpdate(D, yaw, 0.5f); D += 0.5f;
      uint8_t b = biomeIdAt(D);
      if (b != last) { order[no++] = b; seen[b]++; last = b; }
      if (b == B_JAPAN) break;
    }
    printf("  (biomes walked east:");
    for (int i = 0; i < no; i++) printf(" %d", order[i]);
    printf(")\n");
    check(seen[B_JAPAN] > 0 && no <= 5, "heading east through three biomes: the Japanese garden");
    {   // every direction with a special biome brings it
      const int OCT[7] = {0, 2, 3, 4, 5, 6, 7};
      const uint8_t SPEC[7] = {B_JAPAN, B_TUNDRA, B_REDWOODS, B_COAST, B_CANYON, B_RAINFOREST, B_KARST};
      bool all = true;
      for (int k = 0; k < 7; k++) {
        journeyInit(777u + seed * 31u + k, 0.f);
        float Dk = 0.f; bool got = false; int nk = 0; uint8_t lk = 255;
        for (int st = 0; st < 60000 && !got && nk < 12; st++) {
          float yaw = OCT[k] * 0.7853982f + 0.25f * sinf(st * 0.01f);
          journeyUpdate(Dk, yaw, 0.5f); Dk += 0.5f;
          uint8_t b = biomeIdAt(Dk);
          if (b != lk) { nk++; lk = b; }
          if (b == SPEC[k]) got = true;
        }
        if (!got || nk > 5) { all = false; printf("  (octant %d did not reach its special)\n", OCT[k]); }
      }
      check(all, "all seven special directions arrive after three biomes");
    }
    {   // and southwest brings the canyon
      journeyInit(4321u + seed, 0.f);
      float D2 = 0.f; bool canyon = false; int n2 = 0; uint8_t l2 = 255;
      for (int st = 0; st < 60000 && !canyon && n2 < 12; st++) {
        float yaw = -2.356f + 0.25f * sinf(st * 0.01f);
        journeyUpdate(D2, yaw, 0.5f); D2 += 0.5f;
        uint8_t b = biomeIdAt(D2);
        if (b != l2) { n2++; l2 = b; }
        if (b == B_CANYON) canyon = true;
      }
      check(canyon && n2 <= 5, "heading southwest through three biomes: the red rock canyon");
      // and the canyon is dry: no lake anywhere in it
      bool wet = false;
      for (float d = D2 + 40.f; d < D2 + 640.f; d += 5.f) for (int k = 0; k < 16; k++) if (lakeField(k * 0.3927f, d) > LAKE_T || inStream(k * 0.3927f, d)) wet = true;
      check(!wet, "the canyon is dry: no lakes, no creeks");
    }
    // and turning away resets it: walking a zig-zag never reaches it
    journeyInit(99u + seed, 0.f);
    D = 0.f; bool special = false;
    for (int st = 0; st < 40000; st++) {
      float yaw = ((st / 2500) & 1) ? 1.6f : -1.6f;            // north, then south, then north...
      journeyUpdate(D, yaw, 0.5f); D += 0.5f;
      if (biomeIdAt(D) >= B_ORDINARY_N) special = true;
    }
    check(!special, "wandering with no steady heading: no special biome");
    journeyInit(worldSeed(), totalDistance);
  }

  // 7. soak
  int frames = (int)(minutes * 60.f * 30.f);
  float worst = 1e9f, wander = 0.f, stuck = 0.f;
  long inside = 0, wet = 0, nan = 0, stops = 0;
  bool prevB = false;
  for (int f = 0; f < frames; f++) {
    float b = totalDistance;
    if ((f % 90) == 0) wander = ((rand() % 200) - 100) * 0.4f;
    frame(0.3f, stuck > 0.6f ? 80.f : wander);
    float moved = totalDistance - b;
    stuck = moved < 0.01f ? stuck + 1.f / 30.f : 0.f;
    if (wasBumped && !prevB) stops++;
    prevB = wasBumped;
    if (std::isnan(totalDistance) || std::isnan(currentYaw)) nan++;
    const FieldObj* o = fieldObjects();
    for (int i = 0; i < FIELD_N; i++) {
      if (!o[i].solid || o[i].wet) continue;
      float d = sqrtf(o[i].x * o[i].x + o[i].y * o[i].y) - o[i].radius - BODY_R;
      if (d < worst) worst = d;
      if (d < -0.02f) inside++;
    }
    for (float z = 0.3f; z < 2.f; z += 0.4f) if (moved > 0.f && lakeField(currentYaw, totalDistance + z) > LAKE_T) { wet++; break; }
  }
  check(inside == 0, "soak: never inside a trunk / rock / cliff");
  check(wet == 0, "soak: never steps into a lake");
  check(nan == 0, "soak: no NaN");
  int kinds[8] = {0};
  const FieldObj* o = fieldObjects();
  for (int i = 0; i < FIELD_N; i++) kinds[o[i].type]++;
  printf("  info: blind walker covered %.0f m, %ld stops, closest %.3f m | trees %d bushes %d rocks %d cliffs %d | audio %.1f%% of a host core\n",
         totalDistance, stops, worst, kinds[OBJ_PINE] + kinds[OBJ_BROAD], kinds[OBJ_BUSH], kinds[OBJ_BOULDER], kinds[OBJ_OUTCROP],
         100.0 * g_audioUs / (frames * 33333.0 + 1e-9));
  printf("%s\n", fails ? "FAIL" : "PASS");
  return fails ? 1 : 0;
}
