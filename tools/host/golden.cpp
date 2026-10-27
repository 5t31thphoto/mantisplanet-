// Showcase: scripted walks through the real firmware loop at different times, weathers and moments.
// Frames -> snaps/*.ppm (render.py makes PNGs + a sheet); with "audio" also writes snaps/*.wav.
#include "harness_core.inc"
using namespace mantis;

// Find a spot (distance walked, heading) whose surroundings match: 0 forest, 1 lake ahead, 2 open meadow.
static void findSpot(int want, float& outD, float& outA) {
  float bestScore = -1e9f;
  for (int di = 0; di < 90; di++) {
    float D = 120.f + di * 37.f;
    for (int k = 0; k < 24; k++) {
      float a = k * kTau / 24.f;
      float wetNear = 0.f, lake = 0.f, forest = 0.f;
      for (float z = 0.f; z < 4.f; z += 0.5f) wetNear = fmaxf(wetNear, lakeField(a, D + z));
      if (wetNear > LAKE_T - 0.04f) continue;
      for (float z = 5.f; z < 16.f; z += 1.f) lake = fmaxf(lake, lakeField(a, D + z));
      for (int j = -2; j <= 2; j++) for (float r = 3.f; r < 20.f; r += 5.f) forest += forestField(a + j * 0.5f, D + r);
      forest /= 20.f;
      float score = want == 0 ? forest * 3.f - (lake > LAKE_T ? 1.f : 0.f)
                  : want == 1 ? (lake > LAKE_T ? 2.f : lake) - forest
                  : 1.f - forest * 2.f - (lake > LAKE_T ? 1.f : 0.f);
      if (score > bestScore) { bestScore = score; outD = D; outA = a; }
    }
  }
}

static void begin(float tod, int biome, float yawOffset = 0.f) {
  g_force = WeatherForce();
  g_force.cloud = 0.3f; g_force.rain = 0.f; g_force.storm = 0.f; g_force.fog = 0.1f; g_force.wet = 0.f;
  g_force.rainbow = 0.f; g_force.murmur = 0.f; g_force.fireflies = 0.f; g_force.aurora = 0.f; g_force.meteors = 0.f;
  g_scene.wind = 0.3f;
  static float spotD[3] = {-1, -1, -1}, spotA[3];
  if (spotD[biome] < 0) findSpot(biome, spotD[biome], spotA[biome]);
  totalDistance = spotD[biome];
  initControls();
  frame(0.f, 0.f);                                  // level = standing
  currentYaw = spotA[biome] + yawOffset;
  setYaw(currentYaw);
  setTimeOfDay(tod, millis() / 1000.f, totalDistance);
  fieldInit(currentYaw, totalDistance);
  FieldObj* o = const_cast<FieldObj*>(fieldObjects());
  for (int i = 0; i < FIELD_N; i++) {
    float d = sqrtf(o[i].x * o[i].x + o[i].y * o[i].y);
    float rel = fabsf(atan2f(sinf(atan2f(o[i].y, o[i].x) - currentYaw), cosf(atan2f(o[i].y, o[i].x) - currentYaw)));
    if (d < 3.f || (rel < 0.25f && d < 6.f)) o[i].type = OBJ_NONE, o[i].solid = false;
  }
  wasBumped = false;
}
static float sunAzNow() { return g_scene.sunAz; }
static void face(float rel) { frame(0.f, 0.f); float yaw = sunAzNow() + rel; currentYaw = yaw; setYaw(yaw); fieldInit(currentYaw, totalDistance); }
static void faceAbs(float yaw) { currentYaw = yaw; setYaw(yaw); fieldInit(currentYaw, totalDistance); }
static void run(int frames, float tilt, float turn = 0.f) { for (int i = 0; i < frames; i++) frame(tilt, turn); }
static void shot(const char* name) {
  char p[64];
  snprintf(p, sizeof p, "snaps/%s.ppm", name);
  writePNGish(p);
  const Scene& s = g_scene;
  printf("%-22s tod %.2f cloud %.2f rain %.2f storm %.2f fog %.2f forest %.2f lake %.2f awe %.2f groove %.2f\n",
         name, s.tod, s.cloud, s.rain, s.storm, s.fog, s.forestNear, s.lakeNear, s.awe, audioGroove());
}
static void walk(int frames, float tilt, float drift) {
  float stuck = 0.f, turning = 0.f;
  for (int i = 0; i < frames; i++) {
    if (getenv("WALKDBG") && i % 300 == 0) printf("    walk %d d %.1f spd %.2f groove %.2f walkFor %.1f\n", i, totalDistance, currentSpeed, audioGroove(), g_scene.walkFor);
    float b = totalDistance;
    if (turning > 0.f) { frame(0.f, 85.f); turning -= 1.f / 30.f; continue; }
    frame(tilt, drift);
    stuck = totalDistance - b < 0.005f ? stuck + 1.f / 30.f : 0.f;
    if (stuck > 0.5f) { turning = 0.6f + (rand() % 100) * 0.006f; stuck = 0.f; }
  }
}
static void wavStart() { g_wav.clear(); g_recordAudio = true; g_maxGroove = 0.f; }
static void wavEnd(const char* name) {
  char p[64];
  snprintf(p, sizeof p, "snaps/%s.wav", name);
  writeWav(p);
  g_recordAudio = false;
  int pk = 0; double rms = 0; long clip = 0;
  for (int16_t v : g_wav) { int a = v < 0 ? -v : v; if (a > pk) pk = a; rms += (double)v * v; if (a > 32000) clip++; }
  printf("  audio %-18s groove max %.2f walked to %.0f  ", name, g_maxGroove, totalDistance);
  printf("%.1fs peak %.2f rms %.3f clipped %ld\n", g_wav.size() / 24000.0, pk / 32768.0, sqrt(rms / (g_wav.size() + 1)) / 32768.0, clip);
}

