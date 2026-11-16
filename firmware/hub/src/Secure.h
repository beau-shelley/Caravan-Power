// Secure mode (anti-theft) on the hub: arm/disarm, feeds the level sensor's
// samples to cv::TheftGuard, and keeps the armed state across a restart.
// The alarm itself is the "Theft alarm" rule (metric "theft", Critical), so it
// reaches the display, web page, app and phone push like any other alert.
#pragma once
#include <Arduino.h>
#include "CaravanModel.h"

namespace hub {
namespace secure {

void begin(uint32_t nowMs);
// One accelerometer (g) + gyro (deg/s) sample from the MPU-6050, ~100 Hz.
void feed(const cv::Vec3 &accelG, const cv::Vec3 &gyroDps, uint32_t nowMs);
// Every loop: housekeeping and state.sec. Returns true when something changed
// that every screen should see straight away.
bool loop(cv::State &s, bool sensorOk, uint32_t nowMs);
// false + *why when it can't (e.g. no level sensor).
bool arm(bool on, bool sensorOk, uint32_t nowMs, const char **why);
void setSensitivity(uint8_t sens);

// Fake-data build: after arming, pretend wind gusts rock the van for a while
// (it must stay quiet), then pretend a theft - jacking on odd arms, towing on
// even ones. Used only when no real MPU-6050 is plugged in.
void simLoop(uint32_t nowMs);

}  // namespace secure
}  // namespace hub
