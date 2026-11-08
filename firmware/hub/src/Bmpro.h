// BMPRO (Odyssey Link / JHub / CommLink) integration.
//
// BMPRO does not publish its CommLink / Trek protocol. This module is a
// capture tool: it scans BLE, lists nearby devices, connects to the CommLink,
// dumps its GATT table and logs every notification as hex so the protocol
// can be decoded (see docs/BMPRO_INTEGRATION.md). Once frames are understood,
// fill in Bmpro::decode() and set battery source to BMPRO.
#pragma once
#include <Arduino.h>
#include "CaravanModel.h"

namespace hub {

class Bmpro {
 public:
  void begin();
  void loop(cv::State &s);
  void connect(const char *addr);  // async
  void disconnect();
  // Copies the capture log (newest last) as text lines.
  String log() const;
  String devices() const;  // JSON array of seen BLE devices
  bool connected() const;

  // Hook: called with every notification. Return true if it updated state.
  static bool decode(const char *charUuid, const uint8_t *data, size_t len, cv::State &s);
};

extern Bmpro gBmpro;

}  // namespace hub
