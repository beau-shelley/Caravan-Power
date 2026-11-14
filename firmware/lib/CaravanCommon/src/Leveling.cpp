#include "Leveling.h"

#include <cmath>

namespace cv {

namespace {
constexpr float kPi = 3.14159265358979f;
float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
Vec3 scale(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
Vec3 sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
float len(Vec3 a) { return std::sqrt(dot(a, a)); }
bool normalize(Vec3 &a) {
  float l = len(a);
  if (l < 1e-6f) return false;
  a = scale(a, 1.0f / l);
  return true;
}
// Component of v perpendicular to unit vector u.
Vec3 reject(Vec3 v, Vec3 u) { return sub(v, scale(u, dot(v, u))); }

// Default forward: the sensor axis that lies most nearly level, projected flat.
Vec3 defaultForward(Vec3 up) {
  Vec3 f = reject(Vec3{1, 0, 0}, up);
  if (len(f) < 0.3f) f = reject(Vec3{0, 1, 0}, up);  // X axis points (nearly) up
  normalize(f);
  return f;
}
}  // namespace

void levelSetZero(LevelCal &cal, Vec3 a) {
  if (!normalize(a)) return;
  cal.up = a;
  cal.zeroed = true;
  // Keep a learned forward direction, just re-squared to the new "up".
  Vec3 f = reject(cal.fwd, cal.up);
  if (cal.fwdLearned && normalize(f)) cal.fwd = f;
  else {
    cal.fwdLearned = false;
    cal.fwd = defaultForward(cal.up);
  }
}

bool levelLearnFront(LevelCal &cal, Vec3 a) {
  if (!normalize(a)) return false;
  Vec3 d = reject(a, cal.up);  // lifting the front tilts "up" toward forward
  if (len(d) < std::sin(0.5f * kPi / 180.0f)) return false;
  normalize(d);
  cal.fwd = d;
  cal.fwdLearned = true;
  return true;
}

LevelAngles levelCompute(const LevelCal &cal, Vec3 a) {
  Vec3 left = cross(cal.up, cal.fwd);  // x forward, y left, z up (right-handed)
  float au = dot(a, cal.up), af = dot(a, cal.fwd), al = dot(a, left);
  LevelAngles r;
  // Slope along each van axis - exactly what wheel/jockey heights depend on.
  r.pitchDeg = std::atan2(af, au) * 180.0f / kPi;
  r.rollDeg = std::atan2(al, au) * 180.0f / kPi;
  return r;
}

LevelAdvice levelAdvice(const LevelAngles &a, float trackMm, float jockeyMm) {
  LevelAdvice adv;
  float roll = a.rollDeg * kPi / 180.0f, pitch = a.pitchDeg * kPi / 180.0f;
  adv.raiseLeft = roll < 0;  // left side low
  adv.wheelMm = trackMm * std::fabs(std::tan(roll));
  adv.jockeyMm = -jockeyMm * std::tan(pitch);  // nose high -> lower the jockey
  return adv;
}

}  // namespace cv
