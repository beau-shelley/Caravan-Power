// Secure mode (anti-theft) using the level sensor (MPU-6050 on the hub).
//
// When armed, the hub watches for what a thief has to do to take the van,
// and ignores what the weather does to a parked van:
//
//   Tilt  - winding the jockey wheel up/down to hitch, jacking a wheel off,
//           or being towed (acceleration looks like tilt). The tilt is
//           averaged over 5 s, so a wind gust rocking the van and settling
//           back doesn't count; it has to stay past the limit for 10 s.
//   Turn  - the van rotating about its vertical axis (towed round a corner
//           or out of a site). Wind rocks a van side to side; it can't spin
//           it on its wheels, so this one ignores wind almost completely.
//   Shake - road vibration: the van shaking on most seconds of a minute.
//           Gusts come and go, so short bursts don't add up to an alarm.
//   Sensor - the level sensor stops answering while armed (unplugged).
//
// Movement first starts a 5 minute entry delay (a warning on every screen and
// the phone, beeping on the hub). Disarm within it - with the code on the
// display - and nothing more happens; otherwise the full Critical alarm.
//
// Arming has a 60 s exit delay so you can close the door and walk away. The
// hub remembers it's armed (and the van's resting tilt) across a restart, so
// pulling the power and moving the van is still caught when it comes back.
// Portable: no Arduino, time passed in. Unit tested on the host.
#pragma once
#include <cstdint>

#include "Leveling.h"  // Vec3

namespace cv {

// SEC_ENTRY: movement was detected; 5 minutes to disarm (code on the display,
// none needed on the phone) before it becomes SEC_TRIGGERED, the full alarm.
enum SecureMode : uint8_t { SEC_OFF = 0, SEC_ARMING = 1, SEC_ARMED = 2, SEC_TRIGGERED = 3, SEC_ENTRY = 4 };
enum SecureCause : uint8_t { SEC_NONE = 0, SEC_TILT = 1, SEC_TURN = 2, SEC_SHAKE = 3, SEC_SENSOR = 4 };
enum SecureSens : uint8_t { SENS_LOW = 0, SENS_MEDIUM = 1, SENS_HIGH = 2 };
const char *secureModeText(uint8_t mode);
const char *secureCauseText(uint8_t cause);  // short, for alert messages
const char *secureSensText(uint8_t sens);

// What every screen shows (part of the state frame, key "sec").
struct Secure {
  uint8_t mode = SEC_OFF;
  uint8_t cause = SEC_NONE;
  uint8_t sens = SENS_MEDIUM;
  bool sensor = false;    // level sensor is working (secure mode needs it)
  uint16_t armLeft = 0;   // seconds left: until armed (SEC_ARMING) or until the alarm (SEC_ENTRY)
  uint32_t since = 0;     // unix time of the last mode change (0 = clock not set)
  // Live movement readings while armed, and the limits they're compared to,
  // so you can see how close wind gets to setting it off.
  float tilt = 0, tiltLim = 0;  // degrees from where it was armed
  float turn = 0, turnLim = 0;  // degrees turned in the last minute or so
  uint8_t shake = 0, shakeLim = 0;  // shaking seconds in the last minute
};

struct GuardLimits {
  float tiltDeg;       // tilt past this ...
  float tiltHoldSec;   // ... for this long
  float turnDeg;       // turned this far (leaky, ~1 min memory)
  float shakeMg;       // a second counts as "shaking" above this RMS vibration
  uint8_t shakeSecs;   // this many shaking seconds out of the last 60
};
GuardLimits guardLimits(uint8_t sens);

class TheftGuard {
 public:
  static constexpr uint32_t ARM_DELAY_MS = 60000;   // exit delay
  static constexpr uint32_t RESUME_DELAY_MS = 5000; // after a restart: settle, measure gyro bias
  static constexpr uint32_t SENSOR_LOST_MS = 5000;
  static constexpr uint32_t ENTRY_DELAY_MS = 300000;  // 5 min to disarm after movement

