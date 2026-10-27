#include "sim.h"
#include <M5Unified.h>
#include <math.h>
#include <string.h>
#ifndef HOST
#include <Preferences.h>
#include <utility/imu/MPU6886_Class.hpp>
#endif

namespace mantis {

struct V3 { float x, y, z; };
static inline V3 v3(float x, float y, float z) { V3 v = {x, y, z}; return v; }
static inline V3 sub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline V3 sc(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline V3 cross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static inline float len(V3 a) { return sqrtf(dot(a, a)); }
static inline V3 norm(V3 a) { float l = len(a); return l > 1e-6f ? sc(a, 1.f / l) : v3(0, 0, 1); }

// Flip if turning the unit pans the panorama the wrong way.
static constexpr float YAW_SIGN = -1.0f;
static constexpr float DEG2RAD = 0.01745329252f;
// The view leads by the real latency: about one frame of render plus the LCD push. Measured from the
// loop's own frame time, smoothed and clamped so one slow frame can't make the view jump.
static float s_dtAvg = 0.033f;

static V3 s_up = {0, 0, 1};          // gravity direction in the device frame (accelerometer sense)
static V3 s_bias = {0, 0, 0};        // gyro bias, deg/s
static V3 s_aLP = {0, 0, 1};         // smoothed accelerometer for the throttle
static bool s_init = false;
static uint32_t s_lastImuUs = 0;
static float s_yaw = 0.f, s_rate = 0.f, s_rateSm = 0.f;
static float s_learnT = 0.f;
static uint32_t s_savedAt = 0;
// Every gyro sample, not one per frame: the IMU's FIFO keeps them all (100 Hz) and each frame drains it, so
// the hand's sway and tremor integrate exactly instead of being point-sampled once a frame.
static bool s_fifo = false;
static constexpr int FIFO_DIV = 9;                        // 1 kHz / (1 + 9) = 100 Hz
static float s_sampleDt = 1.f / 100.f;                    // refined from the measured rate (the chip's clock is +-1%)
static uint32_t s_rateT0 = 0, s_rateN = 0;
// Bias learning from windows of samples: the spread (variance) of the gyro and accelerometer says how still
// the unit is, not the gyro's size (which is the bias itself). TABLE: truly at rest, 1 s. HAND: held calmly,
struct Win { float t; int n; V3 gm, gm2, am, am2; float an, vm, vm2; };
static Win s_win;
static float s_biasUnc = 1.f;                             // how unsure the bias is (deg/s): sets how far a hand reading may disagree
// The bias follows the chip's temperature (it warms as it runs): learned values are kept per degree.
static constexpr int TBINS = 60, TBASE = 10;              // 10..69 C
struct TBin { V3 b; float w; };
static TBin s_tab[TBINS];
static float s_tempC = -100.f, s_tempLearnC = -100.f, s_tempT = 0.f;
static float s_fwdSign = 1.f, s_fwdLearn = 0.f;
static float s_pitchDeg = 0.f, s_flickDps = 0.f;
// that got past the gyro bias learners. It's applied only while the gyro is integrating (moving), at
static float s_userRate = 0.f;      // deg/s added to the heading while integrating
static float s_activeT = 0.f;       // integrating seconds since the last alignment
static float s_dragSum = 0.f;       // radians slid during the current drag
static float s_testDrift = 0.f;     // tests: extra drift that the learners can't see
static float forwardSpeed = 0.f;
static bool s_trying = false;

static void loadBias() {
#ifndef HOST
    Preferences p;
    if (p.begin("mantisplanet", true)) {
        s_bias = v3(p.getFloat("gbx", 0.f), p.getFloat("gby", 0.f), p.getFloat("gbz", 0.f));
        s_fwdSign = p.getFloat("fwd", 1.f) < 0.f ? -1.f : 1.f;
        s_userRate = p.getFloat("udrift", 0.f);
        if (!(fabsf(s_userRate) <= 1.5f)) s_userRate = 0.f;
        if (p.getBytesLength("btab") == sizeof s_tab) p.getBytes("btab", s_tab, sizeof s_tab);
        p.end();
    }
#endif
}
static void saveFwd() {
#ifndef HOST
    Preferences p;
    if (p.begin("mantisplanet", false)) { p.putFloat("fwd", s_fwdSign); p.end(); }
#endif
}
static void saveBias() {
#ifndef HOST
    Preferences p;
    if (p.begin("mantisplanet", false)) { p.putFloat("gbx", s_bias.x); p.putFloat("gby", s_bias.y); p.putFloat("gbz", s_bias.z); p.putBytes("btab", s_tab, sizeof s_tab); p.end(); }
#endif
}

void setYaw(float yaw) { s_yaw = yaw; s_rate = s_rateSm = 0.f; }
float controlsYawRate() { return s_rateSm * DEG2RAD; }
float controlsPitchDeg() { return s_pitchDeg; }

static void saveUserRate() {
#ifndef HOST
    Preferences p;
    if (p.begin("mantisplanet", false)) { p.putFloat("udrift", s_userRate); p.end(); }
#endif
}
void controlsAlign(float dyaw) { s_yaw += dyaw; s_dragSum += dyaw; }
void controlsAlignDone() {
    float d = s_dragSum;
    s_dragSum = 0.f;
    if (fabsf(d) < 0.002f) return;
    // only a plausible drift over a meaningful stretch teaches the tracker; anything else just moved the view
    if (s_activeT >= 30.f) {
        float implied = d * 57.29578f / s_activeT;                  // deg/s that went uncorrected
        if (fabsf(implied) <= 1.f) {
            s_userRate = fmaxf(-1.5f, fminf(1.5f, s_userRate + 0.6f * implied));
            saveUserRate();
        }
    }
    s_activeT = 0.f;
}
float controlsDriftRate() { return s_userRate; }
float controlsDragSum() { return s_dragSum; }
void controlsTestDrift(float degPerSec) { s_testDrift = degPerSec; }
void controlsResetDriftLearning() { s_userRate = 0.f; s_activeT = 0.f; s_dragSum = 0.f; }
float controlsFlickDps() { return s_flickDps; }

void initControls() {
    s_init = false;
    s_yaw = 0.f; s_rate = s_rateSm = 0.f;
    forwardSpeed = 0.f;
    s_trying = false;
    s_learnT = 0.f;
    memset(&s_win, 0, sizeof s_win);
    memset(s_tab, 0, sizeof s_tab);
    s_tempC = s_tempLearnC = -100.f; s_tempT = 0.f;
    s_lastImuUs = 0;
    s_bias = v3(0.f, 0.f, 0.f);
    s_biasUnc = 1.f;
    loadBias();                                                    // on the device: last session's learned bias (and its table)
#ifndef HOST
    // the FIFO, through M5Unified's own MPU6886 driver (so its scaling and the Core2's axis mapping still apply)
    s_fifo = false;
    if (M5.Imu.getType() == m5::imu_t::imu_mpu6886) {
        if (auto* imu = static_cast<m5::MPU6886_Class*>(M5.Imu.getImuInstancePtr(0))) {
            imu->enableFIFO((m5::MPU6886_Class::Fodr)FIFO_DIV);
            s_fifo = true;
        }
    }
#endif
    s_sampleDt = 1.f / 100.f; s_rateT0 = 0; s_rateN = 0;
}

static constexpr float REF_DT = 0.033333f;
static inline float perDt(float kRef, float dt) { return 1.f - powf(1.f - kRef, dt / REF_DT); }

static void tableLearn(V3 z) {
    if (s_tempC < TBASE || s_tempC >= TBASE + TBINS) return;
    TBin& b = s_tab[(int)(s_tempC - TBASE)];
    if (b.w <= 0.f) { b.b = z; b.w = 1.f; }
    else { b.b = v3(b.b.x + (z.x - b.b.x) * 0.3f, b.b.y + (z.y - b.b.y) * 0.3f, b.b.z + (z.z - b.b.z) * 0.3f); b.w = fminf(b.w + 1.f, 20.f); }
    s_tempLearnC = s_tempC;
}
static void learnBias(V3 z, float k) {
    s_bias = v3(s_bias.x + (z.x - s_bias.x) * k, s_bias.y + (z.y - s_bias.y) * k, s_bias.z + (z.z - s_bias.z) * k);
    tableLearn(s_bias);
}
static inline float maxStd(V3 m2, int n) { return n > 1 ? sqrtf(fmaxf(m2.x, fmaxf(m2.y, m2.z)) / (n - 1)) : 1e9f; }
static void windowAdd(V3 w, V3 a, float an, float dt) {
    Win& W = s_win;
    W.n++; W.t += dt;
    float inv = 1.f / W.n;
    float vr = dot(sub(w, s_bias), s_up);                          // the vertical rate, bias removed
    float dv = vr - W.vm; W.vm += dv * inv; W.vm2 += dv * (vr - W.vm);
    V3 d = sub(w, W.gm); W.gm = v3(W.gm.x + d.x * inv, W.gm.y + d.y * inv, W.gm.z + d.z * inv);
    V3 d2 = sub(w, W.gm); W.gm2 = v3(W.gm2.x + d.x * d2.x, W.gm2.y + d.y * d2.y, W.gm2.z + d.z * d2.z);
    V3 e = sub(a, W.am); W.am = v3(W.am.x + e.x * inv, W.am.y + e.y * inv, W.am.z + e.z * inv);
    V3 e2 = sub(a, W.am); W.am2 = v3(W.am2.x + e.x * e2.x, W.am2.y + e.y * e2.y, W.am2.z + e.z * e2.z);
    W.an += (an - W.an) * inv;
    bool level = fabsf(W.an - 1.f) < 0.05f;
    if (W.t >= 1.f && W.t - dt < 1.f) {                           // one second: truly at rest?
        if (level && maxStd(W.gm2, W.n) < 0.15f && maxStd(W.am2, W.n) < 0.004f && len(sub(W.gm, s_bias)) < 3.f) {   // still, and not a steady turn
            learnBias(W.gm, 0.5f);
            s_biasUnc = 0.05f;
            s_learnT += W.t;
            memset(&W, 0, sizeof W);
            return;
        }
    }
    if (W.t >= 2.f) {
        // and the leftover vertical rate is small, that leftover is bias: fold it out along the vertical.
        // How small: up to 1 deg/s while the bias is uncertain, tightening to 0.3 deg/s once it's well known,
        // so a slow deliberate turn can't be taken for bias.
        float vstd = W.n > 1 ? sqrtf(W.vm2 / (W.n - 1)) : 1e9f;
        // the gate allows for the window's own noise (tremor averaged over n samples): a trembling hand's leftover
        // bias gets through, a smooth slow turn (tiny noise) still doesn't
        float gate = fminf(1.f, fmaxf(0.3f, 3.f * s_biasUnc)) + 3.f * vstd / sqrtf((float)W.n);
        if (fabsf(W.an - 1.f) < 0.12f && vstd < 4.f && maxStd(W.am2, W.n) < 0.05f && fabsf(W.vm) < gate) {
            float k = W.vm * 0.7f;
            s_bias = v3(s_bias.x + s_up.x * k, s_bias.y + s_up.y * k, s_bias.z + s_up.z * k);
            tableLearn(s_bias);
            s_biasUnc = fmaxf(0.1f, s_biasUnc * 0.75f);
        }
        memset(&W, 0, sizeof W);
    }
    s_biasUnc = fminf(1.f, s_biasUnc + dt * 0.0005f);              // confidence fades slowly (~0.03 deg/s a minute)
}

static void tempFollow(float dt) {
    if (s_tempC < TBASE || s_tempC >= TBASE + TBINS || fabsf(s_tempC - s_tempLearnC) < 1.f) return;
    int c = (int)(s_tempC - TBASE);
    for (int r = 0; r <= 3; r++)
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            int i = c + r * sgn;
            if (i < 0 || i >= TBINS || s_tab[i].w <= 0.f) continue;
            float k = fminf(1.f, dt * 0.05f);
            s_bias = v3(s_bias.x + (s_tab[i].b.x - s_bias.x) * k, s_bias.y + (s_tab[i].b.y - s_bias.y) * k, s_bias.z + (s_tab[i].b.z - s_bias.z) * k);
            return;
        }
}

// One gyro/accelerometer sample: carry gravity, take the yaw rate about true vertical, learn the bias,
static void imuSample(V3 a, V3 w, float dt) {
    float an = len(a);
    V3 an1 = an > 0.2f ? sc(a, 1.f / an) : s_up;
    if (!s_init) { s_up = an1; s_aLP = an1; s_init = true; }
    s_flickDps = fabsf(w.x - s_bias.x);
    V3 wc = sub(w, s_bias);
    // accelerometer (no drift). A vector fixed in the world seen from a turning body: dg = -w x g dt.
    s_up = sub(s_up, cross(sc(wc, DEG2RAD * dt), s_up));
    float trust = perDt(fabsf(an - 1.f) < 0.12f ? 0.05f : 0.006f, dt);   // shaking hands: trust the gyro
    s_up = norm(v3(s_up.x + (an1.x - s_up.x) * trust, s_up.y + (an1.y - s_up.y) * trust, s_up.z + (an1.z - s_up.z) * trust));
    // Yaw is rotation about true vertical: pitch and roll can't leak into it, however it's held.
    float rate = dot(wc, s_up) * YAW_SIGN;                         // deg/s
    windowAdd(w, a, an, dt);
    // soft zero-rate band: residual creep below ~0.3 deg/s is ignored, deliberate turns pass 1:1
    float ar = fabsf(rate);
    float active = fminf(1.f, fmaxf(0.f, (ar - 0.15f) / 0.35f));
    rate *= active;
    s_rate = rate;
    s_yaw += (rate + (s_userRate + s_testDrift) * active) * DEG2RAD * dt;   // the hand-measured drift, while integrating
    s_activeT += active * dt;
    s_rateSm += (rate - s_rateSm) * perDt(0.35f, dt);
    s_aLP = norm(v3(s_aLP.x + (an1.x - s_aLP.x) * perDt(0.35f, dt), s_aLP.y + (an1.y - s_aLP.y) * perDt(0.35f, dt), s_aLP.z + (an1.z - s_aLP.z) * perDt(0.35f, dt)));
}

void updateControls(float dt, float &outYaw, float &outSpeed, bool &trying) {
    M5.update();

    bool fresh = false;
    if (s_fifo) {                                                   // every sample since the last frame
        for (int k = 0; k < 24 && M5.Imu.update(); k++) {
            auto d = M5.Imu.getImuData();
            imuSample(v3(d.accel.x, d.accel.y, d.accel.z), v3(d.gyro.x, d.gyro.y, d.gyro.z), s_sampleDt);
            fresh = true;
            s_rateN++;
        }
        uint32_t nowUs = micros();                                  // the chip's true sample rate
        if (!s_rateT0) s_rateT0 = nowUs;
        else if (nowUs - s_rateT0 > 20000000u && s_rateN > 500) {
            float measured = (nowUs - s_rateT0) * 1e-6f / s_rateN;
            if (measured > 0.008f && measured < 0.012f) s_sampleDt += (measured - s_sampleDt) * 0.5f;
            s_rateT0 = nowUs; s_rateN = 0;
        }
    } else if (M5.Imu.update()) {                                   // no FIFO: one sample, over the real time since the last
        auto d = M5.Imu.getImuData();
        uint32_t nowUs = micros();
        float idt = s_lastImuUs ? (nowUs - s_lastImuUs) * 1e-6f : dt;
        s_lastImuUs = nowUs;
        if (idt > 0.1f) idt = 0.1f;
        imuSample(v3(d.accel.x, d.accel.y, d.accel.z), v3(d.gyro.x, d.gyro.y, d.gyro.z), idt);
        fresh = true;
    }
    s_tempT += dt;
    if (s_tempT >= 1.f) {                                           // the chip's temperature, once a second
        s_tempT = 0.f;
        float t;
        if (M5.Imu.getTemp(&t) && t > -40.f && t < 120.f) s_tempC = t;
    }
    tempFollow(dt);
    // flash writes pause both cores (a click in the audio): save once when learned, then at most every 30 min
    if (s_learnT > 6.f && (s_savedAt == 0 || millis() - s_savedAt > 1800000u)) { saveBias(); s_savedAt = millis() | 1u; s_learnT = 0.f; }
    if (fresh) {
        float tilt = asinf(fminf(1.f, fabsf(s_aLP.z))) * 57.29578f;       // screen angle from vertical
        float targetSpeed = (tilt > 20.f && tilt < 72.f) ? (tilt - 20.f) / 25.f : 0.f;
        if (targetSpeed > 0.3f) {                                   // learn which way is forward from walking
            s_fwdLearn += dt;
            if (s_fwdLearn > 1.5f) { float sg = s_aLP.z >= 0.f ? 1.f : -1.f; if (sg != s_fwdSign) { s_fwdSign = sg; saveFwd(); } s_fwdLearn = -1e9f; }
        }
        s_pitchDeg = asinf(fmaxf(-1.f, fminf(1.f, s_aLP.z * s_fwdSign))) * 57.29578f;   // + forward, - back
        if (targetSpeed > 1.1f) targetSpeed = 1.1f;
        s_trying = targetSpeed > 0.02f;
        forwardSpeed += (targetSpeed - forwardSpeed) * (1.f - powf(0.86f, dt / 0.05f));
        if (!s_trying && forwardSpeed < 0.03f) forwardSpeed = 0.f;
    }

    s_dtAvg += (fminf(dt, 0.1f) - s_dtAvg) * 0.05f;
    float predict = fminf(0.1f, fmaxf(0.03f, s_dtAvg + 0.015f));
    outYaw = s_yaw + s_rateSm * DEG2RAD * predict;                 // lead the latency
    outSpeed = forwardSpeed;
    trying = s_trying;
}

}
