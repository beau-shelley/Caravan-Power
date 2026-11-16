// CAN bus raw logger for the Waveshare 7B's on-board CAN port (TJA1051T/3,
// TX GPIO20 / RX GPIO19, shared with native USB through an FSUSB42 switch that
// CH32 EXIO5 sets to CAN; board::begin() drives it high).
//
// Purpose: capture the BMPRO Trek 3 bus so the (unpublished) protocol can be
// decoded later. The controller runs LISTEN-ONLY: it never transmits, never
// ACKs and never sends error frames, so it cannot disturb the BMPRO system.
//
// Everything runs on core 0 (driver ISR + one task). LVGL owns core 1, so CAN
// traffic cannot cost the UI a frame. Serial output never blocks: when the
// UART buffer is full a line is dropped and counted instead.
//
// Serial commands (type a letter + Enter in the serial monitor, 115200 baud):
//   d  changes only (default): print a frame when its ID is new or its data changed
//   r  raw: print every frame
//   s  print the per-ID summary now (also printed every 10 s in d/r modes)
//   q  quiet: log nothing (counters keep running)
//   b  rescan the bitrate
// Frame lines are CSV:  CAN,<ms>,<id hex>,<s|x>,<dlc>,<data hex>
#pragma once
#include <Arduino.h>

#ifndef FEATURE_CAN_LOG
#define FEATURE_CAN_LOG 1
#endif
// 0 = auto-detect (listen at each common rate until valid frames arrive).
// Set e.g. -D CAN_BITRATE=250000 once the BMPRO rate is known.
#ifndef CAN_BITRATE
#define CAN_BITRATE 0
#endif

namespace canbus {

struct Stats {
  uint32_t bitrate = 0;     // 0 while still searching
  uint32_t frames = 0;      // valid frames received
  uint16_t ids = 0;         // distinct IDs seen
  uint32_t rxMissed = 0;    // frames lost because the driver queue was full
  uint32_t linesDropped = 0;  // log lines skipped because Serial was busy
  uint32_t busErrors = 0;
};

void begin();   // starts the core-0 task; returns immediately
Stats stats();  // safe to call from any task

}  // namespace canbus
