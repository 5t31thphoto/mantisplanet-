# Mantis Sprite Studio

The art pipeline for MantisPlanet's creatures and planet colours. The dev team builds the next generation of
creature art here; the same tool lets players mod their own planets.

## Install (Windows)
1. Python 3.10 or newer from python.org (tick "Add python.exe to PATH"; Tk is included).
2. `pip install pillow`
3. `python mantis_sprite_studio.py` (from this folder)

Optional, for live previews: the live renderer is the game itself built for Windows. Install MSYS2, open
"MSYS2 MINGW64", run `pacman -S --needed mingw-w64-x86_64-gcc`, then from `tools/host`: `sh build_mpview.sh`.
In the studio: Settings, point "Live renderer" at `tools/host/mpview.exe`; tick "Live renderer" on the Preview tab.

## The workflow
1. Pick a creature. The **Spec** tab says exactly what it takes: each asset, its frame count, its sizes.
2. On **Assets**, choose an asset and size, then **Copy template** (paste into Krita) or **Save template**.
   The template is the current art blown up so a game pixel is many screen pixels; draw over it at that scale.
   Clean pixel art converts exactly; painted art is averaged into game pixels. Transparent background (or a
   plain background colour for JPGs). Facing right. Use the creature palette (Tools: export the .gpl / .aco).
3. **Import art**: the studio shrinks it to game pixels, snaps it to the 32 creature colours and keeps its
   anchors. **Undo import** steps back once; **Revert to game art** removes yours.
4. Deer only: set the **head / attach** and **tail root** points per frame in the anchor editor (or draw one
   magenta #FF00FF / cyan #00FFFF pixel in the art). Bodies are drawn without tails; tails and antlers are parts.
5. **Preview**: the art in its biome at far, mid and near, animated; deer as doe, buck, fawn or cow, any pose
   and tail. With the live renderer on, it's the game itself, at any time of day and weather.
6. **Export pack to SD** writes `<card>/mp-sprites/...`. The game reads it at boot; anything missing or broken
   falls back to the built-in art.

## Planet colours
The **Planet colours** tab holds all 478 base colours: the sky and light through the day, base materials,
every biome's ramps, the creature palette and the aurora. Edit them one by one, or **Export swatch**, repaint
it in Krita and **Import swatch**. Tick "Include this planet in the pack" to send `planet.json` to the card.

## Files
- `bestiary.json`: every creature, its assets and sizes (run `gen_bestiary.py` after editing: it makes the
  game's `src/bestiary_gen.h`).
- `defaults/`: today's game art, palette, plates and default planet, exported from the game by
  `tools/host/exportdefaults` (run it again whenever the built-in art changes).
- `.mps` (the game's sprite file): "MPS", width, height, frames; per frame its anchors (signed bytes: bodies
  head x, y, tail x, y; parts attach x, y; other creatures 2 bytes, unused); then width x height x frames
  palette indices (0 = clear; creature colours 144-159, 208-223 only).
