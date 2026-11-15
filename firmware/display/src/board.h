// Waveshare ESP32-S3-Touch-LCD-7B: RGB panel, GT911 touch and CH32 IO extension.
#pragma once
#include <Arduino.h>

namespace board {
void begin();          // panel + touch + LVGL
void backlight(bool on);
bool backlightOn();
uint32_t lastTouchMs();
}  // namespace board

// On-board RS485 (auto-direction transceiver)
#define PIN_RS485_TX 16
#define PIN_RS485_RX 15
