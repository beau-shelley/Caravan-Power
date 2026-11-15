// Caravan levelling maths for an accelerometer (MPU-6050) that is NOT mounted
// square to the van.
//
// Calibration, stored permanently:
//  1. "Set level"   - van on known-level ground: the measured gravity
//                     direction becomes the van's "up".
//  2. "Learn front" - optional: lift the front (jockey wheel) a little: the
//                     direction the reading moved becomes the van's "forward".
//                     Without it, the sensor's X axis is assumed to point
//                     roughly forward (towards the hitch).
// After that any mounting angle works. Portable: unit tested on the host.
#pragma once
#include <cstdint>

namespace cv {

struct Vec3 {
  float x = 0, y = 0, z = 0;
  constexpr Vec3(float x_ = 0, float y_ = 0, float z_ = 0) : x(x_), y(y_), z(z_) {}
};

struct LevelCal {
  bool zeroed = false;
  Vec3 up{0, 0, 1};      // van "up" in sensor coordinates (unit)
  bool fwdLearned = false;
  Vec3 fwd{1, 0, 0};     // van "forward" in sensor coordinates (unit, perpendicular to up)
};

// pitchDeg + = front (hitch) high; rollDeg + = left (door side) high.
struct LevelAngles {
  float pitchDeg = 0, rollDeg = 0;
};

// Height corrections to level the van (mm).
struct LevelAdvice {
  bool raiseLeft = false;  // which wheel to put on ramps / chocks
  float wheelMm = 0;       // how much to raise that wheel
  float jockeyMm = 0;      // + raise the jockey wheel, - lower it
};

void levelSetZero(LevelCal &cal, Vec3 accel);
// Returns false (calibration unchanged) if the front wasn't lifted enough (< 0.5 deg).
bool levelLearnFront(LevelCal &cal, Vec3 accelFrontRaised);
LevelAngles levelCompute(const LevelCal &cal, Vec3 accel);
// trackMm: distance between the wheels; jockeyMm: axle to jockey wheel.
LevelAdvice levelAdvice(const LevelAngles &a, float trackMm, float jockeyMm);

}  // namespace cv
