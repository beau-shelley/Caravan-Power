#include "BleTag.h"

#include <cctype>
#include <cstdio>
#include <cstring>

#include "BleSensors.h"
#include "BmproAdv.h"
#include "VictronBle.h"

namespace cv {

namespace {

bool nameHas(const char *name, const char *word) {
  // Case-insensitive substring search.
  size_t n = strlen(name), w = strlen(word);
  for (size_t i = 0; i + w <= n; i++) {
    size_t k = 0;
    while (k < w && tolower((unsigned char)name[i + k]) == tolower((unsigned char)word[k])) k++;
    if (k == w) return true;
  }
  return false;
}

// Company ids of devices that are never part of the van system.
bool noiseCompany(uint16_t id) {
  switch (id) {
    case 0x004C:  // Apple (iPhones, AirPods, AirTags, Macs)
    case 0x0006:  // Microsoft (Windows "Swift Pair", Surface)
    case 0x0075:  // Samsung
    case 0x00E0:  // Google
    case 0x0087:  // Garmin
    case 0x0157:  // Huami / Amazfit
    case 0x027D:  // Huawei
    case 0x038F:  // Xiaomi phones
    case 0x0171:  // Amazon
      return true;
    default: return false;
  }
}

bool noiseService(uint16_t u) {
  switch (u) {
    case 0xFD6F:  // COVID exposure notifications
    case 0xFE9F:  // Google
    case 0xFEF3:  // Google
    case 0xFE2C:  // Google Fast Pair
    case 0xFD5A:  // Samsung
    case 0xFE07:  // Sonos
    case 0xFEAA:  // Eddystone beacons
      return true;
    default: return false;
  }
}

}  // namespace

const char *victronRecordName(uint8_t record) {
  switch (record) {
    case VREC_SOLAR_CHARGER: return "solar";
    case VREC_BATTERY_MONITOR: return "battery";
    case VREC_INVERTER: return "inverter";
    case VREC_DCDC: return "dcdc";
    default: return "other";
  }
}

void bleClassify(const uint8_t *raw, size_t len, BleTag &tag) {
  tag = BleTag();
  uint16_t svc[8];
  int nSvc = 0;
  bool heaterSvc = false, noiseSvc = false;
  const uint8_t *svcData[4];
  size_t svcDataLen[4];
  uint16_t svcDataUuid[4];
  int nSvcData = 0;

  for (size_t i = 0; i < len;) {
    uint8_t l = raw[i];
    if (l == 0 || i + 1 + l > len) break;
    uint8_t type = raw[i + 1];
    const uint8_t *d = raw + i + 2;
    size_t dl = l - 1;
    switch (type) {
      case 0x08:
      case 0x09: {  // short / complete local name
        size_t n = dl < sizeof(tag.name) - 1 ? dl : sizeof(tag.name) - 1;
        if (type == 0x09 || !tag.name[0]) {
          memcpy(tag.name, d, n);
          tag.name[n] = 0;
        }
        break;
      }
      case 0x02:
      case 0x03:  // 16-bit service UUIDs
        for (size_t k = 0; k + 1 < dl; k += 2) {
          uint16_t u = d[k] | (d[k + 1] << 8);
          if (u == 0xFFE0) heaterSvc = true;
          if (noiseService(u)) noiseSvc = true;
          if (nSvc < 8) svc[nSvc++] = u;
        }
        break;
      case 0x16:  // service data, 16-bit UUID
        if (dl >= 2) {
          uint16_t u = d[0] | (d[1] << 8);
          if (noiseService(u)) noiseSvc = true;
          if (nSvcData < 4) {
            svcDataUuid[nSvcData] = u;
            svcData[nSvcData] = d + 2;
            svcDataLen[nSvcData] = dl - 2;
            nSvcData++;
          }
        }
        break;
      case 0xFF:  // manufacturer data
        if (dl >= 2) {
          tag.mfr = d;
          tag.mfrLen = dl;
          tag.mfrId = d[0] | (d[1] << 8);
        }
        break;
    }
    i += 1 + l;
  }
  (void)svc;

  // 1. Victron: encrypted Instant Readout, recognisable without the key.
  VictronAdvert va;
  if (tag.mfr && victronParseAdvert(tag.mfr, tag.mfrLen, va)) {
    tag.kind = BLE_VICTRON;
    snprintf(tag.label, sizeof(tag.label), "victron-%s", victronRecordName(va.record));
    return;
  }
  if (tag.mfr && tag.mfrId == VICTRON_COMPANY_ID) {  // Victron, but not a readout (e.g. pairing beacon)
    tag.kind = BLE_VICTRON;
    snprintf(tag.label, sizeof(tag.label), "victron");
    return;
  }
  // 2. BMPRO broadcast (service data 0xFE0E, see BmproAdv.h).
  for (int k = 0; k < nSvcData; k++)
    if (svcDataUuid[k] == BMPRO_SVC_UUID && svcDataLen[k] >= 1) {
      tag.kind = BLE_BMPRO;
      uint8_t f = svcData[k][0];
      snprintf(tag.label, sizeof(tag.label), "%s",
               f == 0x01 ? "bmpro-battery" : f == 0x04 ? "bmpro-outputs" : f == 0x00 ? "bmpro-id" : "bmpro");
      return;
    }
  // 2b. A thermometer the hub already understands.
  BleSensorReading r;
  for (int k = 0; k < nSvcData; k++)
    if (decodeBleServiceData(svcDataUuid[k], svcData[k], svcDataLen[k], r)) break;
  if (!r.hasTemp && tag.mfr) decodeBleManufacturerData(tag.name, tag.mfr, tag.mfrLen, r);
  if (r.hasTemp) {
    tag.kind = BLE_THERMOMETER;
    tag.hasTemp = true;
    tag.tempC = r.tempC;
    snprintf(tag.label, sizeof(tag.label), "thermometer");
    return;
  }
  // 3. BMPRO, by name (OdysseyLink, CommLink, SmartSense gas, BC300 shunt).
  static const char *bmproWords[] = {"bmpro", "odyssey", "commlink", "smartsense", "smart sense", "bc300",
                                     "batteryplus", "jhub", "trek"};
  for (const char *w : bmproWords)
    if (nameHas(tag.name, w)) {
      tag.kind = BLE_BMPRO;
      snprintf(tag.label, sizeof(tag.label), "bmpro?");
      return;
    }
  // 4. Diesel heater (AirHeaterBLE apps use service FFE0).
  if (heaterSvc || nameHas(tag.name, "heater")) {
    tag.kind = BLE_HEATER;
    snprintf(tag.label, sizeof(tag.label), "heater?");
    return;
  }
  // 5. Everyday gadgets.
  if ((tag.mfr && noiseCompany(tag.mfrId)) || noiseSvc) {
    tag.kind = BLE_NOISE;
    snprintf(tag.label, sizeof(tag.label), "noise");
    return;
  }
  snprintf(tag.label, sizeof(tag.label), "?");
}

}  // namespace cv
