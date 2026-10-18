#include "sim.h"
#include <M5Unified.h>
#include <math.h>
#ifndef HOST
#include <Preferences.h>
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
static constexpr float YAW_SIGN = 1.0f;
static constexpr float DEG2RAD = 0.01745329252f;
// The view leads by the real latency: about one frame of render plus the LCD push. Measured from the
// loop's own frame time, smoothed and clamped so one slow frame can't make the view jump.
static float s_dtAvg = 0.033f;

static V3 s_up = {0, 0, 1};          // gravity direction in the device frame (accelerometer sense)
static V3 s_bias = {0, 0, 0};        // gyro bias, deg/s
static V3 s_aLP = {0, 0, 1};         // smoothed accelerometer for the throttle
static bool s_init = false;
static float s_neutralY = 0.f;
static float s_yaw = 0.f, s_rate = 0.f, s_rateSm = 0.f;
static float s_stillT = 0.f, s_learnT = 0.f;
static float s_mean = 0.f, s_act = 0.f, s_handT = 0.f;   // hand-held bias tracking
static V3 s_wPrev = {0, 0, 0};
static float s_jit = 1.f;                                 // sample-to-sample gyro jitter: hands tremble, tables don't
static uint32_t s_savedAt = 0;
static float forwardSpeed = 0.f;
static bool s_trying = false;

static void loadBias() {
#ifndef HOST
    Preferences p;
    if (p.begin("mantisplanet", true)) {
        s_bias = v3(p.getFloat("gbx", 0.f), p.getFloat("gby", 0.f), p.getFloat("gbz", 0.f));
        p.end();
    }
#endif
}
static void saveBias() {
#ifndef HOST
    Preferences p;
    if (p.begin("mantisplanet", false)) { p.putFloat("gbx", s_bias.x); p.putFloat("gby", s_bias.y); p.putFloat("gbz", s_bias.z); p.end(); }
#endif
}

void setYaw(float yaw) { s_yaw = yaw; s_rate = s_rateSm = 0.f; }
float controlsYawRate() { return s_rateSm * DEG2RAD; }

void initControls() {
    s_init = false;
    s_yaw = 0.f; s_rate = s_rateSm = 0.f;
    forwardSpeed = 0.f;
    s_trying = false;
    s_stillT = s_learnT = 0.f;
    s_mean = s_act = s_handT = 0.f;
    s_jit = 1.f;
    s_bias = v3(0.f, 0.f, 0.f);
    loadBias();                                                    // on the device: last session's learned bias
}

void updateControls(float dt, float &outYaw, float &outSpeed, bool &trying) {
    M5.update();

    if (M5.Imu.update()) {
        auto d = M5.Imu.getImuData();
        V3 a = v3(d.accel.x, d.accel.y, d.accel.z);            // g
        V3 w = v3(d.gyro.x, d.gyro.y, d.gyro.z);               // deg/s
        float an = len(a);
        V3 an1 = an > 0.2f ? sc(a, 1.f / an) : s_up;
        if (!s_init) { s_up = an1; s_aLP = an1; s_neutralY = an1.y; s_init = true; }
        V3 wc = sub(w, s_bias);

        // Gravity in the device frame: carried along by the gyro (instant), pulled gently toward the
        // accelerometer (no drift). A vector fixed in the world seen from a turning body: dg = -w x g dt.
        s_up = sub(s_up, cross(sc(wc, DEG2RAD * dt), s_up));
        float trust = fabsf(an - 1.f) < 0.12f ? 0.05f : 0.006f;    // shaking hands: trust the gyro
        s_up = norm(v3(s_up.x + (an1.x - s_up.x) * trust, s_up.y + (an1.y - s_up.y) * trust, s_up.z + (an1.z - s_up.z) * trust));

        // Yaw is rotation about true vertical: pitch and roll can't leak into it, however it's held.
        float rate = dot(wc, s_up) * YAW_SIGN;                     // deg/s

        // Gyro bias: learn it whenever the unit is genuinely still; keep it for next time.
        s_jit += (len(sub(w, s_wPrev)) - s_jit) * 0.2f;
        s_wPrev = w;
        bool still = s_jit < 0.25f && len(wc) < 3.f && fabsf(an - 1.f) < 0.04f && len(sub(an1, s_aLP)) < 0.02f;
        s_stillT = still ? s_stillT + dt : 0.f;
        if (s_stillT > 0.8f) {
            float k = fminf(1.f, dt * 0.7f);
            s_bias = v3(s_bias.x + (w.x - s_bias.x) * k, s_bias.y + (w.y - s_bias.y) * k, s_bias.z + (w.z - s_bias.z) * k);
            s_learnT += dt;
            if (s_learnT > 6.f && millis() - s_savedAt > 120000u) { saveBias(); s_savedAt = millis(); s_learnT = 0.f; }
        }
        // In the hand it's never truly still, but tremor averages to zero over a couple of seconds and
        // bias doesn't. When the vertical rate has averaged under 1 deg/s with only tremor-sized jitter,
        // that average is bias: fold it out along the vertical axis, slowly.
        s_mean += (rate - s_mean) * fminf(1.f, dt / 2.f);
        s_act += (fabsf(rate - s_mean) - s_act) * fminf(1.f, dt / 2.f);
        s_handT = (fabsf(s_mean) < 1.f && s_act < 4.f) ? s_handT + dt : 0.f;
        if (s_handT > 2.f && s_stillT <= 0.8f) {
            float k = s_mean * YAW_SIGN * fminf(1.f, dt / 6.f);
            s_bias = v3(s_bias.x + s_up.x * k, s_bias.y + s_up.y * k, s_bias.z + s_up.z * k);
        }
        // soft zero-rate band: residual creep below ~0.3 deg/s is ignored, deliberate turns pass 1:1
        float ar = fabsf(rate);
        rate *= fminf(1.f, fmaxf(0.f, (ar - 0.15f) / 0.35f));
        s_rate = rate;
        s_yaw += rate * DEG2RAD * dt;
        s_rateSm += (rate - s_rateSm) * 0.35f;

        // Throttle: tip forward relative to how you held it at the start.
        s_aLP = norm(v3(s_aLP.x + (an1.x - s_aLP.x) * 0.35f, s_aLP.y + (an1.y - s_aLP.y) * 0.35f, s_aLP.z + (an1.z - s_aLP.z) * 0.35f));
        float pitchTip = s_aLP.y - s_neutralY;
        float targetSpeed = pitchTip > 0.06f ? (pitchTip - 0.06f) * 2.6f : 0.f;
        if (targetSpeed > 1.1f) targetSpeed = 1.1f;
        s_trying = targetSpeed > 0.02f;
        forwardSpeed = forwardSpeed * 0.86f + targetSpeed * 0.14f;
        if (!s_trying && forwardSpeed < 0.03f) forwardSpeed = 0.f;
    }

    s_dtAvg += (fminf(dt, 0.1f) - s_dtAvg) * 0.05f;
    float predict = fminf(0.1f, fmaxf(0.03f, s_dtAvg + 0.015f));
    outYaw = s_yaw + s_rateSm * DEG2RAD * predict;                 // lead the latency
    outSpeed = forwardSpeed;
    trying = s_trying;
}

}
