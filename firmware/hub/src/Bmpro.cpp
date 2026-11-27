#include "Bmpro.h"

#include <NimBLEDevice.h>
#include "BleCompat.h"

#include "Ble.h"
#include "Comms.h"
#include "BmproAdv.h"
#include "BmproLink.h"

namespace hub {

Bmpro gBmpro;

namespace {
// Kept for the bmpro_log command. 40 lines of at most 120 characters: the
// ring used to hold 150 full lines, and with the 20 s summary (~260
// characters) in it the hub's free RAM fell ~770 B/min for 50 minutes
// (2026-11-19) - about 40 KB on a board that has none to spare.
constexpr int LOG_LINES = 40;
constexpr unsigned LOG_LINE_MAX = 120;
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
// The OdysseyLink uses a random (static) Bluetooth address: connecting to it
// as a public address times out (status 13). Take the type from its adverts;
// random is the right guess for BMPRO gear until one is heard.
volatile uint8_t gTargetType = BLE_ADDR_RANDOM;
volatile bool gRetargeted = false;
volatile bool gDecodedAny = false;
volatile uint32_t gFrames = 0;
// Voltage readings side by side, for matching the Trek3's figure.
volatile float gVBcast = 0, gVConn = 0, gV2 = 0, gABcast = 0, gAConn = 0;
// Last battery reading over the connection. While it is fresh the broadcast is
// ignored: its voltage updates only every few minutes (13.28 V while the
// connection and the Trek3 said 13.05-13.1 V, 2026-11-18).
volatile uint32_t gConnBatMs = 0;  // notifications from the OdysseyLink's data characteristic

// Decoded data handed from the BLE task to loop(), with arrival times.
BmproData gData;
uint32_t gSocMs = 0;  // last % straight from the BMPRO (connection)
uint32_t gBatMs = 0, gChgMs = 0, gTankMs[cv::NUM_TANKS] = {}, gPumpMs[cv::NUM_PUMPS] = {}, gGasMs = 0, gLoadMs = 0;

struct WriteReq {
  char uuid[40];
  uint8_t data[64];
  uint8_t len;
  bool response;
};
QueueHandle_t gWriteQ;

void logLine(const String &s, bool keep = true) {
  if (keep) {
    xSemaphoreTake(gMux, portMAX_DELAY);
    String line = String(millis() / 1000) + "s " + s;
    gLog[gLogHead] = line.length() > LOG_LINE_MAX ? line.substring(0, LOG_LINE_MAX) : line;
    gLogHead = (gLogHead + 1) % LOG_LINES;
    if (gLogCount < LOG_LINES) gLogCount++;
    xSemaphoreGive(gMux);
  }
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
// Last battery sample handed to the hub's own % counter (takeBattery()).
uint32_t gBatTakenMs = 0;

// No OdysseyLink address set up yet: pick it out by its advert (service
// 0xFE0E listed, no service data - the broadcaster carries the data) when it
// is close (> -80 dBm), so the van works without setting anything up.
void autoTarget(const ble::Advert &a) {
  if (gTargetAddr[0] || a.rssi < -80 || a.dev->getServiceDataCount() > 0) return;
  if (!a.dev->isAdvertisingService(NimBLEUUID((uint16_t)cv::BMPRO_SVC_UUID))) return;
  strncpy(gTargetAddr, a.addr, 17);
  gTargetAddr[17] = 0;
  gTargetType = a.dev->getAddress().getType();
  logLine(String("found the OdysseyLink at ") + gTargetAddr + " (rssi " + a.rssi + ") - connecting");
}

void onBmproBroadcast(const ble::Advert &a) {
  autoTarget(a);
  cv::BmproAdvert b;
  if (!cv::bmproParseAdvert(a.raw, a.rawLen, b)) return;
  if (!gAdvAddr[0]) {
    if (a.rssi >= -80 && b.hasBattery) strncpy(gAdvAddr, a.addr, 17);
    else return;
    logLine(String("reading broadcasts from ") + gAdvAddr);
  }
  if (strcasecmp(gAdvAddr, a.addr) != 0) return;
  static BmproData out;  // NimBLE host task: small stack
  out = BmproData();
  if (b.hasBattery) {
    out.batValid = true;
    out.batV = b.batV;
    gVBcast = b.batV;
    gABcast = b.batA;
    out.batA = b.batA;
    out.remainAh = b.remainAh;  // logged only: doesn't match the 405 Ah bank, meaning unknown
    out.batSoc = -1;            // worked out in loop() from volts + amps
  }
  // Not used: frame 0x04 bytes 16-17 matched the solar figure once, but read
  // 61 A at night with no charging (2026-11-18), so its meaning is unknown.
  // Solar comes from the connection (param 40.64) instead.
  if (gConnBatMs && millis() - gConnBatMs < 15000) return;
  if (out.batValid) applyDecoded(out);
}

bool looksBmpro(const char *name) {
  return ble::containsNoCase(name, "BMPRO") || ble::containsNoCase(name, "ODYSSEY") ||
         ble::containsNoCase(name, "COMMLINK") || ble::containsNoCase(name, "TREK") ||
         ble::containsNoCase(name, "SMART");
}

void onAdvert(const ble::Advert &a) {
  NimBLEAdvertisedDevice *d = a.dev;
  xSemaphoreTake(gMux, portMAX_DELAY);
  int slot = -1;
  for (int i = 0; i < gSeenCount; i++)
    if (!strcmp(a.addr, gSeen[i].addr)) slot = i;
  if (slot < 0 && gSeenCount < MAX_SEEN) slot = gSeenCount++;
  if (slot >= 0) {
    strncpy(gSeen[slot].addr, a.addr, 17);
    if (a.name[0]) strncpy(gSeen[slot].name, a.name, 23);
    gSeen[slot].rssi = a.rssi;
    gSeen[slot].lastMs = millis();
  }
  xSemaphoreGive(gMux);
  bool isTarget = strcasecmp(a.addr, gTargetAddr) == 0;
  if (isTarget) gTargetType = d->getAddress().getType();
  onBmproBroadcast(a);
  // Wireless BMPRO sensors may broadcast their readings in adverts, so log
  // manufacturer + service data of anything BMPRO-looking in full.
  if (gVerbose && (looksBmpro(a.name) || isTarget)) {
    String line = String("ADV ") + a.addr + " '" + a.name + "' rssi " + a.rssi + " mfg: " + hex(a.mfg, a.mfgLen);
    for (int i = 0; i < d->getServiceDataCount(); i++) {
      std::string sd = d->getServiceData(i);
      line += " svc[" + String(d->getServiceDataUUID(i).toString().c_str()) + "]: " +
              hex((const uint8_t *)sd.data(), sd.size());
    }
    logLine(line);
  }
}

void applyDecoded(const BmproData &in) {
  uint32_t now = millis();
  xSemaphoreTake(gMux, portMAX_DELAY);
  if (in.batValid) {
    gData.batValid = true;
    gData.batV = in.batV;
    gData.batA = in.batA;
    // The broadcast has no %, the connection does: keep the BMPRO's own %
    // while it is fresh rather than letting broadcasts blank it.
    if (in.batSoc >= 0) {
      gData.batSoc = in.batSoc;
      gData.batTtgMin = in.batTtgMin;
      gSocMs = now;
    } else if (!gSocMs || now - gSocMs > 60000) {
      gData.batSoc = -1;
      gData.batTtgMin = in.batTtgMin;
    }
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

bool queueWrite(const char *uuid, const uint8_t *data, size_t len) {
  return gBmpro.write(uuid, data, len, true);
}

// The app's start-up after logging in (from its HCI log): a list of writes to
// the init characteristic, then requests that start the value stream.
void sendStartup() {
  using namespace cv::bmpro;
  static const uint8_t init[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x0c, 0x0d, 0x0e, 0x0f, 0x12, 0x13};
  for (uint8_t b : init) queueWrite(INIT_UUID, &b, 1);
  static const uint8_t r1[] = {0x3c, 0x00, 0x40, 0x01, 0x01, 0x01};
  static const uint8_t r2[] = {0x3c, 0x00, 0x40, 0x6e, 0x04, 0xf7, 0x91, 0xfd, 0x35};
  static const uint8_t r3[] = {0x3c, 0x00, 0x40, 0x6f, 0x04, 0x00, 0xff, 0x00, 0x00};
  static const uint8_t r4[] = {0x3d, 0xff, 0xfc, 0x00, 0x01, 0x01};
  static const uint8_t r5[] = {0x3c, 0x00, 0x90, 0x0f, 0x01, 0x01};
  queueWrite(CAN_UUID, r1, sizeof(r1));
  queueWrite(CAN_UUID, r2, sizeof(r2));
  queueWrite(CAN_UUID, r3, sizeof(r3));
  queueWrite(CAN_UUID, r4, sizeof(r4));
  queueWrite(CAN_UUID, r5, sizeof(r5));
}

// Login: the OdysseyLink notifies a challenge on "SETEC AUTH"; answer it the
// way the app did, then it says "Authenticated".
void onAuth(const uint8_t *data, size_t len) {
  using namespace cv::bmpro;
  if (len == sizeof(AUTH_CHALLENGE) && !memcmp(data, AUTH_CHALLENGE, len)) {
    logLine("login challenge received - answering");
    queueWrite(AUTH_UUID, AUTH_ANSWER, sizeof(AUTH_ANSWER));
  } else if (len == sizeof(AUTH_CHALLENGE)) {
    // 2026-11-27: the OdysseyLink started sending a different challenge
    // (B8 D8 B3 61) and dropped the link every ~30 s when nothing answered.
    // The app's answer looks like a lossy text conversion (EF BF BD is the
    // UTF-8 replacement character), so try the same answer for any challenge,
    // at most once every 10 s.
    static uint32_t lastTry = 0;
    if (!lastTry || millis() - lastTry > 10000) {
      lastTry = millis() | 1;
      logLine(String("new login challenge ") + hex(data, len) + "- answering with the known reply");
      queueWrite(AUTH_UUID, AUTH_ANSWER, sizeof(AUTH_ANSWER));
    }
  } else if (len >= 13 && !memcmp(data, "Authenticated", 13)) {
    logLine("logged in - starting the data stream");
    sendStartup();
  } else {
    logLine(String("unknown login challenge ") + hex(data, len) + "- send this log line to Claude");
  }
}

void onNotify(NimBLERemoteCharacteristic *c, uint8_t *data, size_t len, bool isNotify) {
  std::string uuid = c->getUUID().toString();
  if (uuid == cv::bmpro::AUTH_UUID) {
    onAuth(data, len);
    return;
  }
  if (gVerbose) logLine(String(isNotify ? "NTF " : "IND ") + uuid.c_str() + " [" + len + "] " + hex(data, len));
  if (uuid == cv::bmpro::CAN_UUID) gFrames++;
  static BmproData out;  // static: NimBLE host task has a small stack
  out = BmproData();
  if (Bmpro::decode(uuid.c_str(), data, len, out)) applyDecoded(out);
}

void dumpAndSubscribe() {
  for (auto *svc : blec::services(gClient, true)) {
    logLine(String("SVC ") + svc->getUUID().toString().c_str());
    for (auto *c : blec::chars(svc, true)) {
      beat(BEAT_BMPRO);  // each read can take seconds: don't look stuck mid-discovery
      String props;
      if (c->canRead()) props += "R";
      if (c->canWrite()) props += "W";
      if (c->canWriteNoResponse()) props += "w";
      if (c->canNotify()) props += "N";
      if (c->canIndicate()) props += "I";
      String line = String("  CHR ") + c->getUUID().toString().c_str() + " [" + props + "]";
      if (c->canRead()) {
        std::string v = c->readValue();
        if (c->getUUID().toString() == cv::bmpro::AUTH_UUID) onAuth((const uint8_t *)v.data(), v.size());
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
  for (auto *svc : blec::services(gClient, false))
    for (auto *c : blec::chars(svc, false))
      if (c->getUUID() == want) return c;
  return nullptr;
}

void bleTask(void *) {
  uint32_t nextTry = 0;
  uint32_t failures = 0;
  for (;;) {
    beat(BEAT_BMPRO);
    if (gRetargeted && gClient && gClient->isConnected()) gClient->disconnect();
    gRetargeted = false;

    // Keep a permanent connection to the OdysseyLink (it accepts several
    // centrals at once, so the phone app keeps working alongside us).
    bool up = gClient && gClient->isConnected();
    // Connected but the data stream has gone quiet (it normally sends ~2
    // frames a second): drop the link so the next pass reconnects and logs in.
    static uint32_t lastFrames = 0, lastFrameMs = 0;
    if (gFrames != lastFrames || !up) {
      lastFrames = gFrames;
      lastFrameMs = millis();
    } else if (millis() - lastFrameMs > 30000) {
      logLine("no data from the OdysseyLink for 30 s - reconnecting");
      gClient->disconnect();
      lastFrameMs = millis();
      up = false;
    }
    if (!up && gTargetAddr[0] && (int32_t)(millis() - nextTry) >= 0) {
      // 10 s between tries, backing off to 60 s if it keeps failing.
      nextTry = millis() + (failures < 3 ? 10000 : 60000);
      if (!gClient) {
        gClient = NimBLEDevice::createClient();
        blec::setConnectTimeoutSec(gClient, 8);  // NimBLE 2 takes ms: a raw 8 meant 8 ms
      }
      uint8_t type = gTargetType;
      // One connect at a time (the heater task pauses the same scan).
      bool locked = ble::lockConnect();
      beat(BEAT_BMPRO);  // may have waited up to 60 s for the heater's connect
      if (locked) {
        logLine(String("connecting to ") + gTargetAddr + (type == BLE_ADDR_PUBLIC ? " (public)" : " (random)"));
        ble::pauseScan();
        if (gClient->connect(NimBLEAddress(std::string(gTargetAddr), type))) {
          failures = 0;
          logLine("connected");
          dumpAndSubscribe();
        } else {
          failures++;
          logLine(String("connect failed (error ") + gClient->getLastError() + ") - retrying in " +
                  (failures < 3 ? 10 : 60) + " s");
        }
        ble::resumeScan();
        ble::unlockConnect();
      }
    }

    WriteReq w;
    while (xQueueReceive(gWriteQ, &w, 0) == pdTRUE) {
      NimBLERemoteCharacteristic *c = findChar(w.uuid);
      bool ok = c && c->writeValue(w.data, w.len, w.response);
      logLine(String("WRITE ") + w.uuid + " " + hex(w.data, w.len) + (ok ? "ok" : "FAILED"));
    }
    // Sleep up to 100 ms, but wake the moment a write (a pump press) is queued.
    xQueuePeek(gWriteQ, &w, pdMS_TO_TICKS(100));
  }
}
}  // namespace

void Bmpro::begin(const char *addr, bool verbose) {
  gMux = xSemaphoreCreateMutex();
  gWriteQ = xQueueCreate(24, sizeof(WriteReq));  // login start-up queues 17 writes
  gVerbose = verbose;
  strncpy(gTargetAddr, addr ? addr : "", 17);
  ble::begin();
  ble::addListener(onAdvert);
  // Always scan: the battery reading comes from the BMPRO's broadcasts.
  ble::keepScanning();
  xTaskCreatePinnedToCore(bleTask, "bmpro", 6144, nullptr, 1, nullptr, 0);
  logLine(String("BLE started, target '") + gTargetAddr + "'");
}

bool Bmpro::takeBattery(float &v, float &a, uint32_t &ms, float &bmproSoc) {
  xSemaphoreTake(gMux, portMAX_DELAY);
  bool fresh = gData.batValid && gBatMs != gBatTakenMs && millis() - gBatMs < 30000;
  if (fresh) {
    v = gData.batV;
    a = gData.batA;
    ms = gBatMs;
    bmproSoc = gData.batSoc;
    gBatTakenMs = gBatMs;
  }
  xSemaphoreGive(gMux);
  return fresh;
}

void Bmpro::connect(const char *addr) {
  strncpy(gTargetAddr, addr, 17);
  gTargetAddr[17] = 0;
  gRetargeted = true;
}

bool Bmpro::connected() const { return gClient && gClient->isConnected(); }
bool Bmpro::decoded() const { return gDecodedAny; }

float Bmpro::solarA() const {
  xSemaphoreTake(gMux, portMAX_DELAY);
  float a = gData.chargerValid && millis() - gChgMs < 30000 ? gData.chargerA : -1;
  xSemaphoreGive(gMux);
  return a;
}

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
    // % and time left come from the hub's own counter (BatteryShunt::feed,
    // fed by takeBattery()), not the BMPRO: its % tops out at a 600 Ah bank.
    if (fresh) {
      s.bat.source = cv::BAT_SRC_BMPRO;
      s.bat.v = gData.batV;
      s.bat.a = gData.batA;
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
  // Once a minute: what the OdysseyLink link is delivering, so a plain serial
  // log shows whether tanks, pumps and battery % are coming through.
  static uint32_t lastSummary = 0;
  if (now - lastSummary >= 20000) {
    lastSummary = now;
    auto age = [&](bool valid, uint32_t ms) { return valid ? String((now - ms) / 1000) + "s ago" : String("never"); };
    String line = String("link ") + (connected() ? "up" : "down") + ", " + gFrames + " frames; tanks";
    for (int i = 0; i < cv::NUM_TANKS; i++)
      line += String(" ") + (gData.tankValid[i] ? String((int)gData.tankPct[i]) + "%" : String("-"));
    line += String(" (") + age(gData.tankValid[0], gTankMs[0]) + "); pumps";
    for (int i = 0; i < cv::NUM_PUMPS; i++)
      line += String(" ") + (gData.pumpValid[i] ? (gData.pumpOn[i] ? "on" : "off") : "-");
    line += String("; BMPRO % ") + (gSocMs ? String((int)gData.batSoc) + " (" + (now - gSocMs) / 1000 + "s ago)" : String("never"));
    line += String("; minutes left ") + gData.batTtgMin;
    line += String("; volts: broadcast ") + String(gVBcast, 2) + ", 40.62 " + String(gVConn, 2) + ", 40.65 " + String(gV2, 2);
    line += String("; amps: broadcast ") + String(gABcast, 1) + ", 40.61 " + String(gAConn, 1) +
            ", shown " + String(gData.batA, 1) + (gConnBatMs && now - gConnBatMs < 15000 ? " (from link)" : " (from broadcast)");
    xSemaphoreGive(gMux);
    logLine(line, false);  // serial only: too long and too frequent for the RAM log
    return;
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
// Decoded 2026-11-17 from the BMPRO app's HCI log: see docs/BMPRO_PROTOCOL.md.
// Values arrive one per message, so they are gathered here and handed on as
// a set (battery once both volts and amps are known).
bool Bmpro::decode(const char *charUuid, const uint8_t *data, size_t len, BmproData &out) {
  if (strcasecmp(charUuid, cv::bmpro::CAN_UUID) != 0) return false;
  static cv::bmpro::Values v;  // NimBLE host task only
  cv::bmpro::Msg m;
  if (!cv::bmpro::parse(data, len, m) || !cv::bmpro::apply(m, v)) return false;
  if (v.hasBatV) gVConn = v.batV;
  if (v.hasBatA) gAConn = v.batA;
  if (v.hasV2) gV2 = v.v2;
  if (v.hasBatV && v.hasBatA) {
    out.batValid = true;
    out.batV = v.batV;
    out.batA = v.batA;
    out.batSoc = v.hasSoc ? v.soc : -1;
    out.batTtgMin = v.hasMinLeft ? v.minLeft : -1;
    gConnBatMs = millis();
  }
  if (v.hasSolarA) {
    out.chargerValid = true;
    out.chargerA = v.solarA;
  }
  if (v.hasTanks)
    for (int i = 0; i < cv::NUM_TANKS && i < 4; i++) {
      out.tankValid[i] = true;
      out.tankPct[i] = v.tankPct[i];
    }
  for (int i = 0; i < cv::NUM_PUMPS && i < 2; i++)
    if (v.hasPump[i]) {
      out.pumpValid[i] = true;
      out.pumpOn[i] = v.pumpOn[i];
    }
  return true;
}

bool Bmpro::encodePump(int pump, bool on, String &charUuid, uint8_t *buf, size_t &len) {
  if (pump < 0 || pump > 1 || len < 6) return false;
  charUuid = cv::bmpro::CAN_UUID;
  len = cv::bmpro::encodePump(pump, on, buf);
  return true;
}

}  // namespace hub
