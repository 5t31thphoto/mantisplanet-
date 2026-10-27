#pragma once
#include <stdint.h>
// Creature sprites: built-in art, overridable from the SD card (/mp-sprites/), and the deer built from parts.
// A deer is one neutral BODY (per distance tier, per animation, 1-4 frames, each frame with a head anchor)
// plus ANTLERS (per tier, with the point where they attach). From those:
//   doe  = body, back hips raised a touch     buck = body, shoulders raised, antlers at the head anchor
//   fawn = the doe at 60% size, with cream spots along the back
// Species (sika, mule deer, elk, caribou, the white stag) are recolours of the same art at draw time.
//
// Files on the card: /mp-sprites/deer/body_<anim>_<tier>.mps, antlers_<tier>.mps, antlers_away_<tier>.mps,
//   tail_<down|flag|flick>_<tier>.mps, tail_away_<down|flag|flick>_<tier>.mps
//   anim: stand walk bound graze alert stomp sniff nurse away toward      tier: far mid near
//   .mps: "MPS", width, height, frames (1 byte each), then per frame its anchors (signed bytes: body files
//         head x, y, tail x, y; parts: attach x, y),
//         then width*height*frames palette indices (0 = clear; creature colours only: 144-159, 208-223).
// Anything missing, malformed or out of range is skipped and the built-in art stays.
namespace mantis {
struct Spr { uint8_t w = 0, h = 0; int8_t ax = 0, ay = 0, tx = 0, ty = 0; uint8_t k = 1; uint8_t* px = nullptr; };   // a: head (antlers) / attach point; t: tail root; k: drawn at k x k
enum : uint8_t { SEX_DOE, SEX_BUCK, SEX_FAWN, SEX_COW, SEX_N };   // COW: a caribou cow (doe hips, and antlers)
enum : uint8_t { AN_STAND, AN_WALK, AN_BOUND, AN_GRAZE, AN_ALERT, AN_STOMP, AN_SNIFF, AN_NURSE, AN_AWAY, AN_TOWARD, AN_N };   // AWAY: seen from behind
enum : uint8_t { TAIL_DOWN, TAIL_FLAG, TAIL_FLICK, TAIL_N };
enum : uint8_t { TIER_FAR, TIER_MID, TIER_NEAR, TIER_N };
constexpr uint8_t AN_MAXF = 4;
// built-in art (char rows) and the colour of each char, from the renderer
void creaturesBuiltIn(uint8_t (*colour)(char), uint8_t (*boneColour)(char));    // the built-in deer art (placeholder poses)
const Spr* deerTail(uint8_t sex, uint8_t tier, bool away, uint8_t state);         // the tail part to draw at the body's tail root
int creaturesLoadMods();                      // after the card is up: take any valid /mp-sprites/deer files; returns how many
const Spr* deerFrame(uint8_t sex, uint8_t tier, uint8_t anim, int frame);   // never null after creaturesBuiltIn
int deerFrames(uint8_t sex, uint8_t tier, uint8_t anim);                    // frames for this animation (falls back to stand)
uint8_t deerTierFor(float pxTall);
// Every other creature (see bestiary_gen.h): its modded art, if the card had any.
int creatureModFor(uint8_t kind, uint8_t variant, uint32_t seed);       // -1: none (draw the built-in)
bool creatureModFlock(int mod);                                         // one bird: the game draws the flock
const Spr* creatureModSprite(int mod, bool moving, float pxPerMetre, int frame);
// tools (exporter, studio): the built-in deer parts as they are
const Spr* deerBodyFrame(uint8_t tier, uint8_t anim, int frame, int* frames);
const Spr* deerPart(uint8_t tier, int which);                           // 0 antlers, 1 antlers_away, 2+st tail, 5+st tail_away
}
