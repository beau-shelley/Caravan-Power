#include "BleTempRx.h"

#include <NimBLEDevice.h>

#include "Ble.h"
#include "BleSensors.h"
#include "HubConfig.h"

namespace hub {

BleTempRx gBleTemps;

namespace {
// Thermometers advertise every few seconds; temperatures change slowly, so
// only call a sensor offline after 10 minutes of silence (a fridge's metal
// cabinet can block many packets).
constexpr uint32_t STALE_MS = 10UL * 60 * 1000;

struct Dev {
  char addr[18];
  char name[20];
  const char *type;
  float tempC, hum;
  int8_t batt;  // -1 unknown
  int rssi;
  uint32_t lastMs;  // 0 = name cached only, no reading yet
  char raw[125];    // last advert + scan response payload as hex (first 62 bytes), for checking a decoder
};
constexpr int MAX_DEVS = 24;
Dev gDevs[MAX_DEVS];
int gDevCount = 0;
portMUX_TYPE gLock = portMUX_INITIALIZER_UNLOCKED;

// Find or create a slot for this address (oldest entry is recycled).
Dev *slotFor(const char *addr) {
  for (int i = 0; i < gDevCount; i++)
    if (!strcmp(gDevs[i].addr, addr)) return &gDevs[i];
  int idx;
  if (gDevCount < MAX_DEVS) {
    idx = gDevCount++;
  } else {
    idx = 0;
    for (int i = 1; i < MAX_DEVS; i++)
      if (gDevs[i].lastMs < gDevs[idx].lastMs) idx = i;
  }
  memset(&gDevs[idx], 0, sizeof(Dev));
  strncpy(gDevs[idx].addr, addr, 17);
  gDevs[idx].type = "";
  gDevs[idx].batt = -1;
  return &gDevs[idx];
}

void onAdvert(NimBLEAdvertisedDevice *d) {
  std::string addrS = d->getAddress().toString();
  const char *addr = addrS.c_str();
  std::string nameS = d->haveName() ? d->getName() : "";

  // Names often arrive in a separate scan response: remember them, because
  // ThermoPro is recognised by name.
  char name[20] = "";
  portENTER_CRITICAL(&gLock);
  for (int i = 0; i < gDevCount; i++)
    if (!strcmp(gDevs[i].addr, addr)) strncpy(name, gDevs[i].name, sizeof(name) - 1);
  portEXIT_CRITICAL(&gLock);
  bool newName = !nameS.empty() && strncmp(name, nameS.c_str(), sizeof(name) - 1) != 0;
  if (!nameS.empty()) strncpy(name, nameS.c_str(), sizeof(name) - 1);

  cv::BleSensorReading r;
  bool ok = false;
  for (int i = 0; !ok && i < (int)d->getServiceDataCount(); i++) {
    NimBLEUUID u = d->getServiceDataUUID(i);
    if (u.bitSize() != 16) continue;
    std::string sd = d->getServiceData(i);
    ok = cv::decodeBleServiceData(u.getNative()->u16.value, (const uint8_t *)sd.data(), sd.size(), r);
  }
  if (!ok && d->haveManufacturerData()) {
    std::string md = d->getManufacturerData();
    ok = cv::decodeBleManufacturerData(name, (const uint8_t *)md.data(), md.size(), r);
  }
  if (!ok && !(newName && !strncmp(name, "TP3", 3))) return;

  char raw[125] = "";
  if (ok) {
    const uint8_t *p = d->getPayload();
    size_t n = d->getPayloadLength() < 62 ? d->getPayloadLength() : 62;
    for (size_t k = 0; k < n; k++) snprintf(raw + k * 2, 3, "%02x", p[k]);
  }

  portENTER_CRITICAL(&gLock);
  Dev *dev = slotFor(addr);
  if (name[0]) strncpy(dev->name, name, sizeof(dev->name) - 1);
  if (ok) {
    dev->type = r.type;
    dev->tempC = r.tempC;
    dev->hum = r.hasHum ? r.humidity : -1;
    if (r.hasBatt) dev->batt = (int8_t)r.battPct;
    dev->rssi = d->getRSSI();
    dev->lastMs = millis();
    memcpy(dev->raw, raw, sizeof(dev->raw));
  }
  portEXIT_CRITICAL(&gLock);
}
}  // namespace

void BleTempRx::begin() {
  ble::begin();
  ble::addListener(onAdvert);
  ble::keepScanning();
  Serial.println("[temp] listening for Bluetooth thermometers");
}

void BleTempRx::poll(cv::State &s) {
  uint32_t now = millis();
  for (int role = 0; role < cv::NUM_TEMPS; role++) {
    if (gConfig.tempSrc[role] != TEMP_BLE) continue;
    cv::Temp &t = s.temp[role];
    bool found = false;
    float c = 0;
    int8_t batt = -1;
    portENTER_CRITICAL(&gLock);
    for (int i = 0; i < gDevCount; i++) {
      const Dev &d = gDevs[i];
      if (d.lastMs && strcasecmp(d.addr, gConfig.tempBle[role]) == 0 && now - d.lastMs < STALE_MS) {
        found = true;
        c = d.tempC;
        batt = d.batt;
      }
    }
    portEXIT_CRITICAL(&gLock);
    if (!found) {
      t.online = false;
      continue;
    }
    if (!t.online && t.minC == 0 && t.maxC == 0) t.minC = t.maxC = c;
    t.online = true;
    t.c = c;
    t.batt = batt;
    if (c < t.minC) t.minC = c;
    if (c > t.maxC) t.maxC = c;
  }
}

String BleTempRx::seen() const {
  Dev copy[MAX_DEVS];
  int n;
  portENTER_CRITICAL(&gLock);
  n = gDevCount;
  memcpy(copy, gDevs, sizeof(Dev) * n);
  portEXIT_CRITICAL(&gLock);
  String out = "[";
  bool first = true;
  for (int i = 0; i < n; i++) {
    const Dev &d = copy[i];
    if (!d.lastMs) continue;
    int role = -1;
    for (int r = 0; r < cv::NUM_TEMPS; r++)
      if (gConfig.tempSrc[r] == TEMP_BLE && !strcasecmp(gConfig.tempBle[r], d.addr)) role = r;
    char buf[340];
    snprintf(buf, sizeof(buf),
             "%s{\"addr\":\"%s\",\"name\":\"%s\",\"type\":\"%s\",\"c\":%.1f,\"hum\":%.0f,\"batt\":%d,\"rssi\":%d,"
             "\"age\":%lu,\"role\":%d,\"raw\":\"%s\"}",
             first ? "" : ",", d.addr, d.name, d.type, d.tempC, d.hum, d.batt, d.rssi,
             (unsigned long)((millis() - d.lastMs) / 1000), role, d.raw);
    out += buf;
    first = false;
  }
  return out + "]";
}

}  // namespace hub
