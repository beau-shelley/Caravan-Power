#include "canbus.h"

#if FEATURE_CAN_LOG
#include <driver/twai.h>

namespace canbus {
namespace {

constexpr gpio_num_t PIN_TX = GPIO_NUM_20, PIN_RX = GPIO_NUM_19;
constexpr int MAX_IDS = 96;
constexpr uint32_t SCAN_WINDOW_MS = 1500;
constexpr uint32_t SUMMARY_MS = 10000;

enum Mode : char { MODE_DIFF = 'd', MODE_RAW = 'r', MODE_QUIET = 'q' };

struct IdEntry {
  uint32_t id;
  bool ext;
  uint8_t dlc;
  uint8_t data[8];
  uint32_t count, changes, firstMs, lastMs;
};

IdEntry gIds[MAX_IDS];  // only touched by the CAN task
uint16_t gIdCount = 0;
Mode gMode = MODE_DIFF;
volatile Stats gStats;  // written by the CAN task, read anywhere (32-bit fields)
bool gRunning = false;

bool timingFor(uint32_t rate, twai_timing_config_t &t) {
  switch (rate) {
    case 1000000: t = TWAI_TIMING_CONFIG_1MBITS(); return true;
    case 500000: t = TWAI_TIMING_CONFIG_500KBITS(); return true;
    case 250000: t = TWAI_TIMING_CONFIG_250KBITS(); return true;
    case 125000: t = TWAI_TIMING_CONFIG_125KBITS(); return true;
    case 100000: t = TWAI_TIMING_CONFIG_100KBITS(); return true;
    case 50000: t = TWAI_TIMING_CONFIG_50KBITS(); return true;
    default: return false;
  }
}

// Never block: if the UART can't take the whole line right now, drop it. A
// blocked Serial write would hold the Serial lock that the UI core also uses.
void emit(const char *line, size_t len) {
  if ((size_t)Serial.availableForWrite() < len) {
    gStats.linesDropped = gStats.linesDropped + 1;
    return;
  }
  Serial.write(reinterpret_cast<const uint8_t *>(line), len);
}

void logFrame(const twai_message_t &m) {
  char line[64];
  int n = snprintf(line, sizeof(line), "CAN,%lu,%lX,%c,%u,", (unsigned long)millis(),
                   (unsigned long)m.identifier, m.extd ? 'x' : 's', m.data_length_code);
  if (m.rtr) n += snprintf(line + n, sizeof(line) - n, "RTR");
  else
    for (int i = 0; i < m.data_length_code && i < 8; ++i)
      n += snprintf(line + n, sizeof(line) - n, "%02X", m.data[i]);
  line[n++] = '\n';
  emit(line, n);
}

void printSummary() {
  char line[96];
  uint32_t now = millis();
  int n = snprintf(line, sizeof(line), "CANSUM,%lu,bitrate=%lu,frames=%lu,ids=%u,missed=%lu,dropped=%lu,buserr=%lu\n",
                   (unsigned long)now, (unsigned long)gStats.bitrate, (unsigned long)gStats.frames,
                   gIdCount, (unsigned long)gStats.rxMissed, (unsigned long)gStats.linesDropped,
                   (unsigned long)gStats.busErrors);
  emit(line, n);
  for (int i = 0; i < gIdCount; ++i) {
    const IdEntry &e = gIds[i];
    uint32_t span = e.lastMs - e.firstMs;
    unsigned periodMs = (e.count > 1 && span) ? span / (e.count - 1) : 0;
    n = snprintf(line, sizeof(line), "CANID,%lX,%c,count=%lu,changes=%lu,period_ms=%u,last=",
                 (unsigned long)e.id, e.ext ? 'x' : 's', (unsigned long)e.count,
                 (unsigned long)e.changes, periodMs);
    for (int b = 0; b < e.dlc && b < 8; ++b) n += snprintf(line + n, sizeof(line) - n, "%02X", e.data[b]);
    line[n++] = '\n';
    // The table can be long; give the UART time to drain rather than drop rows.
    for (int tries = 0; tries < 20 && (size_t)Serial.availableForWrite() < (size_t)n; ++tries) vTaskDelay(pdMS_TO_TICKS(5));
    emit(line, n);
  }
}

void handleFrame(const twai_message_t &m) {
  gStats.frames = gStats.frames + 1;
  IdEntry *e = nullptr;
  for (int i = 0; i < gIdCount; ++i)
    if (gIds[i].id == m.identifier && gIds[i].ext == (bool)m.extd) {
      e = &gIds[i];
      break;
    }
  bool changed = false;
  uint8_t dlc = m.data_length_code > 8 ? 8 : m.data_length_code;
  if (!e) {
    if (gIdCount < MAX_IDS) {
      e = &gIds[gIdCount++];
      *e = {};
      e->id = m.identifier;
      e->ext = m.extd;
      e->firstMs = millis();
      gStats.ids = gIdCount;
    }
    changed = true;
  } else if (e->dlc != dlc || memcmp(e->data, m.data, dlc) != 0) {
    changed = true;
  }
  if (e) {
    if (changed && e->count) e->changes++;
    e->count++;
    e->lastMs = millis();
    e->dlc = dlc;
    memcpy(e->data, m.data, dlc);
  }
  if (gMode == MODE_RAW || (gMode == MODE_DIFF && changed)) logFrame(m);
}

bool start(uint32_t rate) {
  twai_timing_config_t t;
  if (!timingFor(rate, t)) return false;
  twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(PIN_TX, PIN_RX, TWAI_MODE_LISTEN_ONLY);
  g.rx_queue_len = 128;
  g.tx_queue_len = 0;
  g.alerts_enabled = TWAI_ALERT_NONE;
  g.intr_flags = ESP_INTR_FLAG_LEVEL1;  // ISR is allocated on this task's core (0)
  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (twai_driver_install(&g, &t, &f) != ESP_OK) return false;
  if (twai_start() != ESP_OK) {
    twai_driver_uninstall();
    return false;
  }
  gRunning = true;
  return true;
}

void stop() {
  if (!gRunning) return;
  twai_stop();
  twai_driver_uninstall();
  gRunning = false;
}

void updateDriverStats() {
  twai_status_info_t s;
  if (twai_get_status_info(&s) == ESP_OK) {
    gStats.rxMissed = s.rx_missed_count + s.rx_overrun_count;
    gStats.busErrors = s.bus_error_count;
  }
}

// Listen at each rate in turn until valid frames arrive. A wrong rate never
// produces a frame that passes the CRC, so a couple of good frames is proof.
uint32_t scanBitrate() {
  static const uint32_t rates[] = {250000, 125000, 500000, 1000000, 100000, 50000};
  for (;;) {
    for (uint32_t rate : rates) {
      if (!start(rate)) continue;
      uint32_t t0 = millis(), good = 0;
      twai_message_t m;
      while (millis() - t0 < SCAN_WINDOW_MS && good < 3)
        if (twai_receive(&m, pdMS_TO_TICKS(50)) == ESP_OK) good++;
      if (good >= 3) {
        Serial.printf("[can] bus found at %lu bit/s\n", (unsigned long)rate);
        return rate;
      }
      stop();
    }
    static uint32_t lastNote = 0;
    if (millis() - lastNote > 60000) {
      lastNote = millis();
      Serial.println("[can] no CAN traffic at 50k-1M bit/s yet (still listening)");
    }
  }
}

void pollSerialCommands() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    switch (c) {
      case 'd': case 'r': case 'q':
        gMode = (Mode)c;
        Serial.printf("[can] log mode: %s\n", c == 'd' ? "changes only" : c == 'r' ? "raw" : "quiet");
        break;
      case 's': printSummary(); break;
      case 'b':
        stop();
        gStats.bitrate = 0;
        break;
      default: break;
    }
  }
}

