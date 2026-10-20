// The score: minimalist ambient dub, conducted by what you're doing.
// Walk steadily and a groove assembles itself around your footsteps; stand still and it dissolves
// back into the drone and the forest. Time of day picks the mode; moments bring bells and choir.
#include "audio_int.h"
#include "audio.h"
#include <string.h>

namespace mantis {
namespace snd {

using dsp::clampf;
static inline float smooth(float a, float b, float x) { float t = clampf((x - a) / (b - a), 0.f, 1.f); return t * t * (3.f - 2.f * t); }

// ---------------- harmony ----------------
struct Chord { int8_t n[4]; int8_t root; };
enum : uint8_t { MOOD_DAWN, MOOD_DAY, MOOD_GOLD, MOOD_NIGHT, MOOD_STORM, MOOD_JAPAN, MOOD_CANYON, MOOD_KARST, MOOD_N };
static const Chord CHORDS[MOOD_N][4] = {
    // dawn: D lydian, open and lifting
    {{{54, 57, 61, 64}, 38}, {{56, 59, 64, 66}, 38}, {{54, 57, 59, 62}, 35}, {{52, 57, 59, 64}, 33}},
    // day: D dorian
    {{{53, 57, 60, 64}, 38}, {{53, 57, 59, 62}, 43}, {{55, 60, 64, 67}, 45}, {{55, 59, 62, 64}, 36}},
    // golden hour: D aeolian, warm and wide
    {{{53, 57, 60, 64}, 38}, {{53, 57, 60, 62}, 34}, {{53, 57, 60, 64}, 41}, {{55, 60, 62, 67}, 36}},
    // night: a phrygian shadow
    {{{53, 57, 62, 64}, 38}, {{51, 55, 58, 62}, 39}, {{53, 57, 58, 62}, 34}, {{55, 60, 62, 64}, 33}},
    // storm: pedal and shadow
    {{{50, 57, 62, 65}, 38}, {{50, 58, 62, 65}, 34}, {{50, 57, 62, 65}, 38}, {{50, 57, 60, 65}, 36}},
    // the garden: open fifths and the in scale (D Eb G A Bb)
    {{{50, 57, 62, 67}, 38}, {{51, 58, 62, 67}, 39}, {{50, 55, 62, 69}, 38}, {{46, 53, 58, 62}, 34}},
    // the canyon: wide open fifths over a D pedal, a flattened seventh for the dusk
    {{{50, 57, 62, 64}, 38}, {{48, 55, 62, 64}, 36}, {{50, 55, 60, 62}, 38}, {{45, 52, 57, 60}, 33}},
    // karst: the gong scale, open and floating
    {{{50, 57, 62, 66}, 38}, {{47, 54, 59, 62}, 35}, {{52, 59, 62, 66}, 40}, {{45, 52, 57, 64}, 33}},
};
static const int8_t PENTA[MOOD_N][7] = {
    {62, 64, 66, 69, 71, 74, 76}, {62, 65, 67, 69, 72, 74, 77}, {62, 65, 67, 69, 72, 74, 77},
    {62, 64, 65, 69, 70, 74, 76}, {62, 65, 69, 70, 74, 77, 81}, {62, 63, 67, 69, 70, 74, 75}, {62, 65, 67, 69, 72, 74, 76}, {62, 64, 66, 69, 71, 74, 76}};

static uint8_t s_mood = MOOD_GOLD;
static int s_chordI = 0;
static Chord s_chord = CHORDS[MOOD_GOLD][0];

// ---------------- conductor state ----------------
static float s_energy = 0.f, s_awe = 0.f, s_aweBoost = 0.f;
static float g_drone = 0, g_pad = 0, g_dub = 0, g_hat = 0, g_bass = 0, g_kick = 0, g_pluck = 0, g_choir = 0;
static int64_t s_next16 = 0;
static int s_step = 0;                // 16th counter
static float s_lastNight = 0.f;

static const float SAMPLES_PER_16 = AUDIO_SR * 60.f / MUSIC_BPM / 4.f;

// ---------------- voices ----------------
struct PadVoice { dsp::Saw a, b; float hz, target; };
static PadVoice s_pad[4];
static dsp::SVF s_padF;
static uint32_t s_lfo1 = 0, s_lfo2 = 0, s_lfo3 = 0;

static uint32_t s_dr[3] = {0, 0, 0};
static uint32_t s_drAm[3] = {0, 0, 0};

struct Stab { dsp::Saw o[4]; float env; dsp::SVF f; };
static Stab s_stab;

static float s_hatEnv = 0.f, s_hatDec = 0.99f, s_hatLP = 0.f, s_hatBP = 0.f;
static float s_kickEnv = 0.f, s_clickEnv = 0.f;
static uint32_t s_kickPh = 0;
static float s_kickHz = 110.f;

static dsp::Saw s_bassO;
static dsp::SVF s_bassF;
static float s_bassEnv = 0.f, s_bassCut = 300.f;

struct KS { float* buf; int len, pos; float damp, amp; bool on; };
static KS s_ks[3];
static int s_ksI = 0, s_pentaI = 3;

struct Bell { uint32_t ph[3], dph[3]; float env[3], dec[3]; bool on; };
static Bell s_bell[2];
static int s_bellI = 0;

static dsp::Saw s_choir[3];
static dsp::SVF s_f1, s_f2;
static uint32_t s_vib = 0;

static float s_dlySamples = SAMPLES_PER_16 * 3.f;
float musicDelaySamples() { return s_dlySamples; }
float musicKick() { return g_kick; }
float musicGroove() { return clampf((g_dub + g_hat + g_bass) * 0.6f + g_kick * 0.5f, 0.f, 1.f); }

static void bell(float hz, float amp) {
    Bell& b = s_bell[s_bellI = (s_bellI + 1) & 1];
    static const float R[3] = {1.f, 2.76f, 5.4f}, D[3] = {1.1f, 2.f, 3.8f}, A[3] = {0.55f, 0.28f, 0.12f};
    for (int k = 0; k < 3; k++) {
        b.ph[k] = 0;
        b.dph[k] = dsp::inc(hz * R[k] * (k == 1 ? 1.003f : 1.f));
        b.env[k] = A[k] * amp;
        b.dec[k] = expf(-D[k] / dsp::SR);
    }
    b.on = true;
}

static void pluck(float hz, float amp, float bright) {
    KS& k = s_ks[s_ksI = (s_ksI + 1) % 3];
    if (!k.buf) return;
    k.len = (int)clampf(dsp::SR / hz, 8.f, 511.f);
    k.pos = 0;
    float lp = 0.f;
    for (int i = 0; i < k.len; i++) { lp += bright * (rng.bi() - lp); k.buf[i] = lp; }
    k.damp = 0.4991f + 0.0006f * (1.f - bright);       // a longer, darker ring: harp, not blip
    k.amp = amp;
    k.on = true;
}

static void pickMood(const Scene& s) {
    float tod = s.tod;
    uint8_t m;
    if (s.storm > 0.4f || s.rain > 0.7f) m = MOOD_STORM;
    else if (s.japan > 0.5f) m = MOOD_JAPAN;
    else if (s.desert > 0.5f) m = MOOD_CANYON;
    else if (s.karst > 0.5f) m = MOOD_KARST;
    else if (tod > 0.84f || tod < 0.17f) m = MOOD_NIGHT;
    else if (tod < 0.33f) m = MOOD_DAWN;
    else if (tod < 0.62f) m = MOOD_DAY;
    else m = MOOD_GOLD;
    s_mood = m;
}

static void setChord(const Scene& s) {
    pickMood(s);
    s_chord = CHORDS[s_mood][s_chordI & 3];
    // voice leading: each pad voice glides to the nearest unused chord tone
    bool used[4] = {false, false, false, false};
    for (int v = 0; v < 4; v++) {
        float cur = s_pad[v].target;
        int best = 0;
        float bd = 1e9f;
        for (int k = 0; k < 4; k++) {
            if (used[k]) continue;
            float d = fabsf(dsp::midiHz(s_chord.n[k]) - cur);
            if (d < bd) { bd = d; best = k; }
        }
        used[best] = true;
        s_pad[v].target = dsp::midiHz(s_chord.n[best]);
    }
}

void musicMoment(uint8_t kind) {
    float root = dsp::midiHz(s_chord.root + 36);
    switch (kind) {
        case MOMENT_INTRO:   bell(dsp::midiHz(74), 0.5f); break;
        case MOMENT_RAINBOW: bell(root, 0.55f); bell(root * 1.5f, 0.35f); s_aweBoost = 1.f; break;
        case MOMENT_MURMUR:  s_aweBoost = 0.8f; break;
        case MOMENT_AURORA:  bell(root * 0.75f, 0.45f); s_aweBoost = 1.f; break;
        case MOMENT_GOLDEN:  bell(root, 0.35f); s_aweBoost = 0.6f; break;
        case MOMENT_TEMPLE:  bell(dsp::midiHz(43), 0.9f); bell(dsp::midiHz(55), 0.4f); s_aweBoost = 0.7f; break;   // the temple bell
    }
}

// The tide: the forest is the main act. Music comes in as a passage and goes out again.
enum : uint8_t { TIDE_REST, TIDE_RISE, TIDE_SONG, TIDE_FALL };
static uint8_t s_tide = TIDE_REST;
static float s_tideT = 0.f, s_tideLen = 40.f;
// Foreground budget: at most two voices on stage, chosen as pairs that sit well together.
enum : uint8_t { V_DUB = 1, V_HAT = 2, V_BASS = 4, V_PLUCK = 8 };
static uint8_t s_stage = V_DUB;
static const uint8_t SOLO[3] = {V_DUB, V_PLUCK, V_DUB};
static const uint8_t DUO[5] = {V_DUB | V_HAT, V_DUB | V_PLUCK, V_HAT | V_BASS, V_DUB | V_BASS, V_PLUCK | V_HAT};

static void castStage(float E) {
    s_stage = E < 0.5f ? SOLO[rng.next() % 3] : DUO[rng.next() % 5];
    if (s_mood == MOOD_NIGHT) s_stage &= (uint8_t)~(V_HAT | V_BASS);       // night keeps it hushed
    if (s_mood == MOOD_STORM) s_stage = 0;
    if (!s_stage) s_stage = V_DUB;
}

// one 16th note of the score
static void step(const Scene& s) {
    int st = s_step & 15;
    int bar = s_step >> 4;
    if (st == 0 && (bar & 1) == 0) {                         // chord every 2 bars
        s_chordI++;
        setChord(s);
        if (s_awe > 0.55f && rng.uni() < 0.5f) bell(dsp::midiHz(s_chord.n[3] + 12), 0.25f);
    }
    if (st == 0 && (bar & 7) == 0 && s_tide == TIDE_SONG) castStage(s_energy);   // a new pair takes the stage
    // kick on the beat (the motor does the real work)
    if ((st & 3) == 0 && g_kick > 0.05f) { s_kickEnv = g_kick; s_clickEnv = g_kick; s_kickHz = 115.f; s_kickPh = 0; }
    // dub chord stab on the 2-and / 4-and, sometimes more
    if (g_dub > 0.05f) {
        bool hit = st == 6 || st == 14;
        if (hit && rng.uni() < 0.7f) {
            for (int k = 0; k < 4; k++) s_stab.o[k].dph = dsp::inc(dsp::midiHz(s_chord.n[k] + 12) * (k & 1 ? 1.002f : 0.998f));
            s_stab.env = g_dub;
            s_stab.f.set(850.f + 500.f * rng.uni() + 600.f * s_awe, 1.6f);
        }
    }
    // shaker: 16ths, accents on the off-beats
    if (g_hat > 0.05f) {
        if ((st & 3) == 2) {                                 // one soft shake on each off-beat, no 16th chatter
            s_hatEnv = g_hat * (0.75f + 0.25f * rng.uni()) * ((st & 7) == 6 ? 1.f : 0.8f);
            s_hatDec = expf(-1.f / (0.06f * dsp::SR));
        }
    }
    // rolling bass on the 16ths between the kicks
    if (g_bass > 0.05f && (st & 3) != 0) {
        int note = s_chord.root + 12 + ((st & 3) == 3 && rng.uni() < 0.25f ? 12 : 0);
        s_bassO.dph = dsp::inc(dsp::midiHz(note));
        s_bassEnv = g_bass * ((st & 3) == 1 ? 0.8f : 1.f);
        s_bassCut = 1500.f;
    }
    // plucked arpeggio wandering the mode, echoing through the delay
    if (g_pluck > 0.05f && (st & 3) == 0 && rng.uni() < 0.22f) {
        s_pentaI = (int)clampf((float)(s_pentaI + (int)(rng.uni() * 3.f) - 1), 0.f, 6.f);   // stepwise, never leaping
        pluck(dsp::midiHz(PENTA[s_mood][s_pentaI]), g_pluck * 0.45f, 0.22f + 0.18f * rng.uni());
    }
    s_step++;
}

static void conduct(const Scene& s, float dt) {
    // energy only builds from a long steady walk; any stillness lets it go
    float walk = clampf((s.walkFor - 12.f) / 45.f, 0.f, 1.f) * clampf(s.speed / 0.5f, 0.4f, 1.f);
    float e = walk * (1.f - s.rain * 0.6f) * (1.f - s.storm * 0.9f);
    bool night = s.tod > 0.84f || s.tod < 0.17f;
    if (night) e = fminf(e, 0.5f);
    e *= 1.f - 0.5f * s.hush;                               // the giants ask for quiet
    if (s.t < 30.f) e = 0.f;
    if (s.stillFor > 3.f) e = 0.f;
    s_energy += (e - s_energy) * clampf(dt / (e > s_energy ? 10.f : 6.f), 0.f, 1.f);
    s_aweBoost *= expf(-dt / 25.f);
    float aw = fmaxf(s.awe, s_aweBoost);
    s_awe += (aw - s_awe) * clampf(dt / 4.f, 0.f, 1.f);
    float E = s_energy;

    // tide state machine
    s_tideT += dt;
    switch (s_tide) {
        case TIDE_REST:   // the forest alone (a whisper of drone)
            if (s_tideT > s_tideLen && (E > 0.2f || s_awe > 0.4f)) { s_tide = TIDE_RISE; s_tideT = 0.f; s_tideLen = 25.f + rng.uni() * 25.f; }
            break;
        case TIDE_RISE:   // the pad breathes in
            if (s.stillFor > 3.f) { s_tide = TIDE_FALL; s_tideT = 0.f; s_tideLen = 12.f; }
            else if (s_tideT > s_tideLen && E > 0.35f) { s_tide = TIDE_SONG; s_tideT = 0.f; s_tideLen = 60.f + rng.uni() * 60.f; castStage(E); }
            else if (s_tideT > s_tideLen * 3.f) { s_tide = TIDE_FALL; s_tideT = 0.f; s_tideLen = 20.f; }
            break;
        case TIDE_SONG:   // a passage with one or two voices
            if (s.stillFor > 3.f || E < 0.2f || s_tideT > s_tideLen) { s_tide = TIDE_FALL; s_tideT = 0.f; s_tideLen = 20.f; }
            break;
        case TIDE_FALL:   // dissolving back into the forest
            if (s_tideT > s_tideLen) { s_tide = TIDE_REST; s_tideT = 0.f; s_tideLen = 50.f + rng.uni() * 70.f; }
            break;
    }
    float presence = s_tide == TIDE_REST ? 0.f : s_tide == TIDE_RISE ? clampf(s_tideT / 12.f, 0.f, 1.f)
                   : s_tide == TIDE_SONG ? 1.f : clampf(1.f - s_tideT / s_tideLen, 0.f, 1.f);
    bool song = s_tide == TIDE_SONG;
    float duck = 1.f - 0.6f * s_awe;               // awe belongs to bells and choir

    auto ease = [&](float& g, float target, float tau) { g += (target - g) * clampf(dt / tau, 0.f, 1.f); };
    float intro = clampf(s.t / 6.f, 0.f, 1.f);
    ease(g_drone, (0.32f + 0.25f * presence + 0.2f * s_awe) * intro, 6.f);
    ease(g_pad, (0.5f * presence + 0.35f * s_awe) * intro, 8.f);
    float fg = song ? smooth(0.3f, 0.6f, E) * duck : 0.f;
    ease(g_dub, (s_stage & V_DUB) ? fg : 0.f, 4.f);
    ease(g_hat, (s_stage & V_HAT) ? fg * 0.8f : 0.f, 4.f);
    ease(g_bass, (s_stage & V_BASS) ? fg : 0.f, 4.f);
    ease(g_kick, ((s_stage & V_BASS) && !night && E > 0.75f) ? fg : 0.f, 4.f);
    ease(g_pluck, (s_stage & V_PLUCK) ? fg * 0.8f : (song ? 0.f : 0.25f * s_awe + 0.3f * fmaxf(s.japan, s.karst)), 5.f);   // a koto phrase now and then in the garden
    ease(g_choir, smooth(0.4f, 0.9f, s_awe), 8.f);
    s_lastNight = night ? 1.f : 0.f;
}

void musicInit() {
    for (int v = 0; v < 4; v++) {
        s_pad[v].hz = s_pad[v].target = dsp::midiHz(CHORDS[MOOD_GOLD][0].n[v]);
        s_pad[v].a.ph = rng.next();
        s_pad[v].b.ph = rng.next();
    }
    for (KS& k : s_ks) { k.on = false; k.len = 100; k.pos = 0; if (!k.buf) k.buf = (float*)dsp::allocFast(512 * sizeof(float)); }
    for (Bell& b : s_bell) b.on = false;
    s_next16 = (int64_t)SAMPLES_PER_16;
}

void musicBlock(Bus& b, int n, const Scene& s) {
    float dt = (float)n / dsp::SR;
    conduct(s, dt);
    // block-rate parameters
    for (int v = 0; v < 4; v++) {
        PadVoice& p = s_pad[v];
        p.hz += (p.target - p.hz) * clampf(dt / 1.2f, 0.f, 1.f);
        p.a.dph = dsp::inc(p.hz * 0.9965f);
        p.b.dph = dsp::inc(p.hz * 1.0035f);
    }
    s_lfo1 += dsp::inc(0.031f) * n;
    s_lfo2 += dsp::inc(0.07f) * n;
    s_lfo3 += dsp::inc(0.013f) * n;
    float bright = 0.5f + 0.5f * dsp::sine(s_lfo1);
    float moodBright = s_mood == MOOD_NIGHT ? 0.6f : s_mood == MOOD_STORM ? 0.45f : s_mood == MOOD_DAWN ? 1.15f : 1.f;
    s_padF.set((550.f + 1100.f * bright + 600.f * s_awe) * moodBright * (1.f - 0.3f * s.forestNear), 0.85f);
    float rootHz = dsp::midiHz(62);                      // the drone stays home on D
    float drHz[3] = {rootHz, rootHz * 1.4983f, rootHz * 2.f};
    for (int k = 0; k < 3; k++) s_drAm[k] += dsp::inc(0.05f + k * 0.031f) * n;
    s_bassF.set(s_bassCut, 1.4f);
    s_bassCut = fmaxf(260.f, s_bassCut * powf(0.5f, dt / 0.05f));
    float chHz[3];
    for (int k = 0; k < 3; k++) { chHz[k] = dsp::midiHz(s_chord.n[k] + 12); s_choir[k].dph = dsp::inc(chHz[k]); }
    s_f1.set(720.f, 4.f);
    s_f2.set(1150.f, 5.f);
    float padBreath = 0.8f + 0.2f * dsp::sine(s_lfo2);

    for (int i = 0; i < n; i++) {
        if ((int64_t)samples + i >= s_next16) {
            step(s);
            float swing = (s_step & 1) ? 1.12f : 0.88f;   // a little swing on the 16ths
            s_next16 += (int64_t)(SAMPLES_PER_16 * swing);
        }
        float dry = 0.f, rev = 0.f, dly = 0.f;
        // drone
        if (g_drone > 0.002f) {
            float d = 0.f;
            for (int k = 0; k < 3; k++) {
                s_dr[k] += dsp::inc(drHz[k]);
                d += dsp::sine(s_dr[k]) * (0.6f + 0.4f * dsp::sine(s_drAm[k])) * (k == 2 ? 0.5f : 1.f);
            }
            d *= g_drone * 0.05f;
            dry += d * 0.6f; rev += d * 0.7f;
        }
        // pad
        if (g_pad > 0.002f) {
            float p = 0.f;
            for (int v = 0; v < 4; v++) p += s_pad[v].a.tick() + s_pad[v].b.tick();
            p = s_padF.lp(p) * g_pad * 0.028f * padBreath;
            dry += p * 0.5f; rev += p * 0.75f; dly += p * 0.08f;
        }
        // dub stab
        if (s_stab.env > 0.0005f) {
            float x = 0.f;
            for (int k = 0; k < 4; k++) x += s_stab.o[k].tick();
            x = s_stab.f.bp(x) * s_stab.env * 0.11f;
            s_stab.env *= 0.99955f;
            dry += x * 0.3f; dly += x * 0.95f; rev += x * 0.3f;
        }
        // shaker
        if (s_hatEnv > 0.0005f) {
            float w = rng.bi();
            s_hatLP += 0.45f * (w - s_hatLP);                 // a darker shaker: noise between ~4 and 9 kHz
            s_hatBP += 0.5f * ((w - s_hatLP) - s_hatBP);
            float h = s_hatBP * s_hatEnv * 0.045f;
            s_hatEnv *= s_hatDec;
            dry += h * 0.6f; dly += h * 0.12f; rev += h * 0.1f;
        }
        // kick: a soft knock on the speaker; the motor carries the weight
        if (s_kickEnv > 0.001f) {
            s_kickPh += dsp::inc(s_kickHz);
            s_kickHz = fmaxf(52.f, s_kickHz * 0.9993f);
            float k = dsp::sine(s_kickPh) * s_kickEnv * 0.25f + rng.bi() * s_clickEnv * 0.08f;
            s_kickEnv *= 0.9988f;
            s_clickEnv *= 0.93f;
            dry += k;
        }
        // bass
        if (s_bassEnv > 0.001f) {
            float x = dsp::softclip(s_bassF.lp(s_bassO.tick()) * 2.2f) * s_bassEnv * 0.07f;
            s_bassEnv *= 0.9996f;
            dry += x * 0.85f; dly += x * 0.05f;
        }
        // plucks
        for (KS& k : s_ks) {
            if (!k.on) continue;
            int j = k.pos, j1 = j + 1 >= k.len ? 0 : j + 1;
            float y = (k.buf[j] + k.buf[j1]) * k.damp;
            k.buf[j] = y;
            k.pos = j1;
            float x = y * k.amp * 0.22f;
            dry += x * 0.35f; dly += x * 0.65f; rev += x * 0.5f;
            if (fabsf(y) < 0.0004f && k.pos == 0) k.on = false;
        }
        // bells
        for (Bell& bl : s_bell) {
            if (!bl.on) continue;
            float x = 0.f;
            for (int k = 0; k < 3; k++) { bl.ph[k] += bl.dph[k]; x += dsp::sine(bl.ph[k]) * bl.env[k]; bl.env[k] *= bl.dec[k]; }
            x *= 0.12f;
            dry += x * 0.4f; rev += x * 0.8f; dly += x * 0.15f;
            if (bl.env[0] < 0.0005f) bl.on = false;
        }
        // choir: formant 'ah' over the chord
        if (g_choir > 0.002f) {
            s_vib += dsp::inc(5.1f);
            float vib = 1.f + 0.004f * dsp::sine(s_vib);
            float c = 0.f;
            for (int k = 0; k < 3; k++) { s_choir[k].dph = dsp::inc(chHz[k] * vib); c += s_choir[k].tick(); }
            c = (s_f1.bp(c) + 0.7f * s_f2.bp(c)) * g_choir * 0.05f;
            dry += c * 0.3f; rev += c * 0.9f;
        }
        b.dry[i] += dry;
        b.rev[i] += rev;
        b.dly[i] += dly;
    }
}

}
}
