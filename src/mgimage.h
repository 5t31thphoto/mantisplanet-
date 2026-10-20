#pragma once
#include <stdint.h>
#include <stddef.h>

// The locked .mg image format (see docs/MG_FORMAT.md): "MG01", 320 x 240 (uint16 LE), raw RGB565 LE.
namespace mantis {

constexpr int MG_W = 320, MG_H = 240;
constexpr size_t MG_HEADER = 8, MG_BYTES = MG_HEADER + (size_t)MG_W * MG_H * 2;   // 153,608

void mgHeader(uint8_t out[8]);
inline uint16_t mgPixel(uint8_t r, uint8_t g, uint8_t b) { return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)); }
// One 320-pixel row (640 bytes) from the 160 x 120 palette framebuffer, doubled exactly as on screen.
void mgRowFromFrame(uint8_t out[MG_W * 2], int y, const uint8_t* fb160, const uint8_t* paletteRGB);

}