  void setSensitivity(uint8_t sens);
  uint8_t sensitivity() const { return sens_; }
  // Start the exit delay; the resting tilt is captured when it ends.
  void arm(uint32_t nowMs);
  // Arm with no exit delay (from the phone, when nobody is in the van): armed
  // straight away against the current resting tilt. The first few seconds also
  // measure the gyro's bias, so turning is only watched once that's done.
  void armNow(uint32_t nowMs);
  // Armed before a restart: re-arm quickly against the saved resting tilt.
  void resume(const Vec3 &baseline, uint32_t nowMs);
  void disarm();

  // Every sensor sample (~100 Hz). accel in g in the sensor's own axes (any
  // mounting), gyro in deg/s in the same axes.
  void sample(const Vec3 &accelG, const Vec3 &gyroDps, uint32_t nowMs);
  // Once a second or more: exit delay, sensor watchdog, vibration window.
  // Returns true when the mode or cause changed.
  bool tick(uint32_t nowMs, bool sensorOk);

  uint8_t mode() const { return mode_; }
  uint8_t cause() const { return cause_; }
  bool hasBaseline() const { return haveBase_; }
  Vec3 baseline() const { return base_; }
  void fill(Secure &out, uint32_t nowMs) const;

 private:
  uint8_t sens_ = SENS_MEDIUM;
  GuardLimits lim_ = guardLimits(SENS_MEDIUM);
  uint8_t mode_ = SEC_OFF, cause_ = SEC_NONE;
  uint32_t modeSinceMs_ = 0, armAtMs_ = 0, alarmAtMs_ = 0;
  uint32_t calUntilMs_ = 0;  // armNow(): gyro bias still being measured until then (0 = done)
  bool keepBase_ = false;  // resume(): keep the saved baseline

  // Filters (run whenever samples arrive, armed or not, so arming is instant).
  bool primed_ = false;
  uint32_t lastSampleMs_ = 0;
  Vec3 gSlow_, gMid_;     // gravity averaged ~5 s (tilt) and ~1 s (vibration reference)
  // Gyro bias: averaged during the last seconds of the exit delay, then
  // slowly tracked while the van is still.
  double bx_ = 0, by_ = 0, bz_ = 0;
  uint32_t biasN_ = 0;
  Vec3 bias_;
  bool haveBias_ = false;

  bool haveBase_ = false;
  Vec3 base_;      // resting gravity direction (unit) when armed
  float tilt_ = 0; // degrees between gSlow_ and base_
  float tiltOverMs_ = 0;
  float yaw_ = 0;  // leaky-integrated rotation about the van's vertical (deg)

  // Vibration: RMS per second, last 60 seconds as bits.
  double vibSum_ = 0;
  uint32_t vibN_ = 0, vibSecStartMs_ = 0;
  uint64_t shakeBits_ = 0;
  uint8_t lastRmsMg_ = 0;

  uint32_t lastTickMs_ = 0;
  bool sensorOk_ = false;
  bool sensorBad_ = false;
  uint32_t sensorBadSinceMs_ = 0;

  void setMode(uint8_t m, uint32_t nowMs);
  void trigger(uint8_t cause, uint32_t nowMs);
  void startWatching(uint32_t nowMs);
  int shakeCount() const;
};

// Made-up sensor samples for the hub's fake-data build and the tests: a
// parked van in calm air, rocked by wind gusts, jacked / hitched at the
// front, or towed away.
enum SimMotion : uint8_t { SIM_CALM = 0, SIM_WIND, SIM_JACK, SIM_TOW };
const char *simMotionText(uint8_t m);
class MotionSim {
 public:
  // t = seconds since this motion started. Writes accel (g) and gyro (deg/s)
  // in the van's axes (x forward, y left, z up).
  void at(uint8_t motion, float t, Vec3 &accel, Vec3 &gyro);

 private:
  uint32_t rng_ = 12345;
  float noise();  // roughly -1..1
};

}  // namespace cv
