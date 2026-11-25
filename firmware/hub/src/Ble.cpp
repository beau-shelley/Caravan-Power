#include "Ble.h"

#include <NimBLEDevice.h>

#include "BleCompat.h"

#include "BleSurvey.h"

namespace hub {
namespace ble {

namespace {
constexpr int MAX_LISTENERS = 6;  // Victron, BMPRO, thermometers, heater, survey
AdvertFn gListeners[MAX_LISTENERS];
int gListenerCount = 0;
bool gStarted = false;
bool gWantScan = false;

#if HUB_NIMBLE2
class ScanCb : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *d) override {
    // Listeners only read the advert; the pointer is non-const for 1.4.
    auto *dev = const_cast<NimBLEAdvertisedDevice *>(d);
    for (int i = 0; i < gListenerCount; i++) gListeners[i](dev);
  }
};
#else
class ScanCb : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice *d) override {
    for (int i = 0; i < gListenerCount; i++) gListeners[i](d);
  }
};
#endif
}  // namespace

void begin() {
  if (gStarted) return;
  gStarted = true;
  NimBLEDevice::init("caravan-hub");
  NimBLEScan *scan = NimBLEDevice::getScan();
  // Duplicates wanted: Victron devices re-broadcast new readings constantly.
#if HUB_NIMBLE2
  scan->setScanCallbacks(new ScanCb(), true);
#else
  scan->setAdvertisedDeviceCallbacks(new ScanCb(), true);
#endif
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
#if HUB_NIMBLE2
  if (!scan->isScanning()) scan->start(0, false, false);  // forever, keep the duplicate list
#else
  if (!scan->isScanning()) scan->start(0, nullptr, false);
#endif
}

}  // namespace ble
}  // namespace hub
