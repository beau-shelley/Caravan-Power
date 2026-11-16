// Renders every page of the real display UI code to PNG screenshots using
// demo data, so the design can be reviewed without the hardware.
#include <lvgl.h>

#include <cstdio>
#include <vector>

#include "Arduino.h"
#include "app.h"
#include "board.h"
#include "ui/ui.h"

HardwareSerial Serial, Serial1;
static uint32_t gNow = 1000;
extern "C" uint32_t millis(void) { return gNow; }
extern "C" void delay(uint32_t ms) { gNow += ms; }

namespace ui {
void handleLocResults(JsonObjectConst m);
}

namespace board {
void begin() {}
void backlight(bool) {}
bool backlightOn() { return true; }
uint32_t lastTouchMs() { return gNow; }
}  // namespace board

// Same resolution as the firmware (board.h); override with
//   make -C tests/sim RES="-DDISPLAY_HOR_RES=800 -DDISPLAY_VER_RES=480"
static constexpr int W = board::SCREEN_W, H = board::SCREEN_H;
static std::vector<uint16_t> fb(W * H);
static void flush(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *px) {
  for (int y = a->y1; y <= a->y2; y++)
    for (int x = a->x1; x <= a->x2; x++) fb[y * W + x] = (px++)->full;
  lv_disp_flush_ready(d);
}

static void run(int ms) {
  for (int t = 0; t < ms; t += 20) {
    gNow += 20;
    app::linkUp = true;
    ui::loop();
    lv_timer_handler();
  }
}

static void shot(const char *name) {
  app::stateDirty = true;
  run(1500);
  lv_obj_invalidate(lv_scr_act());
  lv_refr_now(nullptr);
  char path[256];
  snprintf(path, sizeof(path), "%s/%s.ppm", getenv("OUT") ? getenv("OUT") : ".", name);
  FILE *f = fopen(path, "wb");
  fprintf(f, "P6\n%d %d\n255\n", W, H);
  for (uint16_t p : fb) {
    uint8_t r = ((p >> 11) & 31) * 255 / 31, g = ((p >> 5) & 63) * 255 / 63, b = (p & 31) * 255 / 31;
    fputc(r, f);
    fputc(g, f);
    fputc(b, f);
  }
  fclose(f);
  printf("wrote %s\n", path);
}

static void demoData() {
  using namespace cv;
  State &s = app::state;
  s.ts = 1762572720;  // Sat 8 Nov 2025 14:32 AEDT
  s.tzOff = 11 * 3600;
  s.bat = {};
  s.bat.online = true;
  s.bat.source = BAT_SRC_SHUNT;
  s.bat.v = 13.31f;
  s.bat.soc = 78;
  s.bat.solarA = 31.4f;
  s.bat.chargerA = 0;
  s.bat.chargeA = 31.4f;
  s.bat.loadA = 12.6f;
  s.bat.a = 18.8f;
  s.bat.ttgMin = 52;
  s.solar[0] = {true, -1, 312, 13.31f, 22.6f, 3, 0, 1.42f, 498, 812.4f};
  s.solar[1] = {true, -1, 118, 13.30f, 8.8f, 4, 0, 0.61f, 205, 233.9f};
  s.temp[TEMP_INSIDE] = {true, 24.6f, 16.2f, 27.9f};
  s.temp[TEMP_OUTSIDE] = {true, 31.2f, 14.8f, 33.4f};
  s.temp[TEMP_FRIDGE] = {true, 3.4f, 2.1f, 5.2f};
  s.tank[0] = {true, 72};
  s.tank[1] = {true, 18};
  s.tank[2] = {true, 41};
  s.pump[0].on = true;
  s.pump[0].sensed = true;
  s.pump[0].runSec = 94;
  s.inv.on = false;
  s.ac.on = true;
  s.ac.preset = 0;
  s.wifi = s.cloud = true;
  strcpy(s.ip, "192.168.1.42");
  const char *wxJson = R"({"current":{"temperature_2m":27.4,"weather_code":2,"wind_speed_10m":14.2,
    "wind_gusts_10m":31.0,"is_day":1},"daily":{"time":["2025-11-08","2025-11-09","2025-11-10","2025-11-11"],
    "weather_code":[2,61,95,0],"temperature_2m_max":[29.1,24.0,22.3,26.8],"temperature_2m_min":[14.2,15.1,12.9,10.4],
    "precipitation_probability_max":[10,80,95,0],"precipitation_sum":[0.0,6.2,18.4,0.0],
    "wind_gusts_10m_max":[38.2,45.0,72.4,25.9]}})";
  parseOpenMeteo(wxJson, strlen(wxJson), s.wx);
  s.wx.updated = s.ts - 12 * 60;
  strcpy(s.wx.place, "Mudgee NSW");
  app::meta.locSrc = 3;
  strcpy(app::meta.locName, "Mudgee NSW");

  Meta &m = app::meta;
  m.acPresetCount = 5;
  const char *p[] = {"Cool 22 auto", "Cool 24 low", "Heat 21", "Dry", "Fan only"};
  for (int i = 0; i < 5; i++) strcpy(m.acPresetName[i], p[i]);
  loadDefaultRules(app::rules);
  app::metaVer++;
  app::rulesVer++;
}

