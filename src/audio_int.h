#pragma once
#include "dsp.h"
#include "scene.h"
#include <stdint.h>

namespace mantis {
namespace snd {

constexpr int BLOCK = 512;          // ~21 ms: enough slack that a busy frame can't starve the speaker
struct Bus { float dry[BLOCK], rev[BLOCK], dly[BLOCK]; };

extern dsp::Rng rng;
extern uint32_t samples;            // samples rendered so far

// nature.cpp
void natureInit();
void natureBlock(Bus& b, int n, const Scene& s);
void natureFootstep(uint8_t surface, float strength);
void natureBump(uint8_t kind);
void natureThunder(float delaySec, float power, float close);
void natureFish(uint8_t kind, float arg);
void natureCritter(uint8_t kind, float dist);

// music.cpp
void musicInit();
void musicBlock(Bus& b, int n, const Scene& s);
void musicMoment(uint8_t kind);
float musicKick();
float musicGroove();
float musicDelaySamples();

}
}
