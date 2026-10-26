#pragma once
// Planet mods: /mp-sprites/planet.json replaces the planet's base colours (the time-of-day sky and light,
// the base materials, every biome's leaf, grass, stone, bark and rock ramps, the creature palette, the
// aurora). Everything the game lights and fogs follows from these, so a whole alien planet is one file.
// Missing keys keep Earth's colours; a file that can't be read is ignored entirely (never half-applied).
namespace mantis {
bool planetLoad();                         // after the card is up; true if a planet was applied
#ifdef HOST
bool planetWriteDefaults(const char* path);   // the current colours as planet.json (tools: the studio's starting point)
#endif
extern const char* const PLANET_BIOME_IDS[17];
}
