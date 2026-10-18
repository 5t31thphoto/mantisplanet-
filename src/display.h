#pragma once
namespace mantis {
void displayInit();
// Expand the 160x120 indexed frame through the palette into a 320x240 canvas and hand it to
// the core-0 push task; rendering of the next frame overlaps the LCD transfer.
void displayPresent();
}
