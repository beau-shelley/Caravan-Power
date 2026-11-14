// Caravan levelling: MPU-6050 accelerometer on the I2C bus, with a Bluetooth
// service so a phone can show the level while you set up (web/level.html).
#pragma once
#include <Arduino.h>
#include "CaravanModel.h"

namespace hub {

class LevelSensor {
 public:
  bool begin();                    // false if no MPU-6050 found
  void loop(cv::State &s);         // 100 Hz sampling, filtering, calibration
  // Calibration takes ~2 s of averaging; results are saved to config.
  void startZero();                // van is on known-level ground
  void startLearnFront();          // front (jockey) lifted a little
  void resetCalibration();
  void setDims(uint16_t trackMm, uint16_t jockeyMm);
  const char *lastResult() const { return result_; }
  bool online() const { return ok_; }
  // Location sent by the phone level page ("Set weather location here").
  bool takeLocation(float &lat, float &lon, char *name, size_t n);

 private:
  bool ok_ = false;
  uint8_t addr_ = 0x68;
  uint32_t lastSampleMs_ = 0, lastBleMs_ = 0, lastRetryMs_ = 0;
  float fx_ = 0, fy_ = 0, fz_ = 0;  // low-pass filtered accel
  bool primed_ = false;
  enum CalMode : uint8_t { CAL_NONE, CAL_ZERO, CAL_FRONT } calMode_ = CAL_NONE;
  double sx_ = 0, sy_ = 0, sz_ = 0;
  uint16_t calN_ = 0;
  const char *result_ = "";
  bool readAccel(float &x, float &y, float &z);
  void finishCal();
  void startBle();
  void notifyBle(const cv::Level &l);
};

extern LevelSensor gLevel;

}  // namespace hub
