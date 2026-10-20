#include "fishing.h"
#include "universe.h"
#include "journey.h"
#include "audio.h"
#include "haptics.h"
#include "bugs.h"
#include <math.h>

namespace mantis {

static FishView V;
static uint32_t s_rng = 0x5EEDF15Du;
static float frand() { s_rng = s_rng * 1664525u + 1013904223u; return (s_rng >> 8) * (1.f / 16777216.f); }
static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline float wrapPi(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }

// the fish on the line (step 1: tiers that fight differently; the procedural species come next)
// strength: steady pull; stamina: how long it lasts; runRate/runPow: how often and how hard it surges.
// A steady haul holds the line just under breaking; it's the runs that push it over, so hauling flat out
// is a gamble: it works if the fish doesn't run in time, and stronger fish run harder and sooner.
struct Fight { float strength, stamina, runRate, runPow, jumpRate, slackGrace, window; };
static Fight TIERS[4] = {
    {0.36f, 1.4f, 0.10f, 0.35f, 0.03f, 3.2f, 1.2f},     // common: forgiving
    {0.50f, 2.2f, 0.26f, 0.85f, 0.08f, 2.6f, 1.0f},     // uncommon
    {0.62f, 3.2f, 0.26f, 1.05f, 0.14f, 2.1f, 0.85f},    // rare: needs real control
    {0.74f, 4.4f, 0.40f, 1.35f, 0.22f, 1.7f, 0.7f},     // legendary
};
static float K_REEL_A = 0.2f, K_REEL_B = 0.25f, K_BREAK = 2.6f, K_DRAG = 0.85f;   // the drag slips before the line breaks
static float s_fightT = 0.f, s_throw = 0.f;
static float s_nextBite = 0.f, s_nextNibble = 0.f, s_waitT = 0.f, s_biteUntil = 0.f;
static int s_nibbles = 0;
static float s_runT = 0.f, s_runDir = 1.f, s_kickT = 0.f, s_slack = 0.f, s_stamLeft = 1.f;
static bool s_forceBite = false;
static FishSpec s_spec;
const FishSpec& fishingSpec() { return s_spec; }

static void enter(uint8_t st);
bool fishingActive() { return V.state != FS_OFF; }
const FishView& fishingView() { return V; }
void fishingForceBite() { s_forceBite = true; }
// tests: start a fight directly, and tune
void fishingTestFight(uint8_t tier, float lineOut, float yaw, uint32_t seed) {
    s_rng = seed * 2654435761u + 1u;
    V = FishView(); V.castYaw = yaw; V.bobZ = lineOut; V.tier = tier;
    V.fishSeed = s_rng; fishMake(s_spec, V.fishSeed, WATER_LAKE, B_LAKES, tier); V.lengthCm = s_spec.lengthCm;
    enter(FS_FIGHT); V.rodOut = 1.f;
    V.lineOut = lineOut; V.fishAng = yaw; V.stamina = 1.f; V.tension = 0.5f; V.breakRisk = 0.f;
    s_runT = 0.f; s_kickT = 0.f; s_slack = 0.f; s_stamLeft = 1.f; s_fightT = 0.f; s_throw = 0.f;
}
void fishingTune(int tier, float strength, float stamina, float runRate, float runPow, float ka, float kb, float kbreak) {
    if (tier >= 0 && tier < 4) { TIERS[tier].strength = strength; TIERS[tier].stamina = stamina; TIERS[tier].runRate = runRate; TIERS[tier].runPow = runPow; }
    K_REEL_A = ka; K_REEL_B = kb; K_BREAK = kbreak;
}

static void enter(uint8_t st) { V.state = st; V.t = 0.f; }

// Is there water to cast into along this heading? Returns the bobber distance, or 0.
static float findWater(float yaw, float D, uint8_t& kind) {
    const bool frozen = biomeAt(D).snow > 0.5f;
    for (float z = 2.5f; z < 18.f; z += 0.5f) {
        int hits = 0;
        uint8_t k = WATER_LAKE;
        for (int s = -1; s <= 1; s++) {
            float a = yaw + s * 0.04f;
            if (lakeField(a, D + z) > LAKE_T) { hits++; k = oceanShore(a, D + z) < 1e8f ? WATER_SEA : frozen ? WATER_ICE : WATER_LAKE; }
            else if (inStream(a, D + z)) { hits++; k = WATER_CREEK; }
            else if (paddyAt(a, D + z)) { hits++; k = WATER_PADDY; }
        }
        if (hits >= 2) {
            kind = k;
            if (k == WATER_CREEK || k == WATER_PADDY) return z + 0.3f;      // small water: drop it right in
            float zz = z;                                                   // open water: a few metres out, still on the water
            for (float e = z; e < z + 5.f && e < 22.f; e += 0.5f) { if (lakeField(yaw, D + e) > LAKE_T) zz = e; else break; }
            return fminf(zz, z + 4.f);
        }
    }
    return 0.f;
}

static float biteDelay(const Scene& sc) {
    float rate = 1.f / 20.f;                                         // a bite every ~20 s on average
    float tod = sc.tod;
    if ((tod > 0.22f && tod < 0.33f) || (tod > 0.68f && tod < 0.78f)) rate *= 1.7f;   // dawn and dusk
    if (sc.cloud > 0.6f) rate *= 1.2f;
    if (sc.rain > 0.2f && sc.storm < 0.3f) rate *= 1.3f;
    if (sc.storm > 0.4f) rate *= 0.6f;
    if (tod > 0.86f || tod < 0.18f) rate *= 0.7f;
    if (baitSelected() >= 0) rate *= 1.5f;                            // something on the hook
    return -logf(fmaxf(1e-4f, frand())) / rate;
}

bool fishingTap(float yaw, float distance) {
    if (V.state == FS_OFF) {
        uint8_t kind;
        float z = findWater(yaw, distance, kind);
        if (z <= 0.f) return false;
        V = FishView();
        V.castYaw = yaw; V.bobZ = z; V.water = kind;
        enter(FS_CAST);
        sfxFish(FISH_CAST, 0.f);
        return true;
    }
    if (V.state == FS_WAIT || V.state == FS_CAST) { enter(FS_STOW); sfxFish(FISH_STOW, 0.f); return true; }
    return false;
}

static void newFish() {
    float r = frand();
    float rareP = 0.07f, legP = 0.01f, uncP = 0.22f;
    int bait = baitSelected();
    if (bait >= 0) uncP += 0.05f;
    if (bait >= 0 && baitSpecialBiome((uint8_t)bait) == journeyBiome()) { rareP = 0.15f; legP = 0.04f; }   // the right bait in the right water
    V.tier = r < legP ? TIER_LEGEND : r < legP + rareP ? TIER_RARE : r < legP + rareP + uncP ? TIER_UNCOMMON : TIER_COMMON;
    V.fishSeed = s_rng ^ (uint32_t)(V.castYaw * 1e4f);
    fishMake(s_spec, V.fishSeed, V.water, journeyBiome(), V.tier);     // the species of this water and this place
    V.lengthCm = s_spec.lengthCm;
}

void fishingUpdate(float dt, float yaw, float distance, float pitchDeg, float flickDps, const Scene& sc) {
    V.t += dt;
    V.rodOut += ((V.state == FS_OFF || V.state == FS_STOW) ? -1.f : 1.f) * dt * 3.f;
    V.rodOut = clampf(V.rodOut, 0.f, 1.f);
    V.reel = clampf((-pitchDeg - 4.f) / 22.f, 0.f, 1.f);          // tilt back past upright: reel
    V.ease = clampf((pitchDeg - 6.f) / 20.f, 0.f, 1.f);            // lean forward: give line
    bool flick = flickDps > 150.f;
    float away = fabsf(wrapPi(yaw - V.castYaw));
    switch (V.state) {
        case FS_OFF: break;
        case FS_CAST:
            if (V.t > 0.8f) { enter(FS_WAIT); s_waitT = 0.f; s_nextBite = biteDelay(sc); s_nibbles = (int)(frand() * 3.5f); s_nextNibble = s_nextBite - s_nibbles * (0.9f + frand()) ; sfxFish(FISH_PLOP, 0.f); }
            break;
        case FS_WAIT: {
            if (away > 0.6f) { enter(FS_STOW); sfxFish(FISH_STOW, 0.f); break; }   // turned away: put the rod away
            s_waitT += dt;
            V.nibble = fmaxf(0.f, V.nibble - dt * 5.f);
            V.dip = fmaxf(0.f, V.dip - dt * 3.f);
            if (s_forceBite) { s_forceBite = false; s_nextBite = s_waitT; s_nibbles = 0; }
            if (s_nibbles > 0 && s_waitT >= s_nextNibble) {
                V.nibble = 1.f; s_nibbles--; s_nextNibble = s_waitT + 0.7f + frand() * 1.2f;
                hx::fishTick(0.35f); sfxFish(FISH_NIBBLE, 0.f);
                if (flick) { V.lostWhy = 4; enter(FS_LOST); break; }      // struck at a nibble: spooked it
            }
            if (!V.biting && s_waitT >= s_nextBite && s_nibbles == 0) {
                newFish();
                V.biting = true; s_biteUntil = s_waitT + TIERS[V.tier].window; V.dip = 1.f;
                hx::fishBite(); sfxFish(FISH_BITE, 0.f);
            }
            if (V.biting) {
                V.dip = 1.f;
                if (flick) {                                               // hook set!
                    V.biting = false;
                    enter(FS_FIGHT);
                    V.lineOut = V.bobZ; V.fishAng = V.castYaw; V.stamina = 1.f; V.tension = 0.5f; V.breakRisk = 0.f;
                    s_runT = 0.f; s_kickT = 0.f; s_slack = 0.f; s_stamLeft = 1.f; s_fightT = 0.f; s_throw = 0.f;
                    hx::fishHookSet(); sfxFish(FISH_HOOKSET, 0.f);
                    baitConsume();                                         // the bait's gone
                } else if (s_waitT > s_biteUntil) {                        // too slow
                    V.biting = false; V.dip = 0.f;
                    s_nextBite = s_waitT + biteDelay(sc); s_nibbles = (int)(frand() * 3.f); s_nextNibble = s_nextBite - s_nibbles * 1.2f;
                    sfxFish(FISH_MISS, 0.f);
                }
            }
        } break;
        case FS_FIGHT: {
            const Fight& F = TIERS[V.tier];
            float sta = s_stamLeft;
            s_fightT += dt;
            // the fish: a steady pull that fades as it tires, runs, head shakes, the odd jump
            if (s_runT <= 0.f && frand() < F.runRate * sta * dt * 4.f) { s_runT = 0.8f + frand() * 1.4f; s_runDir = frand() < 0.5f ? -1.f : 1.f; sfxFish(FISH_RUN, 0.f); }
            float pull = F.strength * (0.35f + 0.65f * sta);
            if (s_runT > 0.f) { s_runT -= dt; pull += F.runPow * sta; V.fishAng += s_runDir * 0.22f * dt; }
            V.fishAng += wrapPi(V.castYaw - V.fishAng) * 0.05f * dt;    // drifts back toward where it bit
            s_kickT -= dt;
            if (s_kickT <= 0.f) {                                      // a kick: shake of the head, felt as a pulse
                s_kickT = 0.3f + (1.f - sta) * 0.9f + frand() * 0.3f;
                hx::fishKick(0.35f + 0.65f * sta);
                pull += 0.15f * F.strength;
            }
            if (V.jump <= 0.f && frand() < F.jumpRate * sta * dt) { V.jump = 0.9f; sfxFish(FISH_JUMP, 0.f); hx::fishKick(1.f); }
            V.jump = fmaxf(0.f, V.jump - dt);
            // in the air it shakes its head: bow to it (ease off) or a tight line lets it throw the hook
            if (V.jump > 0.f && V.tension > 0.7f) s_throw += (V.tension - 0.7f) * 2.4f * dt;
            else s_throw = fmaxf(0.f, s_throw - dt);
            if (s_throw > 0.35f) { V.lostWhy = 2; enter(FS_LOST); hx::fishTension(0.f, 0.f, false); sfxFish(FISH_MISS, 0.f); break; }
            // you: reel against it, or give line; facing the fish gives you control
            float mis = fabsf(wrapPi(V.fishAng - yaw));
            float control = clampf(1.f - mis / 1.2f, 0.3f, 1.f);
            V.lineSpeed = pull * 2.2f * (1.f + V.ease) - V.reel * 2.4f * control * (1.f - 0.4f * pull) - 0.25f;   // a strong fish is hard to gain on
            V.lineOut = clampf(V.lineOut + V.lineSpeed * dt, 0.f, 60.f);
            V.tension = clampf(fminf(pull * (1.f - 0.75f * V.ease), K_DRAG) + V.reel * (K_REEL_A + K_REEL_B * pull) * control + mis * 0.12f, 0.f, 1.5f);   // the fish alone can only pull drag; hauling against it loads the line
            s_stamLeft = clampf(s_stamLeft - dt * (0.05f + 0.35f * V.tension) / F.stamina, 0.f, 1.f);   // held under steady pressure it tires
            V.stamina = s_stamLeft;
            // too tight for too long and it snaps; too slack and the hook comes free
            V.breakRisk = clampf(V.breakRisk + (V.tension > 1.f ? (V.tension - 1.f) * K_BREAK : -0.6f) * dt, 0.f, 1.f);
            s_slack = V.tension < 0.15f ? s_slack + dt : fmaxf(0.f, s_slack - dt * 2.f);
            hx::fishTension(V.tension, V.breakRisk, true);
            if (V.lineOut >= 60.f || V.breakRisk >= 1.f) { V.lostWhy = 1; enter(FS_LOST); hx::fishTension(0.f, 0.f, false); hx::fishSnap(); sfxFish(FISH_SNAP, 0.f); break; }
            if (s_slack > F.slackGrace * (0.6f + 0.4f * sta) + 0.6f * (1.f - sta)) { V.lostWhy = 2; enter(FS_LOST); hx::fishTension(0.f, 0.f, false); sfxFish(FISH_MISS, 0.f); break; }
            if (s_fightT > 120.f) { V.lostWhy = 2; enter(FS_LOST); hx::fishTension(0.f, 0.f, false); sfxFish(FISH_MISS, 0.f); break; }   // left to its own devices, it works free
            if (V.lineOut < 1.2f) { enter(FS_LANDED); hx::fishTension(0.f, 0.f, false); hx::swell(); sfxFish(FISH_LANDED, 0.f); }
        } break;
        case FS_LANDED:
            if (V.t > 5.f) enter(FS_STOW);                              // admire it, let it go, rod away
            break;
        case FS_LOST:
            if (V.t > 1.6f) enter(FS_STOW);
            break;
        case FS_STOW:
            if (V.rodOut <= 0.f) enter(FS_OFF);
            break;
    }
}

}
