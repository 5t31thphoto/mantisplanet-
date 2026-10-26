#pragma once
#include <stdint.h>
#include <stddef.h>
#include "fishgen.h"
// The SD card: trophy shots and screenshots in the locked .mg format (docs/MG_FORMAT.md), and the catch log.
//   /images/fish/fish_0001.mg      trophy shots
//   /images/fish/catches.csv       one line per catch (seed, name, size, biome, water, bait, when)
//   /images/planet/planet_0001.mg  screenshots
// With no card the game carries on; nothing is saved and storageReady() says so.
namespace mantis {
bool storageBegin();
bool storageReady();
// Read a whole file from the card (mods: /mp-sprites/...). False if missing, unreadable or bigger than cap.
bool storageReadFile(const char* path, uint8_t* buf, size_t cap, size_t* got);
bool storageScreenshot();                                  // what's on screen now
bool storageSaveTrophy(const FishSpec& f);                 // what's on screen now (the trophy card) + a log line
void storageDate(char* out, int n);                        // "2026-10-19 18:42" (or "" if the clock isn't set)
const char* biomeName(uint8_t biome);

struct CatchRec { int idx; uint32_t seed; uint8_t water, biome, tier; float lengthCm; };
int catchCount();
const CatchRec& catchAt(int i);
bool storageLoadTrophy(int idx, uint16_t* out320x240);     // a saved trophy shot back into the overlay
}
