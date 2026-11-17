#include "Bmpro.h"

#include <NimBLEDevice.h>

#include "Ble.h"
#include "BmproAdv.h"
#include "SocEstimator.h"

namespace hub {

Bmpro gBmpro;

namespace {
constexpr int LOG_LINES = 150;
String gLog[LOG_LINES];
int gLogHead = 0, gLogCount = 0;
SemaphoreHandle_t gMux;
bool gVerbose = false;

struct Seen {
  char addr[18];
  char name[24];
  int rssi;
  uint32_t lastMs;
};
constexpr int MAX_SEEN = 24;
Seen gSeen[MAX_SEEN];
int gSeenCount = 0;

NimBLEClient *gClient = nullptr;
char gTargetAddr[18] = {};
volatile bool gRetargeted = false;
volatile bool gDecodedAny = false;

// Decoded data handed from the BLE task to loop(), with arrival times.
BmproData gData;
uint32_t gBatMs = 0, gChgMs = 0, gTankMs[cv::NUM_TANKS] = {}, gPumpMs[cv::NUM_PUMPS] = {}, gGasMs = 0, gLoadMs = 0;

struct WriteReq {
  char uuid[40];
  uint8_t data[64];
  uint8_t len;
  bool response;
};
QueueHandle_t gWriteQ;

void logLine(const String &s) {
  xSemaphoreTake(gMux, portMAX_DELAY);
  gLog[gLogHead] = String(millis() / 1000) + "s " + s;
  gLogHead = (gLogHead + 1) % LOG_LINES;
  if (gLogCount < LOG_LINES) gLogCount++;
  xSemaphoreGive(gMux);
  Serial.println("[bmpro] " + s);
}

String hex(const uint8_t *d, size_t n) {
  String s;
  s.reserve(n * 3);
  char b[4];
  for (size_t i = 0; i < n; i++) {
    sprintf(b, "%02X ", d[i]);
    s += b;
  }
  return s;
}

void applyDecoded(const BmproData &in);

// BMPRO broadcasts (service data 0xFE0E, see BmproAdv.h) carry the battery
// shunt reading and the 12 V output current: read them just by listening.
// The hub locks onto the first battery broadcaster heard closer than -80 dBm
// (Beau's van: -55..-70; the neighbour's: -90s), so a neighbour is ignored.
char gAdvAddr[18] = {};
float gCapacityAh = 0;
// % from the broadcast volts + amps (coulomb counting, synced to 100 % when
// the charger tapers off at full voltage), like the wired shunt.
cv::SocEstimator gSoc;
bool gSocStarted = false;
uint32_t gSocFedMs = 0;

void onBmproBroadcast(NimBLEAdvertisedDevice *d, const std::string &addr) {
  cv::BmproAdvert b;
  if (!cv::bmproParseAdvert(d->getPayload(), d->getPayloadLength(), b)) return;
  if (!gAdvAddr[0]) {
    if (d->getRSSI() >= -80 && b.hasBattery) strncpy(gAdvAddr, addr.c_str(), 17);
    else return;
    logLine(String("reading broadcasts from ") + gAdvAddr);
  }
  if (strcasecmp(gAdvAddr, addr.c_str()) != 0) return;
  static BmproData out;  // NimBLE host task: small stack
  out = BmproData();
  if (b.hasBattery) {
    out.batValid = true;
    out.batV = b.batV;
    out.batA = b.batA;
    out.remainAh = b.remainAh;  // logged only: doesn't match the 405 Ah bank, meaning unknown
    out.batSoc = -1;            // worked out in loop() from volts + amps
  }
  if (b.hasInput) {  // loop() works out loads = input - battery current
    out.chargerValid = true;
    out.chargerA = b.inputA;
  }
  if (out.batValid || out.chargerValid) applyDecoded(out);
}

bool looksBmpro(const std::string &name) {
  String n = String(name.c_str());
  n.toUpperCase();
  return n.indexOf("BMPRO") >= 0 || n.indexOf("ODYSSEY") >= 0 || n.indexOf("COMMLINK") >= 0 ||
         n.indexOf("TREK") >= 0 || n.indexOf("SMART") >= 0;
}

void onAdvert(NimBLEAdvertisedDevice *d) {
  {
    std::string addr = d->getAddress().toString();
    std::string name = d->haveName() ? d->getName() : "";
    xSemaphoreTake(gMux, portMAX_DELAY);
    int slot = -1;
    for (int i = 0; i < gSeenCount; i++)
      if (addr == gSeen[i].addr) slot = i;
    if (slot < 0 && gSeenCount < MAX_SEEN) slot = gSeenCount++;
    if (slot >= 0) {
      strncpy(gSeen[slot].addr, addr.c_str(), 17);
      if (!name.empty()) strncpy(gSeen[slot].name, name.c_str(), 23);
      gSeen[slot].rssi = d->getRSSI();
      gSeen[slot].lastMs = millis();
    }
    xSemaphoreGive(gMux);
    onBmproBroadcast(d, addr);
    // Wireless BMPRO sensors may broadcast their readings in adverts, so log
    // manufacturer + service data of anything BMPRO-looking in full.
    if (gVerbose && (looksBmpro(name) || addr == gTargetAddr)) {
      std::string md = d->getManufacturerData();
      String line = String("ADV ") + addr.c_str() + " '" + name.c_str() + "' rssi " + d->getRSSI() +
                    " mfg: " + hex((const uint8_t *)md.data(), md.size());
      for (int i = 0; i < d->getServiceDataCount(); i++) {
        std::string sd = d->getServiceData(i);
        line += " svc[" + String(d->getServiceDataUUID(i).toString().c_str()) + "]: " +
                hex((const uint8_t *)sd.data(), sd.size());
      }
      logLine(line);
    }
  }
}

void applyDecoded(const BmproData &in) {
  uint32_t now = millis();
  xSemaphoreTake(gMux, portMAX_DELAY);
  if (in.batValid) {
    gData.batValid = true;
    gData.batV = in.batV;
    gData.batA = in.batA;
    gData.batSoc = in.batSoc;
    gData.batTtgMin = in.batTtgMin;
    gData.remainAh = in.remainAh;
    gBatMs = now;
  }
  if (in.chargerValid) {
    gData.chargerValid = true;
    gData.chargerA = in.chargerA;
    gChgMs = now;
  }
  if (in.loadValid) {
    gData.loadValid = true;
    gData.loadA = in.loadA;
    gLoadMs = now;
  }
  for (int i = 0; i < cv::NUM_TANKS; i++)
    if (in.tankValid[i]) {
      gData.tankValid[i] = true;
      gData.tankPct[i] = in.tankPct[i];
      gTankMs[i] = now;
    }
  if (in.gasValid) {
    gData.gasValid = true;
    gData.gasPct = in.gasPct;
    gGasMs = now;
  }
  for (int i = 0; i < cv::NUM_PUMPS; i++)
    if (in.pumpValid[i]) {
      gData.pumpValid[i] = true;
      gData.pumpOn[i] = in.pumpOn[i];
      gPumpMs[i] = now;
    }
  xSemaphoreGive(gMux);
  gDecodedAny = true;
}

void onNotify(NimBLERemoteCharacteristic *c, uint8_t *data, size_t len, bool isNotify) {
  std::string uuid = c->getUUID().toString();
  if (gVerbose) logLine(String(isNotify ? "NTF " : "IND ") + uuid.c_str() + " [" + len + "] " + hex(data, len));
  static BmproData out;  // static: NimBLE host task has a small stack
  out = BmproData();
  if (Bmpro::decode(uuid.c_str(), data, len, out)) applyDecoded(out);
}

void dumpAndSubscribe() {
  std::vector<NimBLERemoteService *> *svcs = gClient->getServices(true);
  for (auto *svc : *svcs) {
    logLine(String("SVC ") + svc->getUUID().toString().c_str());
    std::vector<NimBLERemoteCharacteristic *> *chrs = svc->getCharacteristics(true);
    for (auto *c : *chrs) {
      String props;
      if (c->canRead()) props += "R";
      if (c->canWrite()) props += "W";
      if (c->canWriteNoResponse()) props += "w";
      if (c->canNotify()) props += "N";
      if (c->canIndicate()) props += "I";
      String line = String("  CHR ") + c->getUUID().toString().c_str() + " [" + props + "]";
      if (c->canRead()) {
        std::string v = c->readValue();
        line += " = " + hex((const uint8_t *)v.data(), v.size());
        BmproData out;
        if (Bmpro::decode(c->getUUID().toString().c_str(), (const uint8_t *)v.data(), v.size(), out))
          applyDecoded(out);
      }
      logLine(line);
      if (c->canNotify()) c->subscribe(true, onNotify);
      else if (c->canIndicate()) c->subscribe(false, onNotify);
    }
  }
}

NimBLERemoteCharacteristic *findChar(const char *uuid) {
  if (!gClient || !gClient->isConnected()) return nullptr;
  NimBLEUUID want{std::string(uuid)};
  for (auto *svc : *gClient->getServices(false))
    for (auto *c : *svc->getCharacteristics(false))
      if (c->getUUID() == want) return c;
  return nullptr;
}

void bleTask(void *) {
  uint32_t nextTry = 0;
  for (;;) {
    if (gRetargeted && gClient && gClient->isConnected()) gClient->disconnect();
    gRetargeted = false;

    // Keep a permanent connection to the OdysseyLink (it accepts several
    // centrals at once, so the phone app keeps working alongside us).
    bool up = gClient && gClient->isConnected();
    if (!up && gTargetAddr[0] && (int32_t)(millis() - nextTry) >= 0) {
      nextTry = millis() + 10000;
      if (!gClient) {
        gClient = NimBLEDevice::createClient();
        gClient->setConnectTimeout(8);
      }
      logLine(String("connecting to ") + gTargetAddr);
      ble::pauseScan();
      if (gClient->connect(NimBLEAddress(std::string(gTargetAddr)))) {
        logLine("connected");
        dumpAndSubscribe();
      } else {
        logLine("connect failed - retrying in 10 s");
      }
      ble::resumeScan();
    }

    WriteReq w;
    while (xQueueReceive(gWriteQ, &w, 0) == pdTRUE) {
      NimBLERemoteCharacteristic *c = findChar(w.uuid);
      bool ok = c && c->writeValue(w.data, w.len, w.response);
      logLine(String("WRITE ") + w.uuid + " " + hex(w.data, w.len) + (ok ? "ok" : "FAILED"));
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
}  // namespace

void Bmpro::begin(const char *addr, bool verbose) {
  gMux = xSemaphoreCreateMutex();
  gWriteQ = xQueueCreate(8, sizeof(WriteReq));
  gVerbose = verbose;
  strncpy(gTargetAddr, addr ? addr : "", 17);
  ble::begin();
  ble::addListener(onAdvert);
  // Always scan: the battery reading comes from the BMPRO's broadcasts.
  ble::keepScanning();
  xTaskCreatePinnedToCore(bleTask, "bmpro", 6144, nullptr, 1, nullptr, 0);
  logLine(String("BLE started, target '") + gTargetAddr + "'");
}

void Bmpro::setCapacityAh(float ah) { gCapacityAh = ah; }

void Bmpro::connect(const char *addr) {
  strncpy(gTargetAddr, addr, 17);
  gTargetAddr[17] = 0;
  gRetargeted = true;
}

bool Bmpro::connected() const { return gClient && gClient->isConnected(); }
bool Bmpro::decoded() const { return gDecodedAny; }

float Bmpro::loadA() const {
  xSemaphoreTake(gMux, portMAX_DELAY);
  float a = gData.loadValid && millis() - gLoadMs < 30000 ? gData.loadA : -1;
  xSemaphoreGive(gMux);
  return a;
}

void Bmpro::loop(cv::State &s, bool useBattery, bool useTanks, bool usePumps) {
  constexpr uint32_t STALE_MS = 30000;
  uint32_t now = millis();
  xSemaphoreTake(gMux, portMAX_DELAY);
  if (useBattery) {
    bool fresh = gData.batValid && now - gBatMs < STALE_MS;
    s.bat.online = fresh;
    if (fresh && gData.batSoc < 0 && gBatMs != gSocFedMs) {
      if (!gSocStarted || gSoc.config().capacityAh != gCapacityAh) {
        cv::SocEstimator::Config c;
        c.capacityAh = gCapacityAh > 0 ? gCapacityAh : 300;
        gSoc.begin(c, gSocStarted ? gSoc.soc()
                                  : cv::SocEstimator::socFromRestVoltageLfp(gData.batV));
        gSocStarted = true;
      }
      gSoc.update(gData.batV, gData.batA, gBatMs);
      gSocFedMs = gBatMs;
    }
    if (fresh) {
      s.bat.source = cv::BAT_SRC_BMPRO;
      s.bat.v = gData.batV;
      s.bat.a = gData.batA;
      s.bat.soc = gData.batSoc >= 0 ? gData.batSoc : gSoc.soc();
      s.bat.ttgMin = gData.batSoc >= 0 ? gData.batTtgMin : gSoc.timeToGoMin(gData.batA);
      if (gData.chargerValid && now - gChgMs < STALE_MS) s.bat.chargerA = gData.chargerA;
      s.bat.chargeA = s.bat.solarA + s.bat.chargerA;
      s.bat.loadA = max(0.0f, s.bat.chargeA - s.bat.a);
    }
  }
  if (useTanks)
    for (int i = 0; i < cv::NUM_TANKS; i++) {
      s.tank[i].online = gData.tankValid[i] && now - gTankMs[i] < STALE_MS;
      if (s.tank[i].online) s.tank[i].pct = gData.tankPct[i];
    }
  // Gas bottle (SmartSense): goes online once decode() learns its frame.
  s.gas.online = gData.gasValid && now - gGasMs < STALE_MS;
  if (s.gas.online) s.gas.pct = gData.gasPct;
  if (usePumps)
    for (int i = 0; i < cv::NUM_PUMPS; i++) {
      bool fresh = gData.pumpValid[i] && now - gPumpMs[i] < STALE_MS;
      s.pump[i].sensed = fresh && gData.pumpOn[i];
    }
  xSemaphoreGive(gMux);
}

bool Bmpro::write(const char *charUuid, const uint8_t *data, size_t len, bool response) {
  if (!gWriteQ || len > sizeof(WriteReq::data)) return false;
  WriteReq w = {};
  strncpy(w.uuid, charUuid, sizeof(w.uuid) - 1);
  memcpy(w.data, data, len);
  w.len = len;
  w.response = response;
  return xQueueSend(gWriteQ, &w, 0) == pdTRUE;
}

bool Bmpro::setPump(int i, bool on, const char **why) {
  String uuid;
  uint8_t buf[64];
  size_t len = sizeof(buf);
  if (!encodePump(i, on, uuid, buf, len)) {
    if (why) *why = "BMPRO pump command not decoded yet - see docs/BMPRO_INTEGRATION.md";
    return false;
  }
  if (!connected()) {
    if (why) *why = "Not connected to the OdysseyLink";
    return false;
  }
  return write(uuid.c_str(), buf, len, true);
}

String Bmpro::log() const {
  String out;
  xSemaphoreTake(gMux, portMAX_DELAY);
  int start = (gLogHead - gLogCount + LOG_LINES) % LOG_LINES;
  for (int i = 0; i < gLogCount; i++) out += gLog[(start + i) % LOG_LINES] + "\n";
  xSemaphoreGive(gMux);
  return out;
}

String Bmpro::devices() const {
  String out = "[";
  xSemaphoreTake(gMux, portMAX_DELAY);
  for (int i = 0; i < gSeenCount; i++) {
    if (i) out += ",";
    out += String("{\"addr\":\"") + gSeen[i].addr + "\",\"name\":\"" + gSeen[i].name +
           "\",\"rssi\":" + gSeen[i].rssi + ",\"age\":" + (millis() - gSeen[i].lastMs) / 1000 + "}";
  }
  xSemaphoreGive(gMux);
  return out + "]";
}

// ===========================================================================
// PROTOCOL HOOKS - intentionally empty until real frames are captured.
//
// decode(): called for every notification and every readable characteristic.
// Fill in whatever the frame carries and return true, e.g.
//
//   if (!strcmp(charUuid, "0000xxxx-0000-1000-8000-00805f9b34fb") && len >= 12 && data[0] == 0x01) {
//     out.batValid = true;
//     out.batV   = (data[2] | data[3] << 8) / 100.0f;           // volts
//     out.batA   = (int16_t)(data[4] | data[5] << 8) / 10.0f;   // +charge / -discharge
//     out.batSoc = data[6];
//     for (int t = 0; t < 3; t++) { out.tankValid[t] = true; out.tankPct[t] = data[7 + t]; }
//     out.pumpValid[0] = out.pumpValid[1] = true;
//     out.pumpOn[0] = data[10] & 0x01;  out.pumpOn[1] = data[10] & 0x02;
//     return true;
//   }
//   // SmartSense gas bottle level (relayed by the OdysseyLink), e.g.
//   if (... SmartSense frame ...) { out.gasValid = true; out.gasPct = data[n]; return true;
//   }
//
// encodePump(): the bytes the Odyssey app writes to switch a pump (from an
// Android HCI snoop log), e.g.
//
//   charUuid = "0000yyyy-0000-1000-8000-00805f9b34fb";
//   buf[0] = 0x10; buf[1] = pump; buf[2] = on; len = 3;
//   return true;
// ===========================================================================
bool Bmpro::decode(const char *charUuid, const uint8_t *data, size_t len, BmproData &out) {
  (void)charUuid;
  (void)data;
  (void)len;
  (void)out;
  return false;
}

bool Bmpro::encodePump(int pump, bool on, String &charUuid, uint8_t *buf, size_t &len) {
  (void)pump;
  (void)on;
  (void)charUuid;
  (void)buf;
  len = 0;
  return false;
}

}  // namespace hub
