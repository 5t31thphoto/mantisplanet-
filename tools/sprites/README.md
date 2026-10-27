# Mantis Sprite Studio

Desktop tool for creating MantisPlanet creature art and planet colour mods.

## Run

Requires Python 3.10+ and Pillow.

```
pip install pillow
python studio.py
```

## Workflow

1. Select a creature. The **Spec** tab lists its assets, frame counts and sizes.
2. On **Assets**, pick an asset and size, then **Copy template** or **Save template**.
3. Draw over the template in Krita or Photoshop at the same scale, facing right, using the creature palette
   (Tools > Export creature palette).
4. **Import art**. The image is scaled to game pixels and snapped to the palette. Deer head and tail points
   are set in the anchor editor.
5. Check it on the **Preview** tab: any biome, distance and time of day, with the game's lighting.
6. **File > Export pack to SD card** writes `/mp-sprites/` to the card.

## Planet colours

The **Planet colours** tab edits every base colour in the game. Colours can be edited individually or
exported as a swatch image, repainted, and imported. Enable "Include this planet in the pack" to export it.

## File formats

`.mps` sprite: `"MPS"`, width, height, frame count (one byte each); per frame its anchor points as signed
bytes (deer bodies: head x, y, tail x, y; parts: attach x, y; other creatures: 2 unused bytes); then
width × height × frames palette indices (0 = transparent; 144–159 and 208–223 only).

`planet.json`: RGB colour lists for the sky keyframes, base materials, biome ramps, creature palette and
aurora. Missing keys keep the default colours.

## Maintenance

- `bestiary.json` defines every creature; `gen_bestiary.py` generates `src/bestiary_gen.h` from it.
- `defaults/` is exported from the game by `tools/host/exportdefaults`.
