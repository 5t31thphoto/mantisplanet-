// Byte-for-byte check of the locked .mg format.
#include "../../src/mgimage.h"
#include <stdio.h>
#include <string.h>
#include <vector>
using namespace mantis;
int main() {
  uint8_t h[8]; mgHeader(h);
  const uint8_t want[8] = {0x4D, 0x47, 0x30, 0x31, 0x40, 0x01, 0xF0, 0x00};
  bool ok = memcmp(h, want, 8) == 0;
  printf("header %02X %02X %02X %02X %02X %02X %02X %02X  %s\n", h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7], ok ? "ok" : "FAIL");
  // a test frame: palette index = x ^ y, palette = a ramp; encode the whole file and check size and a few pixels
  static uint8_t fb[160 * 120], pal[256 * 3];
  for (int i = 0; i < 256; i++) { pal[i * 3] = (uint8_t)i; pal[i * 3 + 1] = (uint8_t)(255 - i); pal[i * 3 + 2] = (uint8_t)(i * 7); }
  for (int y = 0; y < 120; y++) for (int x = 0; x < 160; x++) fb[y * 160 + x] = (uint8_t)(x ^ y);
  std::vector<uint8_t> file(h, h + 8);
  uint8_t row[MG_W * 2];
  for (int y = 0; y < MG_H; y++) { mgRowFromFrame(row, y, fb, pal); file.insert(file.end(), row, row + sizeof row); }
  bool sz = file.size() == 153608;
  printf("size %zu  %s\n", file.size(), sz ? "ok" : "FAIL");
  bool px = true;
  for (int t = 0; t < 1000; t++) {
    int x = (t * 37) % 320, y = (t * 53) % 240;
    uint8_t i = fb[(y / 2) * 160 + x / 2];
    uint16_t want = (uint16_t)(((pal[i * 3] & 0xF8) << 8) | ((pal[i * 3 + 1] & 0xFC) << 3) | (pal[i * 3 + 2] >> 3));
    size_t off = 8 + 2 * (size_t)(y * 320 + x);
    uint16_t got = (uint16_t)(file[off] | (file[off + 1] << 8));
    if (got != want) px = false;
  }
  printf("pixels at 8 + 2*(y*320 + x), RGB565 little-endian  %s\n", px ? "ok" : "FAIL");
  FILE* f = fopen("/tmp/test.mg", "wb"); fwrite(file.data(), 1, file.size(), f); fclose(f);
  return ok && sz && px ? 0 : 1;
}
