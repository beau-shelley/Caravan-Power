#include "BleSurvey.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include "Ble.h"

namespace hub {
namespace survey {
namespace {

constexpr int MAX_DEV = 64;
constexpr size_t MAX_RAW = 62;  // advert + scan response never exceed this

struct Dev {
  uint8_t mac[6];
  uint32_t lastPrintMs = 0;
  uint32_t count = 0;
  int8_t rssi = 0;
  uint8_t rawLen = 0;
  uint8_t raw[MAX_RAW];
  char name[24] = {0};
  uint16_t mfrId = 0;
  bool hasMfr = false;
};

Dev gDev[MAX_DEV];
int gDevCount = 0;
bool gFull = false;

void hex(String &out, const uint8_t *p, size_t n) {
  static const char *h = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) {
    out += h[p[i] >> 4];
    out += h[p[i] & 15];
  }
}

// Called from the NimBLE host task for every advert heard.
void onAdvert(NimBLEAdvertisedDevice *d) {
  const uint8_t *mac = d->getAddress().getNative();
  const uint8_t *raw = d->getPayload();
  size_t rawLen = d->getPayloadLength();
  if (rawLen > MAX_RAW) rawLen = MAX_RAW;

  Dev *dev = nullptr;
  for (int i = 0; i < gDevCount; i++)
    if (!memcmp(gDev[i].mac, mac, 6)) {
      dev = &gDev[i];
      break;
    }
  if (!dev) {
    if (gDevCount >= MAX_DEV) {
      gFull = true;
      return;
    }
    dev = &gDev[gDevCount++];
    memcpy(dev->mac, mac, 6);
  }
  dev->count++;
  dev->rssi = d->getRSSI();
  if (d->haveName()) snprintf(dev->name, sizeof(dev->name), "%s", d->getName().c_str());
  std::string mfr = d->haveManufacturerData() ? d->getManufacturerData() : std::string();
  if (mfr.size() >= 2) {
    dev->hasMfr = true;
    dev->mfrId = (uint8_t)mfr[0] | ((uint8_t)mfr[1] << 8);
  }

  // Print only new payloads, at most once a second per device.
  uint32_t now = millis();
  bool changed = rawLen != dev->rawLen || memcmp(raw, dev->raw, rawLen);
  if (!changed || (dev->lastPrintMs && now - dev->lastPrintMs < 1000)) return;
  dev->lastPrintMs = now;
  dev->rawLen = rawLen;
  memcpy(dev->raw, raw, rawLen);

  String line;
  line.reserve(220);
  line += "ADV ";
  line += now;
  line += ' ';
  line += d->getAddress().toString().c_str();
  line += " rssi=";
  line += dev->rssi;
  if (dev->name[0]) {
    line += " name=\"";
    line += dev->name;
    line += '"';
  }
  if (mfr.size()) {
    line += " mfr=";
    hex(line, (const uint8_t *)mfr.data(), mfr.size());
  }
  line += " raw=";
  hex(line, raw, rawLen);
  Serial.println(line);
}

}  // namespace

void begin() {
  Serial.println("[survey] BLUETOOTH SURVEY - passive listening only: no scan requests, no pairing, no connections");
  Serial.println("[survey] format: ADV <ms> <mac> rssi=<dBm> [name] [mfr=<hex>] raw=<hex>; SEEN summary every 60 s");
  ble::begin();
  ble::addListener(onAdvert);
  ble::keepScanning();
}

void loop() {
  static uint32_t next = 60000;
  uint32_t now = millis();
  if ((int32_t)(now - next) < 0) return;
  next = now + 60000;
  Serial.printf("[survey] --- %d device(s) heard after %lu s%s ---\n", gDevCount, (unsigned long)(now / 1000),
                gFull ? " (list full, newer devices not tracked)" : "");
  for (int i = 0; i < gDevCount; i++) {
    const Dev &d = gDev[i];
    // NimBLE stores the address little-endian; print it the usual way round.
    Serial.printf("SEEN %02x:%02x:%02x:%02x:%02x:%02x rssi=%d n=%lu", d.mac[5], d.mac[4], d.mac[3], d.mac[2], d.mac[1],
                  d.mac[0], d.rssi, (unsigned long)d.count);
    if (d.name[0]) Serial.printf(" name=\"%s\"", d.name);
    if (d.hasMfr) Serial.printf(" mfr_id=%04x", d.mfrId);
    Serial.println();
  }
}

}  // namespace survey
}  // namespace hub
