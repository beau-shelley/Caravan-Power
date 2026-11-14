// Touchscreen UI (LVGL 8.3) - 800x480 landscape.
//   +--------+-------------------------------------------+
//   |  nav   |  status bar: page, clock, link/wifi/cloud |
//   |  rail  +-------------------------------------------+
//   |        |  page content (712 x 436)                 |
//   +--------+-------------------------------------------+
#pragma once
#include <lvgl.h>

namespace ui {

enum Page { PAGE_HOME, PAGE_POWER, PAGE_WATER, PAGE_CLIMATE, PAGE_LEVEL, PAGE_ALERTS, PAGE_SETTINGS, PAGE_COUNT };

void begin();   // build everything
void loop();    // refresh widgets from app:: model (call often; cheap if nothing changed)
void showPage(int p);
int sleepMinutes();

// ---------------------------------------------------------------- palette
namespace col {
constexpr uint32_t BG = 0x0B1220, RAIL = 0x0E1626, CARD = 0x131C2E, BORDER = 0x1E293B;
constexpr uint32_t TEXT = 0xE2E8F0, MUTED = 0x94A3B8, DIM = 0x475569;
constexpr uint32_t SOLAR = 0xFBBF24, CHARGE = 0x34D399, LOAD = 0xF87171, WATER = 0x38BDF8;
constexpr uint32_t CLIMATE = 0xA78BFA, ACCENT = 0x22D3EE, WARN = 0xF59E0B, ALARM = 0xEF4444;
constexpr uint32_t OK = 0x22C55E, TILE = 0x1A2438;
}  // namespace col

// ---------------------------------------------------------------- helpers (ui_common.cpp)
inline lv_color_t c(uint32_t hex) { return lv_color_hex(hex); }
lv_obj_t *panel(lv_obj_t *parent, int x, int y, int w, int h);
lv_obj_t *card(lv_obj_t *parent, int x, int y, int w, int h, const char *title, uint32_t accent = col::ACCENT);
lv_obj_t *lbl(lv_obj_t *parent, const lv_font_t *f, uint32_t color, const char *text = "");
void setText(lv_obj_t *l, const char *fmt, ...);
lv_obj_t *button(lv_obj_t *parent, int w, int h, const char *text, uint32_t bg, lv_event_cb_t cb, void *ud = nullptr);
lv_obj_t *tile(lv_obj_t *parent, int w, int h, const char *sym, const char *text, lv_event_cb_t cb, void *ud = nullptr);
void setTile(lv_obj_t *tile, bool on, uint32_t onColor, const char *sub = nullptr);
lv_obj_t *chip(lv_obj_t *parent);
void setChip(lv_obj_t *chip, const char *txt, uint32_t color);
lv_obj_t *vbar(lv_obj_t *parent, int w, int h, uint32_t color);
void setBarColor(lv_obj_t *bar, uint32_t color);
void toast(const char *msg, uint32_t color = col::ACCENT);
void confirm(const char *title, const char *msg, void (*onYes)(void *), void *ud = nullptr);
uint32_t tempColor(int role, float c);
uint32_t socColor(float soc);
uint32_t tankColor(int i, float pct);
void fmtDuration(char *buf, size_t n, int32_t minutes);

// ---------------------------------------------------------------- pages
void buildHome(lv_obj_t *p);
void updateHome();
void buildPower(lv_obj_t *p);
void updatePower();
void buildWater(lv_obj_t *p);
void updateWater();
void buildClimate(lv_obj_t *p);
void updateClimate();
void buildLevel(lv_obj_t *p);
void updateLevel();
void buildAlerts(lv_obj_t *p);
void updateAlerts();
void buildSettings(lv_obj_t *p);
void updateSettings();

void flushRuleEdits();  // ui_alerts.cpp - debounced rule threshold edits

void buildWeatherWidget(lv_obj_t *statusBar);  // ui_weather.cpp
void updateWeather();
void openWeatherForTest();

// shared actions
void togglePump(int i);
void toggleInverter();
void toggleAircon();

}  // namespace ui
