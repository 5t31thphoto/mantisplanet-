// mpview: the live renderer for Mantis Sprite Studio. Renders a scene with a creature in it, using the art
// and planet colours in a given "card" folder, and writes the frames as PPM images.
//   mpview --sd DIR --biome meadow --tod 0.45 --look 1.4 --weather clear --creature deer --sex doe
//          --pose graze --dist 6 --frames 8 --fps 8 --out C:/tmp/frame
// Prints one line per frame written. Exit 0 on success.
#define main game_main_unused
#include "harness_core.inc"
#undef main
#include "../../src/journey.h"
#include "../../src/planet.h"
#include "../../src/creatures.h"
#include "../../src/bestiary_gen.h"
#include <string>
namespace mantis { void renderDebugCritter(uint8_t, float, float, bool, uint8_t, uint32_t, uint8_t, uint8_t, bool, bool); }
using namespace mantis;
static void setEnv(const char* k, const char* v) {
#ifdef _WIN32
  _putenv_s(k, v);
#else
  setenv(k, v, 1);
#endif
}
int main(int argc, char** argv) {
  std::string sd = "sdcard", biome = "meadow", weather = "clear", creature = "", sex = "doe", pose = "stand", out = "frame";
  float tod = 0.45f, look = 1.4f, dist = 6.f, fps = 8.f; int frames = 1;
  for (int i = 1; i + 1 < argc; i += 2) {
    std::string k = argv[i], v = argv[i + 1];
    if (k == "--sd") sd = v; else if (k == "--biome") biome = v; else if (k == "--tod") tod = (float)atof(v.c_str());
    else if (k == "--look") look = (float)atof(v.c_str()); else if (k == "--weather") weather = v; else if (k == "--creature") creature = v;
    else if (k == "--sex") sex = v; else if (k == "--pose") pose = v; else if (k == "--dist") dist = (float)atof(v.c_str());
    else if (k == "--frames") frames = atoi(v.c_str()); else if (k == "--fps") fps = (float)atof(v.c_str()); else if (k == "--out") out = v;
  }
  setEnv("MANTIS_SD", sd.c_str());
  int b = 0; for (int i = 0; i < 17; i++) if (biome == PLANET_BIOME_IDS[i]) b = i;
  setup(); for (int i = 0; i < 60; i++) frame(0, 0);
  float D = 70000.f + b * 7000.f; totalDistance = D; frame(0, 0);
  for (float d = D - 3000.f; d < D + 3000.f; d += 200.f) journeyForce(d, (uint8_t)b);
  g_force = WeatherForce();
  g_force.cloud = weather == "clear" ? 0.15f : weather == "cloudy" ? 0.75f : 0.95f;
  if (weather == "rain") g_force.rain = 0.6f;
  if (weather == "storm") { g_force.rain = 0.9f; g_force.storm = 0.8f; }
  if (weather == "fog") g_force.fog = 0.85f;
  if (weather == "snow") g_force.rain = 0.6f;
  setTimeOfDay(tod, millis() / 1000.f, totalDistance); frame(0, 0);
  float yaw = g_scene.sunAz + look; setYaw(yaw); currentYaw = yaw; fieldInit(yaw, totalDistance);
  for (int i = 0; i < 30; i++) frame(0, 0);
  // the creature
  uint8_t kind = 255, variant = 0; uint32_t seed = 2;
  struct DeerId { const char* id; uint8_t v; } DEER[] = {{"deer", 0}, {"sika", 2}, {"mule_deer", 3}, {"elk", 4}, {"caribou", 1}, {"caribou_herd", 5}, {"white_stag", 7}};
  for (auto& d : DEER) if (creature == d.id) { kind = CR_DEER; variant = d.v; }
  for (int i = 0; i < BESTIARY_N && kind == 255; i++) if (creature == BESTIARY[i].id) { kind = BESTIARY[i].kind; variant = BESTIARY[i].variant; seed = BESTIARY[i].seedParity == 1 ? 3 : 2; }
  uint8_t sx = sex == "buck" ? SEX_BUCK : sex == "fawn" ? SEX_FAWN : sex == "cow" ? SEX_COW : SEX_DOE;
  uint8_t mood = pose == "graze" ? DEER_GRAZE : pose == "stomp" ? DEER_STOMP : pose == "sniff" ? DEER_SNIFF : pose == "nurse" ? DEER_NURSE
               : pose == "walk" ? DEER_WALK : pose == "show" ? DEER_SHOW : DEER_ALERT;
  bool flee = pose == "bound" || pose == "away" || pose == "move", away = pose == "away";
  int step = fps > 0 ? (int)lroundf(30.f / fps) : 1; if (step < 1) step = 1;
  for (int f = 0; f < frames; f++) {
    for (int k = 0; k < step; k++) frame(0, 0);
    if (kind != 255) renderDebugCritter(kind, dist, 0.f, true, variant, seed, sx, mood, flee, away);
    char p[512]; snprintf(p, sizeof p, "%s_%03d.ppm", out.c_str(), f);
    writePNGish(p);
    printf("%s\n", p);
  }
  return 0;
}
