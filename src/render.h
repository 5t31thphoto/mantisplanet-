#pragma once
#include <stdint.h>

namespace mantis {

// Internal framebuffer: 160x120, 8-bit palette indices. Scaled 2x to the 320x240 panel.
constexpr int FB_W = 160;
constexpr int FB_H = 120;

void renderInit();
void renderFrame(float yaw, float distance, float speed, float timeSec);

// Draw the mute / sound-on icon over the finished frame (alpha 0..1 fades it out).
void renderMuteIcon(bool muted, float alpha);

const uint8_t*  frameBuffer();       // FB_W*FB_H indices
const uint16_t* framePalette565();   // 256 entries, byte-swapped RGB565 (panel order)
const uint8_t*  framePaletteRGB();   // 256*3 RGB888 (for host previews)
int             frameBob();          // vertical walk bob in panel pixels

}
