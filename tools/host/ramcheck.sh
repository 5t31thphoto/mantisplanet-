#!/bin/sh
# Static RAM budget. The ESP32 links every static array into a small internal segment (dram0_0_seg); big
# buffers must be runtime allocations (src/mem.h). This compiles each module for the host and sums the
# static data it declares (array sizes are the same as on the ESP32; tables of pointers count a little
# heavier here). v26 overflowed the device at ~120 KB by this measure; it linked fine at ~70 KB.
cd "$(dirname "$0")"
S=../../src
T=$(mktemp -d)
for f in $S/*.cpp; do g++ -std=gnu++11 -O2 -DHOST -Istubs -include stubs/Arduino.h -I$S -c "$f" -o "$T/$(basename "$f" .cpp).o" 2>/dev/null || { echo "ramcheck: $f did not compile"; exit 1; }; done
TOTAL=$(nm -S -t d "$T"/*.o | awk '$3 ~ /[bBdD]/ {s+=$2} END {print s+0}')
BUDGET=82000
echo "static RAM: $TOTAL bytes (budget $BUDGET)"
nm -S --size-sort -t d "$T"/*.o | awk '$3 ~ /[bBdD]/ && $2+0 >= 4000 {printf "  %7d  %s\n", $2, $4}' | sort -rn
rm -rf "$T"
if [ "$TOTAL" -gt "$BUDGET" ]; then echo "FAIL: too much static RAM; allocate big buffers at runtime (src/mem.h)"; exit 1; fi
echo PASS
