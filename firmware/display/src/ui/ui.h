// Touchscreen UI (LVGL 8.3) - landscape, sized from board::SCREEN_W/H
// (1024x600 on the Waveshare 7B).
//   +--------+-------------------------------------------+
//   |  nav   |  status bar: page, clock, link/wifi/cloud |
//   |  rail  +-------------------------------------------+
//   |        |  page content (PAGE_W x PAGE_H)           |
//   +--------+-------------------------------------------+
#pragma once
#include <lvgl.h>

#include "../board.h"

// Extra icon fonts (see ICON_WATER / ICON_TAP below). Declared outside the namespace:
// the .c files define them with C linkage.
LV_FONT_DECLARE(ui_icons_20);
LV_FONT_DECLARE(ui_icons_24);
LV_FONT_DECLARE(ui_icons_28);

namespace ui {

// ---------------------------------------------------------------- features
// Inverter and aircon control are switched off for now (they'll come back
// later). Build with -D FEATURE_INVERTER_CONTROL=1 / -D FEATURE_AIRCON_CONTROL=1
// to show them again. The code stays compiled either way so it doesn't rot.
#ifndef FEATURE_INVERTER_CONTROL
#define FEATURE_INVERTER_CONTROL 0
#endif
#ifndef FEATURE_AIRCON_CONTROL
#define FEATURE_AIRCON_CONTROL 0
#endif
constexpr bool HAS_INVERTER = FEATURE_INVERTER_CONTROL, HAS_AIRCON = FEATURE_AIRCON_CONTROL;

// ---------------------------------------------------------------- layout
// Every page is laid out from these, never from literal screen sizes.
constexpr int SCR_W = board::SCREEN_W, SCR_H = board::SCREEN_H;
constexpr bool LARGE = SCR_W >= 1000;          // 1024x600 class vs 800x480 class
constexpr int RAIL_W = LARGE ? 100 : 88;       // nav rail
constexpr int BAR_H = LARGE ? 52 : 44;         // status bar
constexpr int PAGE_W = SCR_W - RAIL_W, PAGE_H = SCR_H - BAR_H;
constexpr int GAP = 12;                        // outer margin and gutter between cards
constexpr int CARD_PAD = 14;                   // card() inner padding
constexpr int INNER_W = PAGE_W - 2 * GAP, INNER_H = PAGE_H - 2 * GAP;
// n equal columns across the page: width of one, and x of column i.
constexpr int colW(int n) { return (INNER_W - (n - 1) * GAP) / n; }
constexpr int colX(int i, int n) { return GAP + i * (colW(n) + GAP); }
// Split the page into two columns, the left one `pct` percent wide.
constexpr int splitW(int pct) { return (INNER_W - GAP) * pct / 100; }
// Content size inside a card of the given outer size.
constexpr int innerOf(int outer) { return outer - 2 * CARD_PAD; }
// Status-bar weather widget: right offset and width, so the alert banner
// knows where it has to stop. WX_RIGHT_EDGE = distance from the bar's right
// edge to the widget's left edge.
constexpr int WX_RIGHT_OFS = LARGE ? 262 : 248, WX_W = LARGE ? 210 : 196;
constexpr int WX_RIGHT_EDGE = WX_RIGHT_OFS + WX_W;

// The nav rail only shows everyday pages; Level, Alerts and Settings live
// behind "More" (Alerts is also one tap away via the bell / alert banner).
enum Page { PAGE_HOME, PAGE_POWER, PAGE_WATER, PAGE_CLIMATE, PAGE_MORE, PAGE_LEVEL, PAGE_ALERTS, PAGE_SETTINGS, PAGE_COUNT };

void begin();   // build everything
void loop();    // refresh widgets from app:: model (call often; cheap if nothing changed)
void showPage(int p);
int sleepSeconds();

// ---------------------------------------------------------------- palette
// Extra icons (Font Awesome Free 6, SIL OFL 1.1) in ui_icons_20/24/28.c, made
// with lv_font_conv from fa-solid-900.ttf: -r 0xf043,0xe006 --bpp 4.
#define ICON_WATER ""  // U+F043 droplet (UTF-8 EF 81 83)
#define ICON_TAP ""    // U+E006 faucet-drip, for pumps (UTF-8 EE 80 86)
// The extra icons need their own font; LVGL's symbols use Montserrat.
bool isExtraIcon(const char *sym);

namespace col {
constexpr uint32_t BG = 0x0B1220, RAIL = 0x0E1626, CARD = 0x131C2E, BORDER = 0x1E293B;
constexpr uint32_t TEXT = 0xE2E8F0, MUTED = 0x94A3B8, DIM = 0x475569;
constexpr uint32_t SOLAR = 0xFBBF24, CHARGE = 0x34D399, LOAD = 0xF87171, WATER = 0x38BDF8;
constexpr uint32_t CLIMATE = 0xA78BFA, ACCENT = 0x22D3EE, WARN = 0xF59E0B, ALARM = 0xEF4444;
constexpr uint32_t OK = 0x22C55E, TILE = 0x1A2438, GAS = 0xFB923C, MAINS = 0x60A5FA;
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
uint32_t gasColor(float pct);
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


void buildWeatherWidget(lv_obj_t *statusBar);  // ui_weather.cpp
void buildForecast(lv_obj_t *box, int w, int h, const char *note = nullptr, bool bubble = false);
void updateWeather();
void openWeatherForTest();

// shared actions
void togglePump(int i);
void toggleInverter();
void toggleAircon();

}  // namespace ui
