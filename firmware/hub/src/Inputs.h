// Sensor inputs: Victron VE.Direct x2, DS18B20 temps, ADS1115 tank taps,
// INA226 battery shunt (+ optional charger shunt).
#pragma once
#include <Arduino.h>
#include "CaravanModel.h"
#include "SocEstimator.h"
#include "VeDirect.h"

namespace hub {

class VictronPort {
 public:
  void begin(HardwareSerial &ser, int rxPin, int txPin);
  void poll(cv::Solar &out);
  uint32_t good() const { return parser_.goodBlocks(); }
  uint32_t bad() const { return parser_.badBlocks(); }
  const cv::VeDirectParser &parser() const { return parser_; }

 private:
  HardwareSerial *ser_ = nullptr;
  cv::VeDirectParser parser_;
  uint32_t lastGoodMs_ = 0;
};

struct FoundSensor {
  char addr[17];
  float c;
  int8_t role;  // assigned TempId or -1
};

class TempSensors {
 public:
  void begin(int pin);
  // Non-blocking: starts a conversion, reads results ~800 ms later.
  void poll(cv::State &s);
  int found(FoundSensor *out, int max);
  void resetMinMax(cv::State &s);

 private:
  uint32_t requestedMs_ = 0;
  bool converting_ = false;
  uint32_t lastScanMs_ = 0;
  void autoAssign();
};

class TankInputs {
 public:
  bool begin();
  void poll(cv::State &s);
  float rawMv(int tank) const { return rawMv_[tank]; }

 private:
  bool ok_ = false;
  float rawMv_[cv::NUM_TANKS] = {};
  float filt_[cv::NUM_TANKS] = {};
  bool primed_[cv::NUM_TANKS] = {};
  uint32_t lastMs_ = 0;
  uint8_t next_ = 0;
};

class BatteryShunt {
 public:
  bool begin();
  void poll(cv::State &s, uint32_t nowMs);
  void setSoc(float pct) { soc_.setSoc(pct); }
  float soc() const { return soc_.soc(); }
  void persist(bool force = false);

 private:
  bool ok_ = false, chgOk_ = false;
  cv::SocEstimator soc_;
  uint32_t lastMs_ = 0, lastSaveMs_ = 0;
  float lastSavedSoc_ = -1;
  float avgA_ = 0;
};

}  // namespace hub
