#include "Ble.h"

#include <NimBLEDevice.h>

#include "BleCompat.h"

#include "BleSurvey.h"
#include "Comms.h"

namespace hub {
namespace ble {

namespace {
constexpr int MAX_LISTENERS = 6;  // Victron, BMPRO, thermometers, heater, survey
AdvertFn gListeners[MAX_LISTENERS];
int gListenerCount = 0;
bool gStarted = false;
bool gWantScan = false;
SemaphoreHandle_t gConnectMux = nullptr;
volatile uint32_t gAdverts = 0;  // adverts heard since boot (scan watchdog)

void dispatch(NimBLEAdvertisedDevice *d) {
  gAdverts++;
  static Advert a;  // NimBLE host task: small stack, and only it calls this
  a.dev = d;
  const uint8_t *m = blec::addrBytes(d->getAddress());
  snprintf(a.addr, sizeof(a.addr), "%02x:%02x:%02x:%02x:%02x:%02x", m[5], m[4], m[3], m[2], m[1], m[0]);
  a.raw = blec::payload(d);
  a.rawLen = blec::payloadLen(d);
  a.rssi = d->getRSSI();
  const uint8_t *p;
  uint8_t n;
  a.name[0] = 0;
  if (findAd(a.raw, a.rawLen, 0x09, 0, p, n) || findAd(a.raw, a.rawLen, 0x08, 0, p, n)) {
    if (n >= sizeof(a.name)) n = sizeof(a.name) - 1;
    memcpy(a.name, p, n);
    a.name[n] = 0;
  }
  a.mfg = nullptr;
  a.mfgLen = 0;
  if (findAd(a.raw, a.rawLen, 0xFF, 0, p, n)) {
    a.mfg = p;
    a.mfgLen = n;
  }
  for (int i = 0; i < gListenerCount; i++) gListeners[i](a);
}

#if HUB_NIMBLE2
class ScanCb : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *d) override {
    // Listeners only read the advert; the pointer is non-const for 1.4.
    dispatch(const_cast<NimBLEAdvertisedDevice *>(d));
  }
};
#else
class ScanCb : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice *d) override { dispatch(d); }
};
#endif
}  // namespace

bool findAd(const uint8_t *raw, size_t len, uint8_t type, int idx, const uint8_t *&data, uint8_t &dataLen) {
  size_t i = 0;
  while (raw && i + 1 < len) {
    uint8_t l = raw[i];
    if (l == 0 || i + 1 + l > len) return false;  // padding or a cut-off record
    if (raw[i + 1] == type && idx-- == 0) {
      data = raw + i + 2;
      dataLen = l - 1;
      return true;
    }
    i += 1 + l;
  }
  return false;
}

void begin() {
  if (gStarted) return;
  gStarted = true;
  gConnectMux = xSemaphoreCreateMutex();
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

bool lockConnect(uint32_t waitMs) {
  if (!gConnectMux) return true;
  return xSemaphoreTake(gConnectMux, pdMS_TO_TICKS(waitMs)) == pdTRUE;
}

void unlockConnect() {
  if (gConnectMux) xSemaphoreGive(gConnectMux);
}

void watchdog(uint32_t now) {
  static uint32_t lastCount = 0, lastAdvertMs = 0, nextCheck = 0;
  static uint8_t kicks = 0;
  if (!gStarted || !gWantScan) return;
  if ((int32_t)(now - nextCheck) < 0) return;
  nextCheck = now + 5000;
  uint32_t c = gAdverts;
  if (c != lastCount || !lastAdvertMs) {
    if (c != lastCount && kicks) Serial.println("[ble] adverts arriving again");
    lastCount = c;
    lastAdvertMs = now;
    kicks = 0;
    return;
  }
  if (!c) return;  // nothing heard since boot (bench, nothing in range): don't act
  uint32_t quiet = now - lastAdvertMs;
  if (quiet >= 300000) {
    restartFor("Bluetooth scan silent 5 min");
  } else if ((quiet >= 60000 && kicks == 0) || (quiet >= 120000 && kicks == 1)) {
    // Don't stop a scan that a connect has paused on purpose: wait for it.
    if (!lockConnect(0)) return;
    kicks++;
    Serial.printf("[ble] no adverts for %lu s - restarting the scan\n", (unsigned long)(quiet / 1000));
    pauseScan();
    delay(50);
    resumeScan();
    unlockConnect();
  }
}

}  // namespace ble
}  // namespace hub
