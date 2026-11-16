#include "Ble.h"

#include <NimBLEDevice.h>

#include "BleSurvey.h"

namespace hub {
namespace ble {

namespace {
constexpr int MAX_LISTENERS = 4;
AdvertFn gListeners[MAX_LISTENERS];
int gListenerCount = 0;
bool gStarted = false;
bool gWantScan = false;

class ScanCb : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice *d) override {
    for (int i = 0; i < gListenerCount; i++) gListeners[i](d);
  }
};
}  // namespace

void begin() {
  if (gStarted) return;
  gStarted = true;
  NimBLEDevice::init("caravan-hub");
  NimBLEScan *scan = NimBLEDevice::getScan();
  // Duplicates wanted: Victron devices re-broadcast new readings constantly.
  scan->setAdvertisedDeviceCallbacks(new ScanCb(), true);
  scan->setMaxResults(0);  // callbacks only - don't accumulate results in RAM
  // Active scans ask each device for its name (a scan request). The survey
  // build only listens, so it never transmits anything at other devices.
  scan->setActiveScan(!survey::kOn);
  scan->setInterval(160);  // 100 ms
  scan->setWindow(60);     // 37.5 ms: leaves the shared radio free for Wi-Fi
}

bool started() { return gStarted; }

void addListener(AdvertFn fn) {
  for (int i = 0; i < gListenerCount; i++)
    if (gListeners[i] == fn) return;
  if (gListenerCount < MAX_LISTENERS) gListeners[gListenerCount++] = fn;
}

void keepScanning() {
  gWantScan = true;
  resumeScan();
}

void pauseScan() {
  if (gStarted) NimBLEDevice::getScan()->stop();
}

void resumeScan() {
  if (!gStarted || !gWantScan) return;
  NimBLEScan *scan = NimBLEDevice::getScan();
  if (!scan->isScanning()) scan->start(0, nullptr, false);
}

}  // namespace ble
}  // namespace hub
