#include "BleSurvey.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include "Ble.h"
#include "BleTag.h"
#include "BmproAdv.h"

namespace hub {
namespace survey {
namespace {

constexpr int MAX_DEV = 64;
constexpr size_t MAX_RAW = 62;  // advert + scan response never exceed this

struct Dev {
  uint8_t mac[6];
  uint32_t lastPrintMs = 0;
  uint32_t lastHeardMs = 0;
  uint32_t count = 0;
  uint32_t lines = 0;  // distinct payloads printed
  int8_t rssi = 0;
  uint8_t rawLen = 0;
  uint8_t raw[MAX_RAW];
  char name[24] = {0};
  char label[24] = {0};
  cv::BleKind kind = cv::BLE_UNKNOWN;
  uint16_t mfrId = 0;
  bool hasMfr = false;
};

Dev gDev[MAX_DEV];
int gDevCount = 0;
uint32_t gNoiseAdverts = 0;  // phones, laptops, earbuds... counted, not logged
uint32_t gEvicted = 0;
portMUX_TYPE gLock = portMUX_INITIALIZER_UNLOCKED;

void hex(String &out, const uint8_t *p, size_t n) {
  static const char *h = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) {
    out += h[p[i] >> 4];
    out += h[p[i] & 15];
  }
}

// Our devices first in the summary, unknowns after.
int rank(cv::BleKind k) {
  switch (k) {
    case cv::BLE_BMPRO: return 0;
    case cv::BLE_VICTRON: return 1;
    case cv::BLE_HEATER: return 2;
    case cv::BLE_THERMOMETER: return 3;
    default: return 4;
  }
}

// A slot for this address. When the list is full, the unknown device heard
// longest ago (and at least 2 minutes ago) makes room - phones in a caravan
// park change address every few minutes and would otherwise fill the list.
Dev *slotFor(const uint8_t *mac, cv::BleKind kind, uint32_t now) {
  for (int i = 0; i < gDevCount; i++)
    if (!memcmp(gDev[i].mac, mac, 6)) return &gDev[i];
  Dev *d = nullptr;
  if (gDevCount < MAX_DEV) {
    d = &gDev[gDevCount++];
  } else {
    uint32_t oldest = 0;
    for (int i = 0; i < MAX_DEV; i++) {
      Dev &c = gDev[i];
      uint32_t age = now - c.lastHeardMs;
      if (c.kind == cv::BLE_UNKNOWN && age > 120000 && age > oldest) {
        oldest = age;
        d = &c;
      }
    }
    if (!d) return nullptr;
    gEvicted++;
  }
  *d = Dev();
  memcpy(d->mac, mac, 6);
  d->kind = kind;
  return d;
}

// Called from the NimBLE host task for every advert heard.
void onAdvert(NimBLEAdvertisedDevice *d) {
  const uint8_t *mac = d->getAddress().getNative();
  const uint8_t *raw = d->getPayload();
  size_t rawLen = d->getPayloadLength();
  if (rawLen > MAX_RAW) rawLen = MAX_RAW;

  cv::BleTag tag;
  cv::bleClassify(raw, rawLen, tag);
  uint32_t now = millis();

  portENTER_CRITICAL(&gLock);
  if (tag.kind == cv::BLE_NOISE) {
    gNoiseAdverts++;
    portEXIT_CRITICAL(&gLock);
    return;
  }
  Dev *dev = slotFor(mac, tag.kind, now);
  if (!dev) {
    portEXIT_CRITICAL(&gLock);
    return;
  }
  dev->count++;
  dev->lastHeardMs = now;
  dev->rssi = d->getRSSI();
  // A device keeps its best label (a name may only come in some adverts).
  if (tag.kind != cv::BLE_UNKNOWN || !dev->label[0]) {
    dev->kind = tag.kind;
    snprintf(dev->label, sizeof(dev->label), "%s", tag.label);
  }
  if (tag.name[0]) snprintf(dev->name, sizeof(dev->name), "%s", tag.name);
  if (tag.mfr) {
    dev->hasMfr = true;
    dev->mfrId = tag.mfrId;
  }
  // Print only new payloads: our devices at most once a second, unknown
  // ones once every 5 s (they are only there in case a BMPRO part has no name).
  uint32_t gap = dev->kind == cv::BLE_UNKNOWN ? 5000 : 1000;
  bool changed = rawLen != dev->rawLen || memcmp(raw, dev->raw, rawLen);
  bool print = changed && !(dev->lastPrintMs && now - dev->lastPrintMs < gap);
  if (print) {
    dev->lastPrintMs = now;
    dev->rawLen = rawLen;
    dev->lines++;
    memcpy(dev->raw, raw, rawLen);
  }
  char name[24], label[24];
  memcpy(name, dev->name, sizeof(name));
  memcpy(label, dev->label, sizeof(label));
  int8_t rssi = dev->rssi;
  portEXIT_CRITICAL(&gLock);

  // BMPRO broadcasts we can read: a plain-English line every 10 s.
  if (tag.kind == cv::BLE_BMPRO) {
    static uint32_t lastBat = 0, lastOut = 0;
    cv::BmproAdvert b;
    if (cv::bmproParseAdvert(raw, rawLen, b)) {
      if (b.hasBattery && (!lastBat || now - lastBat >= 10000)) {
        lastBat = now;
        Serial.printf("[bmpro] %s battery %.2f V %+.2f A, counter %.3f Ah (b11=%u b12=%u), rssi %d\n",
                      d->getAddress().toString().c_str(), b.batV, b.batA, b.remainAh, b.b11, b.b12, rssi);
      } else if (b.hasLoad && (!lastOut || now - lastOut >= 10000)) {
        lastOut = now;
        Serial.printf("[bmpro] %s outputs load %.2f A, extra %u, flags %04x\n", d->getAddress().toString().c_str(),
                      b.loadA, b.outExtra, b.outFlags);
      }
    }
  }
  if (!print) return;

  String line;
  line.reserve(240);
  line += "ADV ";
  line += now;
  line += ' ';
  line += d->getAddress().toString().c_str();
  line += " rssi=";
  line += rssi;
  line += " tag=";
  line += label;
  if (name[0]) {
    line += " name=\"";
    line += name;
    line += '"';
  }
  if (tag.hasTemp) {
    line += " temp=";
    line += String(tag.tempC, 1);
  }
  if (tag.mfr) {
    line += " mfr=";
    hex(line, tag.mfr, tag.mfrLen);
  }
  line += " raw=";
  hex(line, raw, rawLen);
  Serial.println(line);
}

}  // namespace

void begin() {
  Serial.println("[survey] BLUETOOTH SURVEY - passive listening only: no scan requests, no pairing, no connections");
  Serial.println("[survey] format: ADV <ms> <mac> rssi=<dBm> tag=<what> [name] [mfr=<hex>] raw=<hex>; SEEN summary every 60 s");
  Serial.println("[survey] tags: bmpro? victron-solar/-battery/-dcdc heater? thermometer ?=unknown (phones etc. are skipped)");
  ble::begin();
  ble::addListener(onAdvert);
  ble::keepScanning();
}

void loop() {
  static uint32_t next = 60000;
  uint32_t now = millis();
  if ((int32_t)(now - next) < 0) return;
  next = now + 60000;

  static Dev copy[MAX_DEV];  // static: ~6 KB, too big for the loop task's stack
  portENTER_CRITICAL(&gLock);
  int n = gDevCount;
  memcpy(copy, gDev, sizeof(Dev) * n);
  uint32_t noise = gNoiseAdverts, evicted = gEvicted;
  portEXIT_CRITICAL(&gLock);

  Serial.printf("[survey] --- %d device(s) heard after %lu s; %lu adverts from phones/laptops skipped",
                n, (unsigned long)(now / 1000), (unsigned long)noise);
  if (evicted) Serial.printf("; %lu old unknown devices dropped", (unsigned long)evicted);
  Serial.println(" ---");
  for (int r = 0; r <= 4; r++)
    for (int i = 0; i < n; i++) {
      const Dev &d = copy[i];
      if (rank(d.kind) != r) continue;
      // NimBLE stores the address little-endian; print it the usual way round.
      Serial.printf("SEEN %02x:%02x:%02x:%02x:%02x:%02x tag=%s rssi=%d n=%lu payloads=%lu age=%lus",
                    d.mac[5], d.mac[4], d.mac[3], d.mac[2], d.mac[1], d.mac[0], d.label, d.rssi,
                    (unsigned long)d.count, (unsigned long)d.lines, (unsigned long)((now - d.lastHeardMs) / 1000));
      if (d.name[0]) Serial.printf(" name=\"%s\"", d.name);
      if (d.hasMfr) Serial.printf(" mfr_id=%04x", d.mfrId);
      Serial.println();
    }
}

}  // namespace survey
}  // namespace hub
