// Hub pin map for ESP32-S3-DevKitC-1. Avoid: 0/3/45/46 (strapping), 19/20
// (USB), 26-37 (flash/PSRAM), 48 (on-board RGB LED). See docs/WIRING.md.
#pragma once

// RS485 link to the 7" display (MAX3485 / auto-direction module)
#define PIN_LINK_RX 44
#define PIN_LINK_TX 43
#define PIN_LINK_DE 42   // set to -1 for auto-direction RS485 modules

// Victron VE.Direct (only the Victron TX -> ESP RX is needed; 5 V -> use divider)
#define PIN_VE1_RX 17
#define PIN_VE1_TX 18    // optional (HEX commands, unused for now)
#define PIN_VE2_RX 16
#define PIN_VE2_TX 15

// I2C bus: ADS1115 (tank taps, 0x48), INA226 battery shunt (0x40), INA226 charger (0x41)
#define PIN_I2C_SDA 8
#define PIN_I2C_SCL 9

// 1-Wire DS18B20 temperature sensors (4k7 pull-up to 3V3)
#define PIN_ONEWIRE 4

// Relay / opto outputs (active HIGH into a relay module or MOSFET driver)
#define PIN_PUMP1_OUT 10
#define PIN_PUMP2_OUT 11
#define PIN_INVERTER_OUT 12
#define PIN_SPARE_OUT 13

// Opto-isolated sense inputs (active LOW, internal pull-up)
#define PIN_PUMP1_SENSE 14
#define PIN_PUMP2_SENSE 21
#define PIN_INVERTER_AC_SENSE 47

// Aircon IR: IR LED via NPN/MOSFET, TSOP38238 receiver for learning codes
#define PIN_IR_TX 5
#define PIN_IR_RX 6

// Piezo buzzer (active buzzer via transistor) for un-acknowledged alarms
#define PIN_BUZZER 7
