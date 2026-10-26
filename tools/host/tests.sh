#!/bin/sh
# Look check: scripted walks at different times of day -> snaps/sheet.png
set -e
cd "$(dirname "$0")"
S=../../src
SRCS="$S/sim.cpp $S/haptics.cpp $S/universe.cpp $S/field.cpp $S/scene.cpp $S/journey.cpp $S/fishing.cpp $S/fishgen.cpp $S/ui.cpp $S/storage.cpp $S/trophy.cpp $S/wall.cpp $S/bugs.cpp $S/jar.cpp $S/mgimage.cpp $S/render.cpp $S/display.cpp $S/dsp.cpp $S/audio.cpp $S/nature.cpp $S/music.cpp"
g++ -std=gnu++11 -O2 -DHOST -Wall -Wno-unused-function -Istubs -I$S snap.cpp $SRCS -o snap
rm -rf snaps; mkdir -p snaps
./snap
python3 render.py
rm -f snap
