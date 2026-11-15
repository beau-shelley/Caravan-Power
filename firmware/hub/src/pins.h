// Hub pin maps. Two boards are supported (docs/WIRING.md):
//  - classic ESP32-WROOM-32 DevKit (30-pin, CH340/CP2102 USB): env hub-esp32
//  - ESP32-S3-DevKitC-1: env hub
#pragma once
#include <sdkconfig.h>  // CONFIG_IDF_TARGET_*

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
// ============================================================ classic ESP32
// Avoid: 0/2/5/12/15 as inputs or boot-time outputs (strapping; 12 must be
// LOW at boot), 1/3 (USB console = UART0), 6-11 (flash). 34-39 are input-only
// with no internal pull-ups. UART0 is the USB console, so only UART1 and
// UART2 are free: UART2 drives the display link, UART1 is shared by the
// optional VE.Direct #1 cable and the optional GPS (one or the other). The
// VE.Direct #2 cable fallback isn't available on this board.

// RS485 link to the 7" display (MAX485 / MAX3485 / auto-direction module)
#define LINK_SERIAL Serial2
#define PIN_LINK_RX 16   // <- module RO
#define PIN_LINK_TX 17   // -> module DI
#define PIN_LINK_DE 4    // -> module DE + /RE tied together; -1 for auto-direction

// Victron VE.Direct #1 cable fallback (Victron TX -> ESP RX; 5 V -> divider)
#define PIN_VE1_RX 36    // VP, input-only
#define PIN_VE1_TX 15    // not wired (HEX commands unused); keeps UART1 off flash pins
#define HUB_HAS_VE2 0
#define PIN_VE2_RX -1
#define PIN_VE2_TX -1

// I2C bus: MPU-6050 level sensor (0x68); fallbacks ADS1115 (0x48), INA226 (0x40/0x41)
#define PIN_I2C_SDA 21
#define PIN_I2C_SCL 22

// 1-Wire DS18B20 temperature sensors (4k7 pull-up to 3V3)
#define PIN_ONEWIRE 23

// Relay / opto outputs (active HIGH into a relay module or MOSFET driver)
#define PIN_PUMP1_OUT 32
#define PIN_PUMP2_OUT 33
#define PIN_INVERTER_OUT 27
#define PIN_SPARE_OUT 13

// Opto-isolated sense inputs (active LOW, internal pull-up)
#define PIN_PUMP1_SENSE 14
#define PIN_PUMP2_SENSE 18
#define PIN_INVERTER_AC_SENSE 19

// Aircon IR: IR LED via NPN/MOSFET, TSOP38238 receiver for learning codes
#define PIN_IR_TX 26
#define PIN_IR_RX 34     // input-only is fine (TSOP has its own pull-up)

// Optional GPS module (9600 baud NMEA) on UART1, shared with VE.Direct #1.
#define GPS_SERIAL Serial1
#define GPS_SHARES_SOLAR 0   // index of the VE.Direct port that shares GPS's UART
#define PIN_GPS_RX 39    // VN, input-only  <- GPS TX
#define PIN_GPS_TX 15    // -> GPS RX (optional, not needed)

// Piezo buzzer (active buzzer via transistor) for un-acknowledged alarms
#define PIN_BUZZER 25

#else
// ============================================================ ESP32-S3-DevKitC-1
// Avoid: 0/3/45/46 (strapping), 19/20 (USB), 26-37 (flash/PSRAM), 48
// (on-board RGB LED). Serial is native USB, which frees UART0 for RS485.
#define LINK_SERIAL Serial0
#define HUB_HAS_VE2 1
#define GPS_SERIAL Serial2
#define GPS_SHARES_SOLAR 1

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

// Optional GPS module (u-blox NEO-6M/M8N etc., 9600 baud NMEA) for the
// weather location. Uses UART2, so not available together with the VE.Direct
// cable fallback for solar controller 2.
#define PIN_GPS_RX 39   // <- GPS TX
#define PIN_GPS_TX 40   // -> GPS RX (optional)

// Piezo buzzer (active buzzer via transistor) for un-acknowledged alarms
#define PIN_BUZZER 7

#endif