int main() {
  lv_init();
  static lv_disp_draw_buf_t db;
  static std::vector<lv_color_t> buf(W * H);
  lv_disp_draw_buf_init(&db, buf.data(), nullptr, W * H);
  static lv_disp_drv_t drv;
  lv_disp_drv_init(&drv);
  drv.hor_res = W;
  drv.ver_res = H;
  drv.flush_cb = flush;
  drv.draw_buf = &db;
  lv_disp_drv_register(&drv);

  ui::begin();
  demoData();
  // Fill the 30-minute power chart with a plausible history.
  for (int i = 0; i < 180; i++) {
    app::state.solar[0].pvW = 250 + 80 * sinf(i / 18.0f) + (i % 7) * 4;
    app::state.solar[1].pvW = 100 + 30 * sinf(i / 11.0f);
    app::state.bat.loadA = i > 120 && i < 140 ? 95 : 10 + (i % 5);
    app::stateDirty = true;
    app::linkUp = true;
    gNow += 10000;
    ui::loop();
  }
  demoData();

  ui::showPage(ui::PAGE_HOME);
  shot("01-home");
  ui::showPage(ui::PAGE_POWER);
  shot("02-power");
  ui::showPage(ui::PAGE_WATER);
  shot("03-water");
  ui::showPage(ui::PAGE_CLIMATE);
  shot("04-climate");
  app::state.lvl.online = app::state.lvl.zeroed = app::state.lvl.fwdLearned = true;
  app::state.lvl.roll = -0.9f;   // left (door side) low
  app::state.lvl.pitch = 0.6f;   // front high
  ui::showPage(ui::PAGE_LEVEL);
  shot("08-level");
  ui::showPage(ui::PAGE_MORE);
  shot("11-more");
  app::state.lvl.roll = 0.1f;  // now level -> "set weather location on your phone" hint appears
  app::state.lvl.pitch = -0.2f;
  ui::showPage(ui::PAGE_LEVEL);
  for (int i = 0; i < 4; i++) shot("12-level-done");  // level held > 3 s (sim refreshes once per shot)

  // An active alarm + a warning
  cv::State &s = app::state;
  s.temp[cv::TEMP_FRIDGE].c = 7.4f;
  s.alertCount = 3;
  s.alerts[0] = {};
  s.alerts[0].ruleId = 4;
  s.alerts[0].sev = cv::SEV_CRIT;
  s.alerts[0].since = s.ts - 600;
  strcpy(s.alerts[0].msg, "Battery charge low: 4% (< 5)");
  s.alerts[1] = {};
  s.alerts[1].ruleId = 7;
  s.alerts[1].sev = cv::SEV_ALARM;
  s.alerts[1].since = s.ts - 1500;
  strcpy(s.alerts[1].msg, "Fridge warm: 7.4C (> 6.0)");
  s.alerts[2] = {};
  s.alerts[2].ruleId = 12;
  s.alerts[2].sev = cv::SEV_INFO;
  s.alerts[2].since = s.ts - 3600;
  strcpy(s.alerts[2].msg, "Tank 2 low: 18% (< 20)");
  ui::showPage(ui::PAGE_ALERTS);
  shot("05-alerts");
  ui::showPage(ui::PAGE_HOME);
  shot("06-home-alarm");
  s.alertCount = 0;
  s.temp[cv::TEMP_FRIDGE].c = 3.4f;
  ui::showPage(ui::PAGE_SETTINGS);
  shot("07-settings");
  ui::showPage(ui::PAGE_HOME);
  ui::openWeatherForTest();
  shot("09-forecast");
  return 0;
}
