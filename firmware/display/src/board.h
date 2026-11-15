// Waveshare ESP32-S3-Touch-LCD-7B: RGB panel, GT911 touch and CH32 IO extension.
#pragma once
#include <Arduino.h>

// Panel resolution. The UI lays itself out from these two numbers (see
// ui/ui.h); the PC simulator can also render other sizes, e.g.
// -D DISPLAY_HOR_RES=800 -D DISPLAY_VER_RES=480.
#ifndef DISPLAY_HOR_RES
#define DISPLAY_HOR_RES 1024
#endif
#ifndef DISPLAY_VER_RES
#define DISPLAY_VER_RES 600
#endif

namespace board {
constexpr int SCREEN_W = DISPLAY_HOR_RES, SCREEN_H = DISPLAY_VER_RES;

void begin();          // panel + touch + LVGL
void backlight(bool on);
bool backlightOn();
uint32_t lastTouchMs();
}  // namespace board

// On-board RS485 (auto-direction transceiver)
#define PIN_RS485_TX 16
#define PIN_RS485_RX 15
