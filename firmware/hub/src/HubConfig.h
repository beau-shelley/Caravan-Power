// Persistent hub configuration, stored as JSON on LittleFS (/config.json).
#pragma once
#include <Arduino.h>
#include "CaravanModel.h"
#include "Leveling.h"

enum SwitchMode : uint8_t {
  SW_LATCH = 0,   // relay held closed while "on" (parallel / diode-OR with BMPRO output)
  SW_PULSE = 1,   // momentary closure toggles the device (button input emulation)
  SW_DISABLED = 2,
  SW_BMPRO = 3    // switched by the BMPRO itself, commanded over Bluetooth (no wiring)
};

enum TankSource : uint8_t {
  TANK_BMPRO = 0,  // levels from the OdysseyLink over Bluetooth (no wiring)
  TANK_ADC = 1,    // ADS1115 taps on the sender wires
  TANK_NONE = 2
};

enum SolarSource : uint8_t {
  SOLAR_BLE = 0,       // Victron Instant Readout over Bluetooth (no cable)
  SOLAR_VEDIRECT = 1,  // VE.Direct cable (adds PV voltage + total yield)
  SOLAR_NONE = 2
};

struct VictronCfg {
  uint8_t src = SOLAR_BLE;
  char addr[18] = "";  // optional; "" = match by key
  char key[33] = "";   // 32 hex chars from VictronConnect "Instant readout details"
};

enum TempSource : uint8_t { TEMP_WIRED = 0, TEMP_BLE = 1 };

struct LevelCfg {
  bool zeroed = false;
  cv::LevelCal cal;
  uint16_t trackMm = 1900;   // between wheel centres
  uint16_t jockeyMm = 4500;  // axle to jockey wheel
};

struct LocationCfg {
  uint8_t src = cv::LOC_NONE;  // GPS / phone / manual
  float lat = 0, lon = 0;
  char name[24] = "";
};

struct TankCal {
  float emptyMv = 300;   // ADS1115 reading (after divider) with tank empty
  float fullMv = 2800;   // ...and full. Inverted senders are fine (empty > full).
  uint8_t channel = 0;   // ADS1115 channel 0..3
  bool enabled = true;
};

struct PumpCfg {
  SwitchMode mode = SW_BMPRO;
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

// Diesel heater: how the hub reaches it (cv::HeaterLink). Set from the phone.
struct HeaterCfg {
  uint8_t link = cv::HTR_LINK_NONE;
  char addr[18] = "";      // Bluetooth address of the heater (find it with heater_seen)
  uint16_t passkey = 1234; // the AirHeaterBLE app's passkey (factory 1234)
};

struct BatteryCfg {
  uint8_t source = cv::BAT_SRC_BMPRO;  // BMPRO over Bluetooth, or own INA226 shunt
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
  VictronCfg victron[cv::NUM_SOLAR];
  LevelCfg level;
  LocationCfg location;
  HeaterCfg heater;
  char tempAddr[cv::NUM_TEMPS][17] = {};  // DS18B20 ROM as hex, "" = auto-assign
  uint8_t tempSrc[cv::NUM_TEMPS] = {TEMP_WIRED, TEMP_WIRED, TEMP_WIRED};
  char tempBle[cv::NUM_TEMPS][18] = {};   // Bluetooth thermometer address per role
  uint16_t cloudIntervalSec = 15;
  bool buzzer = true;
  uint8_t tankSource = TANK_BMPRO;
  char bmproAddr[18] = "";     // OdysseyLink BLE address (find it with bmpro_devices)
  bool bmproExplorer = false;  // verbose BLE logging for protocol capture

  bool pumpViaBmpro(int i) const { return pumps[i].mode == SW_BMPRO; }
  bool usesBmpro() const {
    bool anyPump = false;
    for (auto &p : pumps) anyPump |= p.mode == SW_BMPRO;
    return bat.source == cv::BAT_SRC_BMPRO || tankSource == TANK_BMPRO || anyPump || bmproExplorer;
  }

  void setDefaults();
  bool load();
  bool save() const;
  void toJson(JsonObject o) const;
  void fromJson(JsonObjectConst o);
};

extern HubConfig gConfig;
