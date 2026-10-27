#pragma once

namespace mantis {
// The Core2 is a little window into the world.
//  * Turning: yaw is rotation about true vertical (gravity), so pitching or rolling the unit never
//    cover render + LCD latency.
void initControls();
void setYaw(float yaw);          // previews / tests
void updateControls(float dt, float &outYaw, float &outSpeed, bool &trying);
float controlsYawRate();
float controlsPitchDeg();     // signed tilt from upright: + tipped forward, - tipped back
float controlsFlickDps();     // how fast the device is pitching right now (a flick)
// Sliding the world back into line on the touch screen: turns the view, and when the finger lifts,
// the correction teaches the drift tracker.
void controlsAlign(float dyawRad);
void controlsAlignDone();
float controlsDriftRate();
float controlsDragSum();      // radians slid so far in the current alignment drag
void controlsTestDrift(float degPerSec);      // tests
void controlsResetDriftLearning();            // tests         // rad/s, for the near-field pivot and tests
}
