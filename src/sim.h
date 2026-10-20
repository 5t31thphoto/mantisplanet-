#pragma once

namespace mantis {
// The Core2 is a little window into the world.
//  * Turning: yaw is rotation about true vertical (gravity), so pitching or rolling the unit never
//    swings the view. 1:1 with your hands, gyro bias learned while still, ~35 ms of prediction to
//    cover render + LCD latency.
//  * Walking: held level you stand still; tip forward to walk (further = faster).
void initControls();
void setYaw(float yaw);          // previews / tests
void updateControls(float dt, float &outYaw, float &outSpeed, bool &trying);
float controlsYawRate();
float controlsPitchDeg();     // signed tilt from upright: + tipped forward, - tipped back
float controlsFlickDps();     // how fast the device is pitching right now (a flick)         // rad/s, for the near-field pivot and tests
}
