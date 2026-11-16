#include "Secure.h"

#include "HubConfig.h"
#include "TheftGuard.h"

namespace hub {
namespace secure {
namespace {
cv::TheftGuard gGuard;
uint8_t gLastMode = 0xFF;
uint32_t gSince = 0;  // unix time of the last mode change
uint32_t gLastTickMs = 0;

// Fake-data theft simulation
cv::MotionSim gSim;
uint32_t gSimLastMs = 0, gSimArmedAtMs = 0;
uint8_t gSimMotion = cv::SIM_CALM;
uint16_t gSimArmCount = 0;
constexpr float kSimWindSec = 45, kSimCalmSec = 15;  // then the "thief" turns up
}  // namespace

void begin(uint32_t nowMs) {
  const SecureCfg &c = gConfig.secure;
  gGuard.setSensitivity(c.sens);
  if (c.armed) {
    if (c.haveBase) gGuard.resume(c.base, nowMs);
    else gGuard.arm(nowMs);
    Serial.println("[secure] was armed before the restart - re-arming");
  }
}

void feed(const cv::Vec3 &a, const cv::Vec3 &g, uint32_t nowMs) { gGuard.sample(a, g, nowMs); }

bool arm(bool on, bool sensorOk, uint32_t nowMs, const char **why) {
  SecureCfg &c = gConfig.secure;
  if (on) {
    if (!sensorOk) {
      if (why) *why = "Secure mode needs the level sensor (MPU-6050), and it isn't answering";
      return false;
    }
    if (gGuard.mode() == cv::SEC_OFF) gGuard.arm(nowMs);  // pressing Arm again doesn't restart the delay
    cv::ensureTheftRule(gConfig.rules);
    c.armed = true;
    c.haveBase = false;
    gSimArmCount++;
    gSimArmedAtMs = 0;
  } else {
    gGuard.disarm();
    c.armed = false;
    c.haveBase = false;
  }
  gConfig.save();
  Serial.printf("[secure] %s\n", on ? "arming (60 s exit delay)" : "disarmed");
  return true;
}

void setSensitivity(uint8_t sens) {
  gGuard.setSensitivity(sens);
  gConfig.secure.sens = gGuard.sensitivity();
  gConfig.save();
}

bool loop(cv::State &s, bool sensorOk, uint32_t nowMs) {
  bool changed = false;
  if (nowMs - gLastTickMs >= 200) {
    gLastTickMs = nowMs;
    changed = gGuard.tick(nowMs, sensorOk);
  }
  uint8_t m = gGuard.mode();
  if (m != gLastMode) {
    if (gLastMode != 0xFF) changed = true;
    gLastMode = m;
    gSince = s.ts;
    if (m == cv::SEC_ARMED && gGuard.hasBaseline()) {
      // Save the resting tilt so a restart compares against it.
      gConfig.secure.haveBase = true;
      gConfig.secure.base = gGuard.baseline();
      gConfig.save();
      Serial.println("[secure] ARMED");
    }
    if (m == cv::SEC_TRIGGERED)
      Serial.printf("[secure] ALARM: %s\n", cv::secureCauseText(gGuard.cause()));
  }
  gGuard.fill(s.sec, nowMs);
  s.sec.since = gSince;
  return changed;
}

void simLoop(uint32_t nowMs) {
  if (nowMs - gSimLastMs < 10) return;  // 100 Hz like the real sensor
  gSimLastMs = nowMs;
  uint8_t motion = cv::SIM_CALM;
  float t = 0;
  if (gGuard.mode() == cv::SEC_ARMED || gGuard.mode() == cv::SEC_TRIGGERED) {
    if (!gSimArmedAtMs) gSimArmedAtMs = nowMs;
    float since = (nowMs - gSimArmedAtMs) / 1000.0f;
    if (since < kSimWindSec) {
      motion = cv::SIM_WIND;
      t = since;
    } else if (since >= kSimWindSec + kSimCalmSec) {
      motion = gSimArmCount % 2 ? cv::SIM_JACK : cv::SIM_TOW;
      t = since - kSimWindSec - kSimCalmSec;
    }
  } else {
    gSimArmedAtMs = 0;
  }
  if (motion != gSimMotion) {
    gSimMotion = motion;
    Serial.printf("[secure] fake data: %s\n", cv::simMotionText(motion));
  }
  cv::Vec3 a, g;
  gSim.at(motion, t, a, g);
  gGuard.sample(a, g, nowMs);
}

}  // namespace secure
}  // namespace hub