void canTask(void *) {
  uint32_t lastSummary = millis(), lastFrameMs = millis(), errAtLastFrame = 0;
  for (;;) {
    if (!gRunning) {
      gStats.bitrate = CAN_BITRATE ? (start(CAN_BITRATE) ? CAN_BITRATE : 0) : scanBitrate();
      if (!gStats.bitrate) {
        Serial.println("[can] driver start failed");
        vTaskDelay(pdMS_TO_TICKS(5000));
        continue;
      }
      lastFrameMs = millis();
      errAtLastFrame = 0;
    }
    twai_message_t m;
    // Sleep until a frame arrives (max 100 ms), then drain whatever is queued.
    if (twai_receive(&m, pdMS_TO_TICKS(100)) == ESP_OK) {
      do {
        handleFrame(m);
      } while (twai_receive(&m, 0) == ESP_OK);
      lastFrameMs = millis();
    }
    updateDriverStats();
    if (millis() - lastFrameMs < 200) errAtLastFrame = gStats.busErrors;
    // Auto mode only: silence + a pile of bus errors means the rate is wrong
    // (e.g. a different bus was plugged in). Quiet but error-free just means
    // the BMPRO system is asleep, so keep the rate.
    if (!CAN_BITRATE && millis() - lastFrameMs > 5000 && gStats.busErrors - errAtLastFrame > 50) {
      Serial.println("[can] errors and no frames: rescanning bitrate");
      stop();
      gStats.bitrate = 0;
    }
    pollSerialCommands();
    if (gMode != MODE_QUIET && millis() - lastSummary > SUMMARY_MS) {
      lastSummary = millis();
      printSummary();
    }
  }
}

}  // namespace

void begin() {
  // Core 0, above the idle task but below the Wi-Fi/timer tasks. It blocks on
  // the driver queue nearly all the time, so its CPU cost is the per-frame work.
  xTaskCreatePinnedToCore(canTask, "can", 4096, nullptr, 3, nullptr, 0);
}

Stats stats() {
  Stats s;
  s.bitrate = gStats.bitrate;
  s.frames = gStats.frames;
  s.ids = gStats.ids;
  s.rxMissed = gStats.rxMissed;
  s.linesDropped = gStats.linesDropped;
  s.busErrors = gStats.busErrors;
  return s;
}

}  // namespace canbus

#else
namespace canbus {
void begin() {}
Stats stats() { return {}; }
}  // namespace canbus
#endif
