// exportdefaults: everything Mantis Sprite Studio starts from, taken from the real game:
//   palette.json (the 32 creature colours at noon), planet_default.json, sprites.json (today's art for every
//   creature, size and animation: palette indices; the deer as parts, with anchors), plates/*.ppm (a noon
//   background per biome) + plates.json. Run from tools/host:  ./exportdefaults ../sprites/defaults
#define main game_main_unused
#include "harness_core.inc"
#undef main
#include "../../src/journey.h"
#include "../../src/planet.h"
#include "../../src/creatures.h"
#include "../../src/bestiary_gen.h"
#include <string>
#include <sys/stat.h>
namespace mantis { void renderDebugCritter(uint8_t, float, float, bool, uint8_t, uint32_t, uint8_t, uint8_t, bool, bool); }
using namespace mantis;
static FILE* g_js; static bool g_first = true;
static void writeSprite(const char* id, const char* asset, const char* tier, int w, int h, int frames, const std::vector<int>& anchors, const std::vector<uint8_t>& px) {
  fprintf(g_js, "%s  {\"id\": \"%s\", \"asset\": \"%s\", \"tier\": \"%s\", \"w\": %d, \"h\": %d, \"frames\": %d, \"anchors\": [", g_first ? "" : ",\n", id, asset, tier, w, h, frames);
  for (size_t i = 0; i < anchors.size(); i++) fprintf(g_js, "%s%d", i ? ", " : "", anchors[i]);
  fprintf(g_js, "], \"px\": \"");
  for (uint8_t v : px) fprintf(g_js, "%02x", v);
  fprintf(g_js, "\"}");
  g_first = false;
}
int main(int argc, char** argv) {
  std::string dir = argc > 1 ? argv[1] : "../sprites/defaults";
  mkdir(dir.c_str(), 0755); mkdir((dir + "/plates").c_str(), 0755);
  setup(); for (int i = 0; i < 60; i++) frame(0, 0);
  planetWriteDefaults((dir + "/planet_default.json").c_str());
  // the creature palette at noon, in a meadow
  float D = 70000.f; totalDistance = D; frame(0, 0);
  for (float d = D - 3000.f; d < D + 3000.f; d += 200.f) journeyForce(d, B_MEADOW);
  g_force = WeatherForce(); g_force.cloud = 0.f;
  setTimeOfDay(0.47f, millis() / 1000.f, totalDistance); for (int i = 0; i < 20; i++) frame(0, 0);
  const uint8_t* pal = framePaletteRGB();
  static const char* const NAMES[32] = {"silhouette", "deer dark", "deer", "deer light", "rabbit", "rabbit light", "magenta", "pink", "blossom", "blue dark", "blue", "lavender", "yellow", "yellow light", "white", "night dark",
    "near black", "umber", "brown", "tawny", "fawn", "cream", "slate", "grey", "pale grey", "rust", "orange", "red", "deep green", "leaf green", "deep blue", "sky blue"};
  FILE* pf = fopen((dir + "/palette.json").c_str(), "w");
  fprintf(pf, "{\"about\": \"The 32 creature colours (game palette indices) as they look at noon. Art is snapped to these.\", \"colours\": [\n");
  for (int k = 0; k < 32; k++) { int idx = k < 16 ? 144 + k : 208 + k - 16; fprintf(pf, "  {\"index\": %d, \"name\": \"%s\", \"rgb\": [%d, %d, %d]}%s\n", idx, NAMES[k], pal[idx * 3], pal[idx * 3 + 1], pal[idx * 3 + 2], k < 31 ? "," : ""); }
  fprintf(pf, "]}\n"); fclose(pf);
  // today's art
  g_js = fopen((dir + "/sprites.json").c_str(), "w");
  fprintf(g_js, "{\"about\": \"Today's creature art (palette indices, hex; 00 = clear), as the studio's starting point.\", \"sprites\": [\n");
  static const char* const TN[3] = {"far", "mid", "near"};
  static const char* const AN[AN_N] = {"stand", "walk", "bound", "graze", "alert", "stomp", "sniff", "nurse", "away"};
  for (int t = 0; t < 3; t++) {
    for (int a = 0; a < AN_N; a++) {
      int nf = 0; const Spr* f0 = deerBodyFrame((uint8_t)t, (uint8_t)a, 0, &nf);
      if (!f0) continue;
      int k = f0->k;
      int w = f0->w * k, h = f0->h * k;
      std::vector<int> anc; std::vector<uint8_t> px;
      for (int f = 0; f < nf; f++) {
        const Spr* s = deerBodyFrame((uint8_t)t, (uint8_t)a, f, nullptr);
        anc.push_back(s->ax * k); anc.push_back(s->ay * k); anc.push_back(s->tx * k); anc.push_back(s->ty * k);
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) px.push_back(s->w * k == w ? s->px[(y / k) * s->w + x / k] : 0);
      }
      writeSprite("deer", (std::string("body_") + AN[a]).c_str(), TN[t], w, h, nf, anc, px);
    }
    static const char* const PN[8] = {"antlers", "antlers_away", "tail_down", "tail_flag", "tail_flick", "tail_away_down", "tail_away_flag", "tail_away_flick"};
    for (int p = 0; p < 8; p++) {
      const Spr* s = deerPart((uint8_t)t, p);
      if (!s) continue;
      int k = s->k; std::vector<int> anc = {s->ax * k, s->ay * k}; std::vector<uint8_t> px;
      for (int y = 0; y < s->h * k; y++) for (int x = 0; x < s->w * k; x++) px.push_back(s->px[(y / k) * s->w + x / k]);
      writeSprite("deer", PN[p], TN[t], s->w * k, s->h * k, 1, anc, px);
    }
  }
  // every other creature: drawn by the game at the distance that makes it each tier's size; what it drew, cropped
  static uint8_t clean[160 * 120]; memcpy(clean, frameBuffer(), sizeof clean);
  const float PXR = 160.f / renderFov();
  for (int i = 0; i < BESTIARY_N; i++) {
    const BestiaryEntry& e = BESTIARY[i];
    for (int t = 0; t < 3; t++) {
      if (!e.tierH[t]) continue;
      for (int a = 0; a < 2; a++) {
        if (!e.anim[a][0]) continue;
        float dist = e.heightM * PXR / e.tierH[t];
        if (dist < 1.3f) dist = 1.3f;
        memcpy((void*)frameBuffer(), clean, sizeof clean);
        renderDebugCritter(e.kind, dist, 0.f, true, e.variant, e.seedParity == 1 ? 3 : 2, 0, 0, a == 1, false);
        const uint8_t* fb = frameBuffer();
        int x0 = 999, y0 = 999, x1 = -1, y1 = -1;
        for (int y = 0; y < 120; y++) for (int x = 0; x < 160; x++) if (fb[y * 160 + x] != clean[y * 160 + x]) { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
        if (x1 < 0) continue;
        int w = x1 - x0 + 1, h = y1 - y0 + 1;
        std::vector<uint8_t> px;
        for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) { uint8_t v = fb[y * 160 + x]; px.push_back(v != clean[y * 160 + x] && ((v >= 144 && v < 160) || (v >= 208 && v < 224)) ? v : 0); }
        std::vector<int> anc = {w / 2, h};
        writeSprite(e.id, e.anim[a], TN[t], w, h, 1, anc, px);
      }
    }
  }
  fprintf(g_js, "\n]}\n"); fclose(g_js);
  // a noon plate per biome, and where the ground is at each distance
  FILE* mf = fopen((dir + "/plates.json").c_str(), "w");
  fprintf(mf, "{\"about\": \"Noon backgrounds (320 x 240) for previews. A creature at distance d stands at screen y = 2 * (horizon + zk / d), x = 160; game pixels are drawn 2 x 2.\", \"horizon\": 70, \"zk\": %.3f, \"plates\": [", 1.4f * PXR);
  for (int b = 0; b < 17; b++) {
    float Db = 70000.f + b * 7000.f; totalDistance = Db; frame(0, 0);
    for (float d = Db - 3000.f; d < Db + 3000.f; d += 200.f) journeyForce(d, (uint8_t)b);
    g_force = WeatherForce(); g_force.cloud = 0.3f;
    setTimeOfDay(0.47f, millis() / 1000.f, totalDistance); frame(0, 0);
    float yaw = g_scene.sunAz + 1.4f; setYaw(yaw); currentYaw = yaw; fieldInit(yaw, totalDistance);
    for (int i = 0; i < 30; i++) frame(0, 0);
    char p[512]; snprintf(p, sizeof p, "%s/plates/%s.ppm", dir.c_str(), PLANET_BIOME_IDS[b]); writePNGish(p);
    fprintf(mf, "%s\"%s\"", b ? ", " : "", PLANET_BIOME_IDS[b]);
  }
  fprintf(mf, "]}\n"); fclose(mf);
  printf("defaults written to %s\n", dir.c_str());
  return 0;
}
