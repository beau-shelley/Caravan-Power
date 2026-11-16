#include "TheftGuard.h"

#include <cmath>

namespace cv {

namespace {
constexpr float kRad2Deg = 57.29578f;
constexpr float kTwoPi = 6.2831853f;
constexpr float kSlowTauSec = 5.0f;    // tilt average: longer than any gust
constexpr float kMidTauSec = 1.0f;     // vibration reference
constexpr float kYawLeakSec = 60.0f;   // turning is remembered for about a minute
constexpr float kBiasTauSec = 120.0f;  // gyro bias tracking while still
constexpr float kStillDps = 1.0f;      // below this the van counts as still for bias tracking
constexpr float kBaseDriftSec = 1800;  // resting tilt follows slow drift (temperature, tyres)

float dot(const Vec3 &a, const Vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float len(const Vec3 &a) { return std::sqrt(dot(a, a)); }
Vec3 norm(const Vec3 &a) {
  float l = len(a);
  return l > 1e-6f ? Vec3{a.x / l, a.y / l, a.z / l} : Vec3{0, 0, 1};
}
void lp(Vec3 &f, const Vec3 &x, float k) {
  f.x += (x.x - f.x) * k;
  f.y += (x.y - f.y) * k;
  f.z += (x.z - f.z) * k;
}
float angleDeg(const Vec3 &a, const Vec3 &b) {
  float c = dot(norm(a), norm(b));
  c = c > 1 ? 1 : c < -1 ? -1 : c;
  return std::acos(c) * kRad2Deg;
}
}  // namespace

const char *secureModeText(uint8_t m) {
  switch (m) {
    case SEC_ARMING: return "Arming";
    case SEC_ARMED: return "Armed";
    case SEC_TRIGGERED: return "ALARM";
    default: return "Off";
  }
}

const char *secureCauseText(uint8_t c) {
  switch (c) {
    case SEC_TILT: return "van tilted (jacked or hitched?)";
    case SEC_TURN: return "van turned (being towed?)";
    case SEC_SHAKE: return "van shaking like it's on the road";
    case SEC_SENSOR: return "movement sensor stopped answering";
    default: return "";
  }
}

const char *secureSensText(uint8_t s) {
  switch (s) {
    case SENS_LOW: return "Low";
    case SENS_HIGH: return "High";
    default: return "Medium";
  }
}

GuardLimits guardLimits(uint8_t sens) {
  switch (sens) {
    case SENS_LOW: return {3.0f, 15, 30, 60, 50};   // very windy sites
    case SENS_HIGH: return {1.2f, 6, 12, 25, 30};
    default: return {2.0f, 10, 20, 40, 45};
  }
}

void TheftGuard::setSensitivity(uint8_t sens) {
  sens_ = sens > SENS_HIGH ? (uint8_t)SENS_MEDIUM : sens;
  lim_ = guardLimits(sens_);
}

void TheftGuard::setMode(uint8_t m, uint32_t nowMs) {
  mode_ = m;
  modeSinceMs_ = nowMs;
}

void TheftGuard::arm(uint32_t nowMs) {
  keepBase_ = false;
  haveBase_ = false;
  armAtMs_ = nowMs + ARM_DELAY_MS;
  cause_ = SEC_NONE;
  bx_ = by_ = bz_ = 0;
  biasN_ = 0;
  sensorBad_ = false;
  setMode(SEC_ARMING, nowMs);
}

void TheftGuard::resume(const Vec3 &baseline, uint32_t nowMs) {
  arm(nowMs);
  armAtMs_ = nowMs + RESUME_DELAY_MS;
  base_ = norm(baseline);
  haveBase_ = keepBase_ = true;
}

void TheftGuard::disarm() {
  mode_ = SEC_OFF;
  cause_ = SEC_NONE;
  keepBase_ = haveBase_ = false;
  tilt_ = yaw_ = 0;
  tiltOverMs_ = 0;
  shakeBits_ = 0;
}

void TheftGuard::startWatching(uint32_t nowMs) {
  if (!keepBase_ || !haveBase_) base_ = norm(gSlow_);
  haveBase_ = true;
  keepBase_ = false;
  if (biasN_) {
    bias_ = {(float)(bx_ / biasN_), (float)(by_ / biasN_), (float)(bz_ / biasN_)};
    haveBias_ = true;
  }
  yaw_ = 0;
  tiltOverMs_ = 0;
  shakeBits_ = 0;
  setMode(SEC_ARMED, nowMs);
}

void TheftGuard::trigger(uint8_t cause, uint32_t nowMs) {
  if (mode_ != SEC_ARMED) return;
  cause_ = cause;
  setMode(SEC_TRIGGERED, nowMs);
}

void TheftGuard::sample(const Vec3 &a, const Vec3 &g, uint32_t nowMs) {
  float dt = 0.01f;
  if (!primed_) {
    gSlow_ = gMid_ = a;
    primed_ = true;
    vibSecStartMs_ = nowMs;
  } else {
    dt = (nowMs - lastSampleMs_) / 1000.0f;
    if (dt <= 0) dt = 0.001f;
    if (dt > 0.1f) dt = 0.1f;  // after a gap, don't over-integrate
  }
  lastSampleMs_ = nowMs;
  lp(gSlow_, a, dt / kSlowTauSec);
  lp(gMid_, a, dt / kMidTauSec);

  Vec3 d{a.x - gMid_.x, a.y - gMid_.y, a.z - gMid_.z};
  vibSum_ += dot(d, d);
  vibN_++;

  // Gyro bias: average the last 5 s of the exit delay (everyone's gone by then).
  if (mode_ == SEC_ARMING && (int32_t)(nowMs - (armAtMs_ - RESUME_DELAY_MS)) >= 0) {
    bx_ += g.x;
    by_ += g.y;
    bz_ += g.z;
    biasN_++;
  }

  if (mode_ != SEC_ARMED && mode_ != SEC_TRIGGERED) return;

  // ---- tilt from where it was armed, held for a while
  tilt_ = angleDeg(gSlow_, base_);
  if (tilt_ > lim_.tiltDeg) tiltOverMs_ += dt * 1000;
  else tiltOverMs_ = 0;
  if (tiltOverMs_ >= lim_.tiltHoldSec * 1000) trigger(SEC_TILT, nowMs);

  // ---- turning about the van's vertical (wind can't do this)
  Vec3 r = haveBias_ ? Vec3{g.x - bias_.x, g.y - bias_.y, g.z - bias_.z} : g;
  if (haveBias_ && len(r) < kStillDps) lp(bias_, g, dt / kBiasTauSec);
  yaw_ += dot(r, base_) * dt;
  yaw_ -= yaw_ * (dt / kYawLeakSec);
  if (std::fabs(yaw_) > lim_.turnDeg) trigger(SEC_TURN, nowMs);
}

int TheftGuard::shakeCount() const {
  uint64_t b = shakeBits_ & ((1ULL << 60) - 1);
  int n = 0;
  while (b) {
    n += b & 1;
    b >>= 1;
  }
  return n;
}

bool TheftGuard::tick(uint32_t nowMs, bool sensorOk) {
  uint8_t m0 = mode_, c0 = cause_;
  float dt = lastTickMs_ ? (nowMs - lastTickMs_) / 1000.0f : 0;
  lastTickMs_ = nowMs;
  sensorOk_ = sensorOk && primed_ && nowMs - lastSampleMs_ < 1000;

  if (mode_ == SEC_ARMING && (int32_t)(nowMs - armAtMs_) >= 0) {
    if (sensorOk_) startWatching(nowMs);
    else {
      cause_ = SEC_SENSOR;  // can't watch anything without the sensor
      setMode(SEC_TRIGGERED, nowMs);
    }
  }

  // Sensor watchdog.
  if (mode_ == SEC_ARMED && !sensorOk_) {
    if (!sensorBad_) {
      sensorBad_ = true;
      sensorBadSinceMs_ = nowMs;
    }
    if (nowMs - sensorBadSinceMs_ >= SENSOR_LOST_MS) trigger(SEC_SENSOR, nowMs);
  } else {
    sensorBad_ = false;
  }

  // Vibration, one bit per second.
  if (nowMs - vibSecStartMs_ >= 1000) {
    float rmsMg = vibN_ ? (float)std::sqrt(vibSum_ / vibN_) * 1000 : 0;
    lastRmsMg_ = rmsMg > 255 ? 255 : (uint8_t)rmsMg;
    if (mode_ == SEC_ARMED || mode_ == SEC_TRIGGERED) shakeBits_ = (shakeBits_ << 1) | (rmsMg > lim_.shakeMg ? 1 : 0);
    vibSum_ = 0;
    vibN_ = 0;
    vibSecStartMs_ = nowMs;
    if (shakeCount() >= lim_.shakeSecs) trigger(SEC_SHAKE, nowMs);
  }

  // Let the resting tilt follow very slow drift (sensor temperature, tyres
  // going down), but only while it's well inside the limit.
  if (mode_ == SEC_ARMED && dt > 0 && tilt_ < lim_.tiltDeg / 2) {
    lp(base_, norm(gSlow_), dt / kBaseDriftSec);
    base_ = norm(base_);
  }
  return mode_ != m0 || cause_ != c0;
}

void TheftGuard::fill(Secure &o, uint32_t nowMs) const {
  o.mode = mode_;
  o.cause = cause_;
  o.sens = sens_;
  o.sensor = sensorOk_;
  o.armLeft = 0;
  if (mode_ == SEC_ARMING) {
    int32_t left = (int32_t)(armAtMs_ - nowMs);
    o.armLeft = left > 0 ? (uint16_t)((left + 999) / 1000) : 0;
  }
  bool watching = mode_ == SEC_ARMED || mode_ == SEC_TRIGGERED;
  o.tilt = watching ? tilt_ : 0;
  o.turn = watching ? std::fabs(yaw_) : 0;
  o.shake = watching ? (uint8_t)shakeCount() : 0;
  o.tiltLim = lim_.tiltDeg;
  o.turnLim = lim_.turnDeg;
  o.shakeLim = lim_.shakeSecs;
}

// ---------------------------------------------------------------- simulator
const char *simMotionText(uint8_t m) {
  switch (m) {
    case SIM_WIND: return "wind gusts rocking the van";
    case SIM_JACK: return "someone winding the jockey wheel up";
    case SIM_TOW: return "hitched up and towed away";
    default: return "calm";
  }
}

float MotionSim::noise() {
  rng_ = rng_ * 1664525u + 1013904223u;
  return ((rng_ >> 8) & 0xFFFF) / 32768.0f - 1.0f;
}

void MotionSim::at(uint8_t motion, float t, Vec3 &a, Vec3 &g) {
  const Vec3 bias{0.8f, -0.5f, 0.3f};  // a typical uncalibrated MPU-6050 gyro offset
  float pitch = 0, roll = 0;            // degrees
  float pitchRate = 0, rollRate = 0, yawRate = 0;
  float ax = 0, ay = 0, az = 0;         // extra acceleration (g)
  switch (motion) {
    case SIM_WIND: {
      // Gusts every ~12 s rock the van ~0.6 deg at ~1 Hz and lean it a bit.
      float s = std::sin(kTwoPi * t / 12);
      float e = s > 0 ? s : 0.15f;
      float w = kTwoPi * 1.1f;
      roll = 0.6f * e * std::sin(w * t) + 0.25f * e;
      rollRate = 0.6f * e * w * std::cos(w * t);  // deg/s
      pitch = 0.1f * e * std::sin(kTwoPi * 0.7f * t);
      pitchRate = 0.1f * e * kTwoPi * 0.7f * std::cos(kTwoPi * 0.7f * t);
      ay = 0.012f * e * std::sin(kTwoPi * 1.7f * t);
      break;
    }
    case SIM_JACK: {
      // Jockey wheel wound up 4 deg over 20 s, a little jolt with each crank.
      float k = t < 20 ? t / 20 : 1;
      pitch = 4.0f * k;
      pitchRate = t < 20 ? 0.2f : 0;
      if (t < 20) az = 0.01f * std::sin(kTwoPi * 1.5f * t);
      break;
    }
    case SIM_TOW: {
      // Front lifted onto the towball over 6 s, then driven off: road
      // vibration, speeding up, and a 90 deg turn out of the site.
      pitch = t < 6 ? 1.2f * t / 6 : 1.2f;
      pitchRate = t < 6 ? 0.2f : 0;
      if (t > 8) {
        ax = 0.15f * noise() + (t < 14 ? 0.08f : 0);
        ay = 0.10f * noise();
        az = 0.20f * noise();
        rollRate = 2.0f * noise();
        pitchRate += 2.0f * noise();
      }
      if (t > 14 && t < 24) yawRate = 9;  // 90 deg over 10 s
      break;
    }
    default: break;
  }
  float p = pitch / kRad2Deg, r = roll / kRad2Deg;
  a = {std::sin(p) + ax + 0.002f * noise(), std::sin(r) * std::cos(p) + ay + 0.002f * noise(),
       std::cos(r) * std::cos(p) + az + 0.002f * noise()};
  g = {rollRate + bias.x + 0.05f * noise(), pitchRate + bias.y + 0.05f * noise(), yawRate + bias.z + 0.05f * noise()};
}

}  // namespace cv
