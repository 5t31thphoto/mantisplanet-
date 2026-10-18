#include "audio.h"
#include "audio_int.h"
#include <string.h>
#ifndef HOST
#include <M5Unified.h>
#endif

namespace mantis {
namespace snd {
dsp::Rng rng;
uint32_t samples = 0;
}
using namespace snd;

// ---------------- game -> audio events: single producer, single consumer ----------------
enum : uint8_t { EV_STEP, EV_BUMP, EV_THUNDER, EV_CRITTER, EV_MOMENT };
struct Ev { uint8_t type, a; float x, y, z; };
static Ev s_q[32];
static volatile uint32_t s_head = 0, s_tail = 0;

static void post(const Ev& e) {
    uint32_t h = __atomic_load_n(&s_head, __ATOMIC_RELAXED);
    uint32_t n = (h + 1) & 31u;
    if (n == __atomic_load_n(&s_tail, __ATOMIC_ACQUIRE)) return;    // full: drop, never block the game
    s_q[h] = e;
    __atomic_store_n(&s_head, n, __ATOMIC_RELEASE);
}
static void drain() {
    uint32_t t = __atomic_load_n(&s_tail, __ATOMIC_RELAXED);
    while (t != __atomic_load_n(&s_head, __ATOMIC_ACQUIRE)) {
        const Ev& e = s_q[t];
        switch (e.type) {
            case EV_STEP:    natureFootstep(e.a, e.x); break;
            case EV_BUMP:    natureBump(e.a); break;
            case EV_THUNDER: natureThunder(e.x, e.y, e.z); break;
            case EV_CRITTER: natureCritter(e.a, e.x); break;
            case EV_MOMENT:  musicMoment(e.a); break;
        }
        t = (t + 1) & 31u;
        __atomic_store_n(&s_tail, t, __ATOMIC_RELEASE);
    }
}

void sfxFootstep(uint8_t surface, float strength) { post({EV_STEP, surface, strength, 0, 0}); }
void sfxBump(uint8_t kind) { post({EV_BUMP, kind, 0, 0, 0}); }
void sfxThunder(float delay, float power, float close) { post({EV_THUNDER, 0, delay, power, close}); }
void sfxCritter(uint8_t kind, float dist) { post({EV_CRITTER, kind, dist, 0, 0}); }
void sfxMoment(uint8_t kind) { post({EV_MOMENT, kind, 0, 0, 0}); }

// ---------------- master bus ----------------
static dsp::Reverb s_rev;
static dsp::DubDelay s_dly;
static Bus s_bus;
static float s_env = 0.f, s_gain = 1.f;
static dsp::SVF s_hp;
static volatile float s_kick = 0.f, s_groove = 0.f;
static volatile bool s_muted = false;
static float s_out = 1.f;          // speaker gain, ramped so muting never clicks
static const int LATENCY = BLOCK * 2 + 600;     // queued block + the speaker's own buffers

float audioBeats() {
    int32_t heard = (int32_t)samples - LATENCY;
    if (heard < 0) heard = 0;
    return heard * (MUSIC_BPM / 60.f) / AUDIO_SR;
}
float audioKick() { return s_kick; }
float audioGroove() { return s_groove; }
void audioSetMuted(bool m) { s_muted = m; }
bool audioMuted() { return s_muted; }

void audioRender(int16_t* out, int n) {
    if (n > BLOCK) n = BLOCK;
    drain();
    memset(&s_bus, 0, sizeof(s_bus));
    const Scene& sc = g_scene;
    const float outTarget = s_muted ? 0.f : 1.f;
    natureBlock(s_bus, n, sc);
    musicBlock(s_bus, n, sc);
    s_kick = musicKick();
    s_groove = musicGroove();
    s_dly.target = musicDelaySamples();
    // underwater dark when it storms, open and airy otherwise
    s_rev.fb = 0.80f + 0.05f * sc.fog + 0.04f * sc.awe;
    s_hp.set(165.f, 0.72f);    // the Core2 speaker can't move air down there: spend the headroom where it sings
    for (int i = 0; i < n; i++) {
        float d = s_dly.tick(s_bus.dly[i]);
        float r = s_rev.tick(s_bus.rev[i] + d * 0.3f);
        float x = s_bus.dry[i] + d * 0.85f + r * 0.9f;
        x = s_hp.hp(x) * 1.25f;
        float a = fabsf(x);
        s_env = a > s_env ? s_env + (a - s_env) * 0.25f : s_env * 0.99985f;
        float g = s_env > 0.62f ? 0.62f / s_env : 1.f;
        s_gain += (g - s_gain) * (g < s_gain ? 0.2f : 0.0004f);
        x = dsp::softclip(x * s_gain * 1.05f);
        s_out += (outTarget - s_out) * 0.002f;          // ~20 ms fade
        out[i] = (int16_t)(x * s_out * 29000.f);
    }
    samples += n;
}

#ifndef HOST
static void audioTask(void*) {
    static int16_t bufs[3][BLOCK];
    int k = 0;
    const int CH = 0;
    for (;;) {
        if (M5.Speaker.isPlaying(CH) < 2) {
            audioRender(bufs[k], BLOCK);
            M5.Speaker.playRaw(bufs[k], BLOCK, AUDIO_SR, false, 1, CH, false);
            k = (k + 1) % 3;
        } else {
            vTaskDelay(1);
        }
    }
}
#endif

void audioBegin() {
    dsp::init();
    rng.s = 0x2545F491u;
    s_rev.begin();
    s_dly.begin(12000);
    natureInit();
    musicInit();
#ifndef HOST
    if (!M5.Speaker.isEnabled()) M5.Speaker.begin();
    M5.Speaker.setVolume(165);
    xTaskCreatePinnedToCore(audioTask, "audio", 6144, nullptr, 3, nullptr, 0);
#endif
}

}
