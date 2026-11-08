// Persistent hub configuration, stored as JSON on LittleFS (/config.json).
#pragma once
#include <Arduino.h>
#include "CaravanModel.h"

enum SwitchMode : uint8_t {
  SW_LATCH = 0,   // relay held closed while "on" (parallel / diode-OR with BMPRO output)
  SW_PULSE = 1,   // momentary closure toggles the device (button input emulation)
  SW_DISABLED = 2
};

struct TankCal {
  float emptyMv = 300;   // ADS1115 reading (after divider) with tank empty
  float fullMv = 2800;   // ...and full. Inverted senders are fine (empty > full).
  uint8_t channel = 0;   // ADS1115 channel 0..3
  bool enabled = true;
};

struct PumpCfg {
  SwitchMode mode = SW_LATCH;
  uint16_t pulseMs = 300;
  uint16_t maxRunMin = 30;     // auto-off safety (0 = no limit)
  int8_t dryTank = -1;         // tank index used for dry-run lockout (-1 = none)
  uint8_t dryPct = 5;          // lock out below this level
  bool useSense = true;        // sense input fitted
};

struct InverterCfg {
  SwitchMode mode = SW_PULSE;
  uint16_t pulseMs = 500;
  bool useSense = true;
};

struct BatteryCfg {
  uint8_t source = cv::BAT_SRC_SHUNT;  // shunt (INA226) or BMPRO (once decoded)
  float capacityAh = 300;
  float chargeEff = 0.99f;
  float fullV = 13.9f;
  float tailA = 4.0f;
  float shuntOhm = 0.00025f;          // 300 A / 75 mV shunt
  bool chargerShunt = false;          // second INA226 (0x41) on BMPRO 35A output
  float chargerShuntOhm = 0.0015f;    // 50 A / 75 mV shunt
  float vCal = 1.0f;                  // voltage calibration factor
};

struct HubConfig {
  cv::Meta meta;
  cv::RuleSet rules;
  BatteryCfg bat;
  TankCal tanks[cv::NUM_TANKS];
  PumpCfg pumps[cv::NUM_PUMPS];
  InverterCfg inverter;
  char tempAddr[cv::NUM_TEMPS][17] = {};  // DS18B20 ROM as hex, "" = auto-assign
  uint16_t cloudIntervalSec = 15;
  bool buzzer = true;
  bool bmproExplorer = false;  // BLE logging of BMPRO CommLink for protocol capture

  void setDefaults();
  bool load();
  bool save() const;
  void toJson(JsonObject o) const;
  void fromJson(JsonObjectConst o);
};

extern HubConfig gConfig;
