#!/bin/sh
# Build the live renderer (mpview) for Mantis Sprite Studio. Windows: run in the "MSYS2 MINGW64" shell
# (pacman -S --needed mingw-w64-x86_64-gcc); Linux/macOS: any g++. Run from tools/host.
set -e
cd "$(dirname "$0")"
S=../../src
SRCS="$S/sim.cpp $S/haptics.cpp $S/universe.cpp $S/field.cpp $S/scene.cpp $S/journey.cpp $S/fishing.cpp $S/fishgen.cpp $S/ui.cpp $S/storage.cpp $S/trophy.cpp $S/wall.cpp $S/bugs.cpp $S/jar.cpp $S/creatures.cpp $S/planet.cpp $S/mgimage.cpp $S/render.cpp $S/display.cpp $S/dsp.cpp $S/audio.cpp $S/nature.cpp $S/music.cpp"
OUT=mpview
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) OUT=mpview.exe; EXTRA="-static" ;; esac
g++ -std=gnu++11 -O2 -DHOST -Istubs -I$S -I. mpview.cpp $SRCS $EXTRA -o $OUT
echo "built $OUT"
