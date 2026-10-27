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
void renderFps(int fps);
float renderFov();                        // radians across the screen
// The alignment drag: a diagnostic overlay (a green grid moving with the world away from a red grid left
void renderSetAlign(bool on, float dragSumRad);
void renderSetFlashlight(bool on);
// planet mods: the address of one base colour (see render.cpp); the number of time-of-day keys and each one's time
float* renderColourSlot(const char* group, int i, const char* field);
int renderSkyKeys();
float renderSkyKeyTime(int i);       // long-press B: a warm hand-light for dark nights and storms
bool renderFlashlight();                 // a tiny frame-rate readout in the corner

const uint8_t*  frameBuffer();       // FB_W*FB_H indices
const uint16_t* framePalette565();   // 256 entries, byte-swapped RGB565 (panel order)
const uint8_t*  framePaletteRGB();   // 256*3 RGB888 (for host previews)
int             frameBob();          // vertical walk bob in panel pixels

}