int main_snap(int argc, char** argv) {
  bool audio = argc > 1 && !strcmp(argv[1], "audio");
  setup();
  run(240, 0.f);                                                // let the intro mist clear

  // golden hour in the forest: crepuscular shafts between the trunks
  begin(0.69f, 0, 0.f); g_force.cloud = 0.35f; g_force.fog = 0.3f; face(0.25f);
  run(60, 0.2f); shot("01_golden_forest");
  // misty dawn forest
  begin(0.265f, 0, 0.f); g_force.cloud = 0.3f; g_force.fog = 0.75f; face(-0.3f);
  run(60, 0.2f); shot("02_dawn_mist_forest");
  // sunset over the lake
  begin(0.715f, 1); g_force.cloud = 0.45f; face(0.1f);
  run(40, 0.15f); shot("03_sunset_lake");
  // rain on the lake
  begin(0.45f, 1); g_force.cloud = 0.95f; g_force.rain = 0.85f; g_force.wind = 0.6f;
  run(200, 0.15f); shot("04_rain_lake");
  // storm with lightning
  begin(0.55f, 2); g_force.cloud = 1.f; g_force.rain = 1.f; g_force.storm = 1.f; g_force.wind = 0.9f;
  run(200, 0.f);
  for (int i = 0; i < 3000 && !(g_scene.boltAge < 0.06f && fabsf(atan2f(sinf(g_scene.boltAz - currentYaw), cosf(g_scene.boltAz - currentYaw))) < 0.45f); i++) frame(0.f, 0.f);
  shot("05_storm_bolt");
  // after the rain: rainbow (face away from the low sun)
  begin(0.66f, 2); g_force.cloud = 0.55f; g_force.rain = 0.f; g_force.wet = 0.8f; g_force.rainbow = 1.f;
  face(3.14159f);
  run(160, 0.f); shot("06_rainbow");
  // dusk murmuration over the meadow
  begin(0.745f, 2); g_force.cloud = 0.2f; g_force.murmur = 1.f;
  face(0.f);
  run(100, 0.f); shot("07_murmuration");
  // fireflies at the forest edge
  begin(0.8f, 0); g_force.cloud = 0.2f; g_force.fireflies = 1.f;
  run(150, 0.f);
  for (int i = 0; i < 90; i++) { frame(0.f, 0.f); float f = audioBeats() * 0.5f - 0.32f; if (f - floorf(f) < 0.05f) break; }
  shot("08_fireflies");
  // aurora night
  begin(0.95f, 1); g_force.cloud = 0.1f; g_force.aurora = 1.f; g_force.meteors = 1.f;
  faceAbs(AZ_NORTH);                                 // the aurora is in the north
  run(120, 0.f); shot("09_aurora");
  // noon meadow, wind in the trees
  begin(0.48f, 2); g_force.cloud = 0.4f; g_force.wind = 0.7f;
  run(60, 0.2f); shot("10_noon_meadow");

  if (!audio) return 0;
  // ---------------- listening previews ----------------
  // 1. dawn: the chorus, walking from meadow into forest, the groove assembling
  begin(0.22f, 2); g_force.cloud = 0.25f; g_force.fog = 0.4f;
  wavStart(); walk(30 * 50, 0.3f, 4.f); wavEnd("audio_dawn_walk");
  // 2. golden hour by the lake, walking long enough for the full groove
  begin(0.64f, 2); g_force.cloud = 0.3f;
  walk(30 * 40, 0.32f, 3.f);
  wavStart(); walk(30 * 60, 0.32f, 3.f); wavEnd("audio_golden_groove");
  // 3. rain becoming a storm
  begin(0.5f, 0); g_force.cloud = 1.f; g_force.rain = 0.6f; g_force.wind = 0.5f;
  wavStart(); run(30 * 15, 0.2f); g_force.rain = 1.f; g_force.storm = 1.f; g_force.wind = 0.9f; run(30 * 35, 0.f); wavEnd("audio_storm");
  // 4. the storm passes: rainbow, bells, choir
  begin(0.64f, 2); g_force.cloud = 0.5f; g_force.rain = 0.f; g_force.wet = 0.8f;
  wavStart(); run(30 * 6, 0.f); g_force.rainbow = 1.f; run(30 * 34, 0.f); wavEnd("audio_rainbow");
  // 5. night by the water, standing still: the crickets come back, frogs, a loon
  begin(0.92f, 1); g_force.cloud = 0.2f; g_force.fireflies = 1.f;
  wavStart(); run(30 * 8, 0.25f); run(30 * 42, 0.f); wavEnd("audio_night_still");
  return 0;
}
// The golden-frame test: optimizations must not change a single pixel.
//   golden record   -> writes golden.txt (fingerprints of ~60 scenes: framebuffer + palette, and the full screen
//                      for overlay scenes)
//   golden          -> re-renders and compares; any difference fails and dumps the frames to /tmp/golden_*
#include "../../src/journey.h"
#include "../../src/fishing.h"
#include "../../src/trophy.h"
#include "../../src/wall.h"
#include "../../src/jar.h"
#include "../../src/bugs.h"
#include "../../src/display.h"
#include <string>
#include <vector>
static uint64_t fnv(const uint8_t* p, size_t n, uint64_t h = 1469598103934665603ULL) { for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ULL; } return h; }
static std::vector<std::pair<std::string, uint64_t>> g_out;
static int g_pipeBad = 0, g_pipeN = 0;
static void fingerprint(const std::string& name, bool screen) {
  uint64_t h = fnv(frameBuffer(), 160 * 120);
  h = fnv(framePaletteRGB(), 256 * 3, h);
  if (screen) { uint16_t row[320]; for (int y = 0; y < 240; y++) { compositeRow(y, row); h = fnv((const uint8_t*)row, sizeof row, h); } }
  g_out.push_back({name, h});
  {   // the two display pipelines must send the screen exactly the same bytes
    static std::vector<uint16_t> a(320 * 240), b(320 * 240);
    displayDebugLegacy(a.data()); displayDebugStream(b.data());
    if (memcmp(a.data(), b.data(), a.size() * 2) != 0) { g_pipeBad++; printf("  %-12s: the streaming pipeline sends different pixels\n", name.c_str()); }
    g_pipeN++;
  }
  char p[128]; snprintf(p, sizeof p, "/tmp/golden_%s.ppm", name.c_str()); writePNGish(p);
}
static void place(uint8_t biome, float tod, float yawRel, float cloud, float rain, float storm, float fog) {
  float D = 70000.f + biome * 7000.f;
  totalDistance = D; frame(0, 0);
  for (float d = D - 3000.f; d < D + 3000.f; d += 200.f) journeyForce(d, biome);
  g_force = WeatherForce(); g_force.cloud = cloud; g_force.rain = rain; g_force.storm = storm; g_force.fog = fog;
  setTimeOfDay(tod, millis() / 1000.f, totalDistance); frame(0, 0);
  float yaw = g_scene.sunAz + yawRel; setYaw(yaw); currentYaw = yaw; fieldInit(yaw, totalDistance);
  for (int i = 0; i < 40; i++) frame(0, 0);
}
int main(int argc, char** argv) {
  bool record = argc > 1 && std::string(argv[1]) == "record";
  setup(); for (int i = 0; i < 120; i++) frame(0, 0);
  const float TODS[3] = {0.27f, 0.49f, 0.93f};
  const char* TN[3] = {"dawn", "noon", "night"};
  for (int b = 0; b < 17; b++) for (int t = 0; t < 3; t++) {
    place((uint8_t)b, TODS[t], 0.5f + b * 0.3f, 0.35f, 0.f, 0.f, -1.f);
    char n[48]; snprintf(n, sizeof n, "b%02d_%s", b, TN[t]); fingerprint(n, false);
  }
  place(B_FOREST, 0.60f, 0.8f, 0.8f, 0.7f, 0.f, -1.f); fingerprint("rain", false);
  place(B_MEADOW, 0.55f, 1.6f, 0.95f, 0.9f, 0.8f, -1.f); fingerprint("storm", false);
  place(B_LAKES, 0.30f, 0.4f, 0.4f, 0.f, 0.f, 0.8f); fingerprint("fog", false);
  place(B_TAIGA, 0.45f, 0.9f, 0.8f, 0.7f, 0.f, -1.f); fingerprint("snowfall", false);
  place(B_FOREST, 0.95f, 0.4f, 0.95f, 0.8f, 0.6f, 0.3f); renderSetFlashlight(true); for (int i = 0; i < 10; i++) frame(0, 0); fingerprint("flashlight", false); renderSetFlashlight(false);
  place(B_LAKES, 0.70f, 0.f, 0.3f, 0.f, 0.f, -1.f); fishingTestFight(1, 6.f, currentYaw, 3u); for (int i = 0; i < 12; i++) frame(0, 0); fingerprint("fishing", false);
  for (int i = 0; i < 400 && fishingActive(); i++) frame(0, 0);
  FishSpec f; fishMake(f, 4242u, WATER_LAKE, B_LAKES, 2); trophyBuild(f); overlayEnable(true); frame(0, 0); fingerprint("trophy", true); overlayEnable(false);
  for (int k = 0; k < BAIT_KINDS; k++) baitAdd((uint8_t)k, 1);
  jarOpen(); fingerprint("jar", true); jarClose(); frame(0, 0);
  // compare or record
  const char* path = "golden.txt";
  if (record) {
    FILE* o = fopen(path, "w"); for (auto& e : g_out) fprintf(o, "%s %016llx\n", e.first.c_str(), (unsigned long long)e.second); fclose(o);
    printf("golden: recorded %zu scenes\n", g_out.size()); return 0;
  }
  FILE* in = fopen(path, "r"); if (!in) { printf("golden: no golden.txt (run 'golden record' on the reference build)\nFAIL\n"); return 1; }
  char name[64]; unsigned long long h; int bad = 0, n = 0;
  while (fscanf(in, "%63s %llx", name, &h) == 2) {
    n++;
    for (auto& e : g_out) if (e.first == name && e.second != h) { printf("  %-12s differs (see /tmp/golden_%s.ppm)\n", name, name); bad++; }
  }
  fclose(in);
  printf("golden: %d of %d scenes pixel-identical\n", n - bad, n);
  printf("display: streaming and canvas pipelines identical on %d of %d scenes\n", g_pipeN - g_pipeBad, g_pipeN);
  printf("%s\n", bad || g_pipeBad ? "FAIL" : "PASS");
  return bad + g_pipeBad;
}
