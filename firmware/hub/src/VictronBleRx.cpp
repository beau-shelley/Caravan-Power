#include "VictronBleRx.h"

#include <NimBLEDevice.h>

#include "Ble.h"
#include "BleTag.h"
#include "VictronBle.h"

namespace hub {

VictronBleRx gVictronBle;

namespace {
struct Slot {
  bool configured = false;
  char addr[18] = {};
  uint8_t key[16] = {};
  cv::Solar solar;
  uint32_t lastMs = 0;
  uint32_t packets = 0;
  uint32_t lastLogMs = 0;
};
Slot gSlots[cv::NUM_SOLAR];

struct SeenDev {
  char addr[18];
  uint16_t model;
  uint8_t record;
  int rssi;
  int8_t slot;
  uint32_t lastMs;
  uint32_t hintMs;  // last "key doesn't match" log line
};
constexpr int MAX_SEEN = 8;
SeenDev gSeen[MAX_SEEN];
int gSeenCount = 0;
portMUX_TYPE gLock = portMUX_INITIALIZER_UNLOCKED;

void onAdvert(NimBLEAdvertisedDevice *d) {
  if (!d->haveManufacturerData()) return;
  std::string md = d->getManufacturerData();
  cv::VictronAdvert adv;
  if (!cv::victronParseAdvert((const uint8_t *)md.data(), md.size(), adv)) return;
  std::string addr = d->getAddress().toString();  // lower-case aa:bb:...

  int8_t matched = -1;
  if (adv.record == cv::VREC_SOLAR_CHARGER) {
    for (int i = 0; i < cv::NUM_SOLAR; i++) {
      Slot &s = gSlots[i];
      if (!s.configured || adv.keyCheck != s.key[0]) continue;
      if (s.addr[0] && strcasecmp(s.addr, addr.c_str()) != 0) continue;
      uint8_t dec[32];
      size_t n = 0;
      if (!cv::victronDecrypt(adv, s.key, dec, n)) continue;
      cv::Solar tmp;
      portENTER_CRITICAL(&gLock);
      tmp = s.solar;
      portEXIT_CRITICAL(&gLock);
      if (!cv::victronDecodeSolar(dec, n, tmp)) continue;
      // Sanity check guards against a 1-in-256 key-byte collision.
      if (tmp.batV < 8 || tmp.batV > 70) continue;
      portENTER_CRITICAL(&gLock);
      s.solar = tmp;
      s.lastMs = millis();
      s.packets++;
      portEXIT_CRITICAL(&gLock);
      matched = i;
      // Readings to the USB log every 10 s, so a van visit shows it working.
      if (!s.lastLogMs || millis() - s.lastLogMs >= 10000) {
        s.lastLogMs = millis();
        Serial.printf("[victron] solar %d %s: %.2f V %.1f A %.0f W, today %.2f kWh, state %u, rssi %d\n", i + 1,
                      addr.c_str(), tmp.batV, tmp.batA, tmp.pvW, tmp.yieldTodayKwh, tmp.cs, d->getRSSI());
      }
      break;
    }
  }

  // Remember Victron devices in range (helps finding addresses during setup).
  portENTER_CRITICAL(&gLock);
  int idx = -1;
  for (int i = 0; i < gSeenCount; i++)
    if (!strcmp(gSeen[i].addr, addr.c_str())) idx = i;
  if (idx < 0 && gSeenCount < MAX_SEEN) idx = gSeenCount++;
  if (idx >= 0) {
    strncpy(gSeen[idx].addr, addr.c_str(), 17);
    gSeen[idx].addr[17] = 0;
    gSeen[idx].model = adv.model;
    gSeen[idx].record = adv.record;
    gSeen[idx].rssi = d->getRSSI();
    bool fresh = gSeen[idx].lastMs == 0;
    if (fresh) gSeen[idx].hintMs = 0;
    if (matched >= 0 || fresh) gSeen[idx].slot = matched;
    gSeen[idx].lastMs = millis();
  }
  // Heard a Victron that no saved key opens: say so once a minute, with the
  // first key byte it expects (handy for spotting a mistyped key).
  bool hint = false;
  if (matched < 0 && idx >= 0 && (!gSeen[idx].hintMs || millis() - gSeen[idx].hintMs >= 60000)) {
    gSeen[idx].hintMs = millis();
    hint = true;
  }
  portEXIT_CRITICAL(&gLock);
  if (hint)
    Serial.printf("[victron] %s (%s, model 0x%04X) heard, but no saved key matches - its key starts with %02x. "
                  "Enter it on the phone: Setup > Victron solar controllers\n",
                  addr.c_str(), cv::victronRecordName(adv.record), adv.model, adv.keyCheck);
}
}  // namespace

void VictronBleRx::begin() {
  ble::begin();
  ble::addListener(onAdvert);
  ble::keepScanning();
  Serial.println("[victron] listening for Instant Readout broadcasts");
}

bool VictronBleRx::setDevice(int i, const char *addr, const char *keyHex) {
  if (i < 0 || i >= cv::NUM_SOLAR) return false;
  uint8_t key[16];
  bool ok = keyHex && keyHex[0] && cv::victronKeyFromHex(keyHex, key);
  portENTER_CRITICAL(&gLock);
  Slot &s = gSlots[i];
  s.configured = ok;
  if (ok) memcpy(s.key, key, 16);
  strncpy(s.addr, addr ? addr : "", 17);
  s.addr[17] = 0;
  s.lastMs = 0;
  portEXIT_CRITICAL(&gLock);
  return ok;
}

void VictronBleRx::poll(int i, cv::Solar &out) {
  portENTER_CRITICAL(&gLock);
  const Slot &s = gSlots[i];
  bool fresh = s.lastMs && millis() - s.lastMs < 30000;
  if (fresh) out = s.solar;
  portEXIT_CRITICAL(&gLock);
  out.online = fresh;
}

void VictronBleRx::resetDaily() {
  portENTER_CRITICAL(&gLock);
  for (auto &s : gSlots) s.solar.maxWToday = 0;
  portEXIT_CRITICAL(&gLock);
}

String VictronBleRx::seen() const {
  SeenDev copy[MAX_SEEN];
  int n;
  portENTER_CRITICAL(&gLock);
  n = gSeenCount;
  memcpy(copy, gSeen, sizeof(copy));
  portEXIT_CRITICAL(&gLock);
  String out = "[";
  for (int i = 0; i < n; i++) {
    char buf[160];
    snprintf(buf, sizeof(buf),
             "%s{\"addr\":\"%s\",\"model\":\"0x%04X\",\"type\":\"%s\",\"rssi\":%d,\"slot\":%d,\"age\":%lu}",
             i ? "," : "", copy[i].addr, copy[i].model,
             copy[i].record == cv::VREC_SOLAR_CHARGER ? "solar" : "other", copy[i].rssi, copy[i].slot,
             (unsigned long)((millis() - copy[i].lastMs) / 1000));
    out += buf;
  }
  return out + "]";
}

}  // namespace hub
