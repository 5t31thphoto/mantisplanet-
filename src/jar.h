#pragma once
#include <stdint.h>
// The bug jar (tap B): the artist's jar on its shelf over a dimmed snapshot of where you're standing, with
// every bug you've caught living inside it: crawlers on the moss, hoppers hopping, worms wriggling, flyers
// fluttering, fireflies glowing. Tap a bug to put that kind on your hook; tap the lure for the plain lure.
// The header counts the kinds you've discovered, for collectors who never fish. Tap B again to close.
namespace mantis {
bool jarActive();
void jarOpen();
void jarClose();
void jarTap(int x, int y);        // screen coordinates (320 x 240)
void jarUpdate(float dt);         // animate and redraw (while open)
}
