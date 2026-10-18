# MANTISPLANET — a dream you walk through

A pixel-art dream world for the **M5Stack Core2**. Hold it like a window: turn and the panorama wraps around you,
tip forward and you walk. The world is hallucinated in front of you as you go — biomes drift, the day turns,
god rays fan across the lake, the stars and a ringed giant come out — and nothing behind you is kept.

## Build & install

* **CI**: push this folder (or the zip) to the repo. `.github/workflows/ci.yml` unpacks a root zip over the tree,
  builds with PlatformIO (`pio run -e m5stack-core2`), merges `firmware-merged.bin` and deploys the web installer in `website/`.
* **Local**: `pio run -e m5stack-core2 -t upload`.
* **Browser**: open the GitHub Pages site in Chrome / Edge and click Install (esp-web-tools).

## Controls

| Input | Does |
|---|---|
| **Hold it level** | stand still — nothing ever walks for you |
| **Tip forward** | walk (further = faster); level it again to stop |
| **Turn** | look around — the screen is a window, the world stays put |
| **Long press the screen** | sound off / on (remembered across power-off); the motor keeps going either way |

Trees, boulders, cliff outcrops and lakes are solid. Walk into one head-on and you stop with a bump in your
hands; look around and find another way. Clip the edge of one and you slide past it.

### The window (`sim.cpp`)

No magnetometer, so the camera is built to be trustworthy anyway:

* **Yaw about true vertical.** A gravity vector is carried by the gyro (instant) and gently corrected by the
  accelerometer; yaw rate is the gyro projected onto it. Pitching or rolling the unit can't swing the view, however
  it's held, and no axis mapping is needed. 1:1 with your hands.
* **Bias.** Learned whenever the unit is genuinely still (no tremor: a table) and saved to flash for the next
  session; in the hand, where tremor averages out but bias doesn't, a slow tracker folds the average out along the
  vertical axis. A soft zero-rate band ignores residual creep under ~0.3°/s. Deliberate slow pans pass.
* **Latency.** Direct integration and ~35 ms of prediction to cover render + LCD.
* **Pivot.** The eye sits 0.4 m in front of your body's turning axis, so turning sweeps it along an arc and near
  trunks slide across far ones — the parallax that makes the glass feel like a window. Collision stays with you.

If turning pans the wrong way on your board, flip `YAW_SIGN` in `src/sim.cpp`.

## Sound

