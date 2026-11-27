// Shared Bluetooth LE central: one NimBLE stack, one continuous scan, many
// listeners (Victron Instant Readout, BMPRO OdysseyLink discovery, heater).
#pragma once
#include <Arduino.h>

class NimBLEAdvertisedDevice;

namespace hub {
namespace ble {

// One advert, picked apart once by the shared scan so the listeners don't
// each build their own heap copies of the address, name and data (at a busy
// campsite that was hundreds of tiny allocations a second, chopping up the
// internal RAM the cloud link needs). The pointers point into the advert and
// are only valid during the callback.
struct Advert {
  NimBLEAdvertisedDevice *dev;
  char addr[18];         // "aa:bb:cc:dd:ee:ff", lower case (as NimBLE prints it)
  char name[32];         // complete or short local name, "" if none
  const uint8_t *mfg;    // manufacturer data (company id first), nullptr if none
  uint8_t mfgLen;
  const uint8_t *raw;    // whole advert + scan response
  size_t rawLen;
  int rssi;
};

// Finds AD structures of one type in a raw advert. Call with idx 0, 1, 2...
// Returns false when there are no more.
bool findAd(const uint8_t *raw, size_t len, uint8_t type, int idx, const uint8_t *&data, uint8_t &dataLen);

// Case-insensitive "does hay contain needle" (no heap, unlike String).
inline bool containsNoCase(const char *hay, const char *needle) {
  size_t n = strlen(needle);
  for (; *hay; hay++)
    if (strncasecmp(hay, needle, n) == 0) return true;
  return false;
}

using AdvertFn = void (*)(const Advert &a);

void begin();                    // idempotent
void addListener(AdvertFn fn);   // called for every advert received
void keepScanning();             // a listener needs the scan running permanently
void pauseScan();                // e.g. while connecting to a peripheral
void resumeScan();               // restarts the scan if anyone needs it
bool started();

// Only one Bluetooth connection is set up at a time (BMPRO, heater): the
// scan is paused while connecting, and two tasks pausing and resuming it on
// their own made connects fail. Hold this from pauseScan() to resumeScan().
// Returns false if it couldn't be had within waitMs.
bool lockConnect(uint32_t waitMs = 60000);
void unlockConnect();

// Scan watchdog, from the main loop. If adverts stop arriving while the scan
// should be running (Bluetooth wedged, no crash), it restarts the scan after
// 60 s and again at 2 min; at 5 min it restarts the hub. Only arms once an
// advert has been heard since boot, so a bench with nothing in range never
// restart-loops.
void watchdog(uint32_t now);

}  // namespace ble
}  // namespace hub
