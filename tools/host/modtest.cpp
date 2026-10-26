// SD creature mods: valid files are taken, malformed ones skipped, the rest stays built-in.
#include "harness_core.inc"
#include "../../src/creatures.h"
#include <sys/stat.h>
using namespace mantis;
static void wr(const char* path, const std::vector<uint8_t>& b) { FILE* f = fopen(path, "wb"); fwrite(b.data(), 1, b.size(), f); fclose(f); }
int main() {
  mkdir("sdcard", 0755); mkdir("sdcard/mp-sprites", 0755); mkdir("sdcard/mp-sprites/deer", 0755);
  std::vector<uint8_t> ok = {'M', 'P', 'S', 30, 20, 2, 25, 4, 1, 10, 25, 4, 1, 10};   // 30 x 20, 2 frames; head and tail anchors
  for (int i = 0; i < 30 * 20 * 2; i++) ok.push_back((i % 30) > 3 && (i / 30 % 20) > 8 ? 146 : 0);
  wr("sdcard/mp-sprites/deer/body_walk_mid.mps", ok);
  std::vector<uint8_t> badMagic = ok; badMagic[0] = 'X'; wr("sdcard/mp-sprites/deer/body_graze_mid.mps", badMagic);
  std::vector<uint8_t> badColour = ok; badColour[200] = 7; wr("sdcard/mp-sprites/deer/body_alert_mid.mps", badColour);
  std::vector<uint8_t> shortFile(ok.begin(), ok.begin() + 300); wr("sdcard/mp-sprites/deer/body_bound_mid.mps", shortFile);
  setup(); for (int i = 0; i < 5; i++) frame(0, 0);                        // (setup loads mods after the card)
  int n = creaturesLoadMods();
  int fails = 0;
  auto check = [&](bool c, const char* w) { printf("  %-56s %s\n", w, c ? "ok" : "FAIL"); if (!c) fails++; };
  printf("creature mods\n");
  check(n == 1, "the one valid file is taken, the three broken ones skipped");
  check(deerFrames(SEX_DOE, TIER_MID, AN_WALK) == 2 && deerFrame(SEX_DOE, TIER_MID, AN_WALK, 1)->w == 30, "the modded walk has its two frames");
  check(deerFrames(SEX_DOE, TIER_MID, AN_GRAZE) == 1 && deerFrame(SEX_DOE, TIER_MID, AN_GRAZE, 0)->w == 24, "a skipped animation keeps its built-in pose");
  check(deerFrame(SEX_FAWN, TIER_MID, AN_WALK, 0)->w == 18, "fawns are built from the modded doe too");
  system("rm -rf sdcard");
  printf(fails ? "FAIL\n" : "PASS\n");
  return fails;
}
