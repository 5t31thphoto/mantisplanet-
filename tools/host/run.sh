#!/bin/sh
# Build the firmware on a desktop compiler against tools/host/stubs and soak the walking logic.
# No ESP32 needed.
set -e
cd "$(dirname "$0")"
S=../../src
SRCS="$S/sim.cpp $S/haptics.cpp $S/universe.cpp $S/field.cpp $S/scene.cpp $S/render.cpp $S/display.cpp $S/dsp.cpp $S/audio.cpp $S/nature.cpp $S/music.cpp"
g++ -std=gnu++11 -O2 -DHOST -Wall -Wno-unused-function -Istubs -I$S walktest.cpp $SRCS -o walktest
for seed in 1 2 3 4 5 6; do ./walktest $seed ${1:-2}; done
rm -f walktest
