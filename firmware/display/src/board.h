// Waveshare ESP32-S3-Touch-LCD-7 hardware: RGB panel (LovyanGFX), GT911 touch,
// CH422G I/O expander (backlight, resets) and LVGL glue.
#pragma once
#include <Arduino.h>

namespace board {
void begin();          // panel + touch + LVGL
void backlight(bool on);
bool backlightOn();
uint32_t lastTouchMs();
}  // namespace board

// On-board RS485 (auto-direction transceiver)
#define PIN_RS485_TX 15
#define PIN_RS485_RX 16
