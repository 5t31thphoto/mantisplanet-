#pragma once
#include <stdint.h>
// The catch wall (the right touch button): framed fish on a wooden wall, six to a page, redrawn from
// the catch log by seed so it opens instantly. Tap the left/right edge to page, tap a frame to see
// that catch's trophy shot from the card, tap again to come back. The right button closes it.
namespace mantis {
bool wallActive();
void wallOpen();
void wallClose();
void wallTap(int x, int y);       // screen coordinates (320 x 240)
}
