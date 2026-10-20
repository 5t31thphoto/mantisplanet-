#!/bin/sh
# static RAM budget first: the ESP32 linker overflows if big buffers are static
"$(dirname "$0")/ramcheck.sh" || exit 1
# fishing: the fight's balance per tier and play style (fast), then the flow end to end
g++ -std=gnu++11 -O2 -DHOST -I"$(dirname "$0")/stubs" -I"$(dirname "$0")/../../src" "$(dirname "$0")/fishsim.cpp" "$(dirname "$0")/../../src/fishing.cpp" "$(dirname "$0")/../../src/fishgen.cpp" "$(dirname "$0")/../../src/bugs.cpp" "$(dirname "$0")/../../src/universe.cpp" "$(dirname "$0")/../../src/journey.cpp" -o /tmp/fishsim && /tmp/fishsim || exit 1
# the locked .mg image format, byte for byte
g++ -std=gnu++11 -O2 "$(dirname "$0")/mgtest.cpp" "$(dirname "$0")/../../src/mgimage.cpp" -o /tmp/mgtest && /tmp/mgtest || exit 1
# Build the firmware on a desktop compiler against tools/host/stubs and soak the walking logic.
# No ESP32 needed.
set -e
cd "$(dirname "$0")"
S=../../src
SRCS="$S/sim.cpp $S/haptics.cpp $S/universe.cpp $S/field.cpp $S/scene.cpp $S/journey.cpp $S/fishing.cpp $S/fishgen.cpp $S/ui.cpp $S/storage.cpp $S/trophy.cpp $S/wall.cpp $S/bugs.cpp $S/mgimage.cpp $S/render.cpp $S/display.cpp $S/dsp.cpp $S/audio.cpp $S/nature.cpp $S/music.cpp"
g++ -std=gnu++11 -O2 -DHOST -Wall -Wno-unused-function -Istubs -I$S walktest.cpp $SRCS -o walktest
for seed in 1 2 3 4 5 6; do ./walktest $seed ${1:-2}; done
rm -f walktest
# fishing end to end (cast, bite, strike, fight, land, miss, turn away, no water)
g++ -std=gnu++11 -O2 -DHOST -Istubs -I$S -I. fishtest.cpp $SRCS -o /tmp/fishtest
rm -rf sdcard && /tmp/fishtest
# the catch wall: logged catches drawn from the log, paging, opening a trophy, read back at boot
g++ -std=gnu++11 -O2 -DHOST -Istubs -I$S -I. walltest.cpp $SRCS -o /tmp/walltest
rm -rf sdcard && /tmp/walltest && rm -rf sdcard
# frame-rate caches must be exact: the same moment cold and warm, every pixel identical
g++ -std=gnu++11 -O2 -DHOST -Istubs -I$S -I. cachetest.cpp $SRCS -o /tmp/cachetest
/tmp/cachetest
# quality soaks: placement in all 17 biomes, way-finding in all 8 directions
g++ -std=gnu++11 -O2 -DHOST -Istubs -I$S -I. soak.cpp $SRCS -o /tmp/soak
/tmp/soak