Everything is synthesized live at 24 kHz on core 0 and streamed to the speaker (three rotating buffers through
M5Unified's two-slot channel queue). The Core2 shares one I2S port between mic and speaker, so the mic stays off.

* **Nature** (`nature.cpp`): wind and gusts (a whistle in storms), leaf rustle that follows forest density, lake
  lapping and bubbles, six bird species whose individuals repeat their own motifs and answer each other (dawn
  chorus; a loon over water at dusk; owls at night), crickets that hush as you pass and return when you're still,
  cicadas at noon, frogs and peepers, rain on leaves vs open ground, thunder delayed by the bolt's distance.
  Footsteps follow your pace (about 1.3 steps/s slow, 2.2 brisk; none when you stop), a soft heel-and-roll per surface: grass, forest litter, stone, mud, wet grass.
* **Score** (`music.cpp`): a conductor reads the scene and weaves a minimalist dub: drone, pad, dub chord stabs into
  a tape echo, shaker, rolling bass, Karplus-Strong plucks, singing bowls and a formant choir, in a mode for dawn,
  day, golden hour, night and storm. The forest is the main act: music comes in tides — long nature-only stretches, a
  passage with at most two voices on stage, then it dissolves back. A long steady walk builds it; standing still
  sends it out. Moments of awe bring bells and choir.
* **The motor is the bass** (`haptics.cpp`): the speaker can't move air below ~200 Hz, so the low end is felt —
  the kick on the beat, footsteps shaped per surface, bumps, thunder rolls (a crack when it's close), a deer's
  hooves, rain on your hands, a slow swell for a moment of awe. Max-mixed, remapped over the motor's dead zone,
  written only when the level changes (Synapse pattern).

## Weather and moments

Weather drifts with distance and time: clear, cloudy, mist, rain, storm (lightning, distance-delayed thunder),
wet ground afterwards, wind that sways the trees. Moments: crepuscular rays (sun-to-pixel transmittance through
canopies and clouds, with dust motes), a rainbow after rain mirrored in the lake, a starling murmuration at dusk,
fireflies flashing together on the beat, meteor showers and aurora nights. The day takes ~10 minutes, so a sunset
unfolds while you stand and watch it.

## How the dream works

Two worlds, stitched at the treeline:

* **The panorama** (`universe.*`, `render.cpp`): sky, clouds, three mountain ranges, hill tree lines, lakes and ground
  are sampled from noise that is periodic in *direction* and runs along *distance walked*. Spin in place and you see a
  closed 360° ring; walk and the far world slowly morphs. Lakes live here, so they come toward you from wherever you face.
* **The near field** (`field.*`): everything you can walk up to — pines, broadleaf trees, bushes, flowers, tufts,
  boulders, cliff outcrops and the one critter currently on stage — lives in a small plane around you (34 m).
  Walking shifts it with real parallax; whatever falls out behind is forgotten and re-dreamt at the far edge ahead,
  seeded by the biome fields at that direction and distance. Solid things collide (with sliding).

### Rendering

* 160×120 framebuffer of **palette indices**, scaled 2× to the panel. The 256-entry palette is rebuilt each frame
  from time-of-day keyframes, so sunrise / sunset / cosmic night are palette work, not pixel work.
* Palette = 16 ramps × 16 shades (dark → light). Fog and distance are **palette twins**: lakes reflect through
  darkened twins of the sky / mountain ramps, mist steps pixels to their fogged twins, god rays brighten within a ramp.
* Ordered (Bayer) dithering for gradients, clouds, mist and fade-ins; splats build canopies, needle clumps, flowers;
  rocks are per-pixel Voronoi facets; cliffs are staggered ledges.
* Display follows the Synapse pipeline: two 320×240 PSRAM canvases, the palette expanded into one while the other is
  pushed by a task on core 0.

| File | Role |
|---|---|
| `src/main.cpp` | loop: controls → walk / collide → scene → encounters → footsteps on the beat → haptics → render → present |
| `src/sim.*` | IMU: tilt throttle, gyro yaw (rad), roll-lean drift |
| `src/field.*` | near field: spawn / forget, collision + slide, shore stop, critters |
| `src/universe.*` | hashes, periodic noise, biome / lake / forest / cloud fields, day cycle |
| `src/render.*` | palette, sky, night, clouds, ridges, mist, ground + lake, god rays, plants, rocks, critters |
| `src/display.*` | 2× expand into canvases + core-0 push task |
| `src/scene.*` | shared picture of now: time of day, weather, lightning, biome around you, moments |
| `src/audio.*`, `dsp.*`, `nature.cpp`, `music.cpp` | the sound engine (see Sound) |
| `src/haptics.*` | the motor as the low end of the sound system |

## Host tools (no ESP32 needed)

The firmware builds on a desktop compiler against `tools/host/stubs`, including the real `main.cpp`, the display
path and the audio engine (which the harness pumps in step with each frame).

* `tools/host/run.sh [minutes]` — the rules of the walk over 6 seeds: held level you never move; head-on into a
  rock you stay stopped (no autopilot); turn away and you're free; clip an edge and you slide past; never inside a
  solid, never into a lake, no NaN.
* `tools/host/tests.sh` — showcase scenes (golden forest, misty dawn shafts, sunset lake, rain, lightning, rainbow,
  murmuration, fireflies, aurora, noon) → `tools/host/snaps/sheet.png`. Run `./snap audio` after building to also
  render listening previews (`snaps/audio_*.wav`).
