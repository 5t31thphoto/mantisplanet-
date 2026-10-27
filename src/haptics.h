#pragma once
#include <stdint.h>

// The motor is the low end of the sound system: the Core2 speaker can't move air below ~200 Hz,
// so everything with weight is felt instead. An ERM motor follows setVibration(0..255); this mixer
// max-mixes taps, a continuous rumble and keyframed gestures, remaps above the motor's dead zone and
// only writes the I2C register when the level really changes.

namespace hx {
void begin();
void footstep(uint8_t surface, float strength);   // shaped per surface (grass, forest, stone, mud, wet)
void bump();                                      // walked into something solid
void kick(float level);                           // the groove's kick, felt in the hands
void thunder(float power, float close);           // crack (close) and a long roll
void hooves(float dist);                          // a deer bolting
void swell();
void heartbeat();
void confirmMute(bool muted);                     // two soft taps = sound off, a rising tap = sound on
void rain(float amount);
void wind(float storm, float gust);               // a faint rumble only in a storm
void fishTick(float level);                       // a nibble at the bobber
void fishBite();                                  // the bobber goes under
void fishHookSet();                               // the hook bites home
void fishKick(float strength);                    // the fish shakes its head / kicks
void fishSnap();                                  // the line breaks
void fishTension(float tension, float breakRisk, bool on);   // the line, held: rumble with tension, stutter near breaking
void update(float dt);
}
