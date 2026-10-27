#pragma once
#include <stdint.h>
namespace mantis {
bool jarActive();
void jarOpen();
void jarClose();
void jarTap(int x, int y);        // screen coordinates (320 x 240)
void jarUpdate(float dt);         // animate and redraw (while open)
}
