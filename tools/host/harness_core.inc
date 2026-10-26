// Shared host runtime: clock, M5 stub instance, then the real firmware main.
#include "M5Unified.h"
#include <cstdlib>
#include <cmath>
#include <chrono>
static uint64_t g_us = 1000000;
uint32_t millis() { return (uint32_t)(g_us / 1000); }
uint32_t micros() { return (uint32_t)g_us; }
void delay(uint32_t ms) { g_us += (uint64_t)ms * 1000; }
M5Class M5;
uint32_t esp_random() { static uint32_t x = 0x9E3779B9u; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return x; }
M5Canvas* g_hostLastCanvas = nullptr;
#include "main.cpp"
#include "../../src/field.h"
#include "../../src/universe.h"
#include "../../src/audio.h"
#include "../../src/scene.h"
#include "../../src/display.h"
#include <vector>
static std::vector<int16_t> g_wav;      // everything the speaker would have played
static bool g_recordAudio = false;
static double g_audioDebt = 0.0;
static double g_audioUs = 0.0;   // host time spent synthesizing
static float g_maxGroove = 0.f;

// One firmware frame at a fixed host frame time. tilt: accel.y offset (forward > 0),
// turn: gyro z in deg/s.
static float g_flickDps = 0.f, g_holdPitchDeg = -999.f;
static void frame(float tilt, float turnDps, uint32_t frameUs = 33333) {
  g_us += frameUs;
  // physical: held upright (gravity along y); 'tilt' > 0 tips the top away past the walk threshold.
  // A turn about true vertical shows up on whichever gyro axes gravity lies along.
  float th = tilt > 0.f ? (20.f + tilt * 70.f) * 0.0174533f : tilt * 60.f * 0.0174533f;   // < 0: tipped back toward you
  if (g_holdPitchDeg > -900.f) th = g_holdPitchDeg * 0.0174533f;                       // fishing tests hold an exact pitch
  M5.Imu.data.accel = {0.f, cosf(th), sinf(th)};
  M5.Imu.data.gyro = {g_flickDps, turnDps * cosf(th), turnDps * sinf(th)};
  g_flickDps = 0.f;
  float dBefore = totalDistance;
  loop();
  if (g_recordAudio && mantis::audioGroove() > g_maxGroove) g_maxGroove = mantis::audioGroove();
  (void)dBefore;
  // the audio task runs alongside on core 0: render exactly as many samples as this frame lasted
  g_audioDebt += frameUs * (mantis::AUDIO_SR / 1e6);
  int16_t blk[256];
  while (g_audioDebt >= 256.0) {
    auto a0 = std::chrono::high_resolution_clock::now();
    mantis::audioRender(blk, 256);
    g_audioUs += std::chrono::duration<double, std::micro>(std::chrono::high_resolution_clock::now() - a0).count();
    if (g_recordAudio) g_wav.insert(g_wav.end(), blk, blk + 256);
    g_audioDebt -= 256.0;
  }
}

static void writeWav(const char* path) {
  FILE* f = fopen(path, "wb");
  uint32_t n = (uint32_t)g_wav.size(), bytes = n * 2, sr = mantis::AUDIO_SR, br = sr * 2, sz = 36 + bytes;
  fwrite("RIFF", 1, 4, f); fwrite(&sz, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
  uint32_t fmtLen = 16; uint16_t pcm = 1, ch = 1, align = 2, bits = 16;
  fwrite(&fmtLen, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f);
  fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
  fwrite(g_wav.data(), 2, n, f); fclose(f);
}

// one frame with an explicit IMU reading (accel in g, gyro in deg/s)
static void frameImu(float ax, float ay, float az, float gx, float gy, float gz, uint32_t frameUs = 33333) {
  g_us += frameUs;
  M5.Imu.data.accel = {ax, ay, az};
  M5.Imu.data.gyro = {gx, gy, gz};
  loop();
  g_audioDebt += frameUs * (mantis::AUDIO_SR / 1e6);
  int16_t blk[256];
  while (g_audioDebt >= 256.0) { mantis::audioRender(blk, 256); g_audioDebt -= 256.0; }
}

static void writePNGish(const char* path) {   // PPM of exactly what the screen shows (frame, palette, overlay); tools/host/render.py turns these into PNGs
  FILE* f = fopen(path, "wb");
  if (!f) return;
  fprintf(f, "P6 320 240 255\n");
  uint16_t row[320];
  for (int y = 0; y < 240; y++) {
    mantis::compositeRow(y, row);
    for (int x = 0; x < 320; x++) { uint16_t v = row[x]; unsigned char c[3] = {(unsigned char)((v >> 11) << 3), (unsigned char)(((v >> 5) & 63) << 2), (unsigned char)((v & 31) << 3)}; fwrite(c, 1, 3, f); }
  }
  fclose(f);
}
