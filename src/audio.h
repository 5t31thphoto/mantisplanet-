#pragma once
#include <stdint.h>

// The sound of the dream: nature, weather and a generative dub score, synthesized live at 24 kHz
// on core 0 and streamed to the Core2 speaker. The speaker can't do bass, so the haptic motor plays
// the low end (see haptics.*): kick, thunder, bumps and footsteps are felt, not heard.

namespace mantis {

constexpr int   AUDIO_SR = 24000;
constexpr float MUSIC_BPM = 96.f;

enum : uint8_t { BUMP_TREE, BUMP_ROCK, BUMP_WATER };
enum : uint8_t { FISH_CAST, FISH_PLOP, FISH_NIBBLE, FISH_BITE, FISH_HOOKSET, FISH_RUN, FISH_JUMP, FISH_SNAP, FISH_MISS, FISH_LANDED, FISH_STOW, FISH_SHUTTER, FISH_BUG };
void sfxFish(uint8_t kind, float arg);
enum : uint8_t { MOMENT_INTRO, MOMENT_RAINBOW, MOMENT_MURMUR, MOMENT_AURORA, MOMENT_GOLDEN, MOMENT_TEMPLE };
enum : uint8_t { CRIT_DEER, CRIT_RABBIT, CRIT_BIRDS, CRIT_BUTTERFLY, CRIT_DUCK, CRIT_BEAVER, CRIT_SQUIRREL, CRIT_SPLASH, CRIT_HERON };

void audioBegin();

// game -> audio (lock-free, safe from the main loop)
void sfxFootstep(uint8_t surface, float strength);
void sfxBump(uint8_t kind);
void sfxThunder(float delaySec, float power, float close);
void sfxCritter(uint8_t kind, float dist);
void sfxMoment(uint8_t kind);

// audio -> game
float audioBeats();     // musical beats as heard right now (output latency compensated)
float audioKick();      // 0..1: the groove has a kick (the motor plays it on the beat)
float audioGroove();    // 0..1: how much groove; footsteps land on the 16th grid when it's up

// Mute silences the speaker only. The engine keeps running underneath, so the music clock still
// drives the motor's kick, footsteps on the beat and the fireflies.
void audioSetMuted(bool muted);
bool audioMuted();

// One engine block (the audio task calls this; the host harness calls it to make WAVs).
void audioRender(int16_t* out, int n);

}
