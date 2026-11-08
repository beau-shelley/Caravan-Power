// Shared Bluetooth LE central: one NimBLE stack, one continuous scan, many
// listeners (Victron Instant Readout, BMPRO OdysseyLink discovery).
#pragma once
#include <Arduino.h>

class NimBLEAdvertisedDevice;

namespace hub {
namespace ble {

using AdvertFn = void (*)(NimBLEAdvertisedDevice *dev);

void begin();                    // idempotent
void addListener(AdvertFn fn);   // called for every advert received
void keepScanning();             // a listener needs the scan running permanently
void pauseScan();                // e.g. while connecting to a peripheral
void resumeScan();               // restarts the scan if anyone needs it
bool started();

}  // namespace ble
}  // namespace hub
