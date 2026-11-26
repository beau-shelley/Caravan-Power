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
// Theme textures (tex_theme.c, made by tools/theme/make_theme.py).
LV_IMG_DECLARE(tex_bg);     // full-screen background: plain, with a subtle camo header strip

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
// with lv_font_conv from fa-solid-900.ttf: -r 0xf043,0xe006,0xf023,0xf09c,0xf3ed --bpp 4.
#define ICON_WATER ""  // U+F043 droplet (UTF-8 EF 81 83)
#define ICON_TAP ""    // U+E006 faucet-drip, for pumps (UTF-8 EE 80 86)
// Secure mode (anti-theft): -r 0xf023,0xf09c,0xf3ed added to the same fonts.
#define ICON_LOCK "\xEF\x80\xA3"    // U+F023 lock (armed)
#define ICON_UNLOCK "\xEF\x82\x9C"  // U+F09C lock-open (off)
#define ICON_SHIELD "\xEF\x8F\xAD"  // U+F3ED shield-halved (alarm)
// The extra icons need their own font; LVGL's symbols use Montserrat.
bool isExtraIcon(const char *sym);

namespace col {
// Caravan theme, matched to the decals on the van: plain dark background with
// the van's X (subtle camo fill, white outline, green pinstripes), a subtle camo
// header strip, solid gunmetal-grey tiles (the van body) with white text, and
// the decals' white and green as highlights. Status colours keep
// their meanings: amber warning, red alarm, fuchsia critical, blue cold/water.
// Same values in web/index.html (:root) and the app's Colors.kt.
// 2026-11 restyle to match the app: gunmetal cards with a static top-to-bottom
// gradient (CARD_TOP -> CARD_BOT; CARD is the mid tone used for blending) and
// dark inner wells (TILE). Gradients are drawn once per redraw like a solid
// fill; no shadows, glows or looping animations (stability first).
constexpr uint32_t BG = 0x0E0F11, RAIL = 0x0A0B0C, CARD = 0x33373C, BORDER = 0x4A4F56;
constexpr uint32_t CARD_TOP = 0x3C4046, CARD_BOT = 0x2A2D32;
constexpr uint32_t TEXT = 0xFFFFFF, MUTED = 0xD3D6D9, DIM = 0x8E949A;
constexpr uint32_t SOLAR = 0xFFD93B, CHARGE = 0x9BE03A, LOAD = 0xFF8A8A, WATER = 0x5CCBFF;
constexpr uint32_t CLIMATE = 0xC4B0FF, ACCENT = 0x9BE03A, WARN = 0xFFB020, ALARM = 0xFF6B6B;
constexpr uint32_t OK = 0x9BE03A, TILE = 0x1E2125, GAS = 0xFFA35C, MAINS = 0x8CC0FF;
constexpr lv_opa_t CARD_OPA = LV_OPA_COVER;
constexpr uint32_t SECURE = 0x9BE03A;  // secure mode armed (green, same as the phone page)
constexpr uint32_t CRIT = 0xF07CFF;  // critical alerts (same colour as the phone page)
constexpr uint32_t HEAT = 0xFB923C;  // diesel heater (warm orange, same as the phone page)
constexpr uint32_t ACCENT_HI = 0xB8F060;  // top of the lime gradient (active nav button)
}  // namespace col
inline uint32_t sevColor(int sev) {
  return sev >= cv::SEV_CRIT ? col::CRIT : sev == cv::SEV_ALARM ? col::ALARM : sev == cv::SEV_WARN ? col::WARN : col::ACCENT;
}

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


// Diesel heater card on the Climate page (ui_heater.cpp). Hidden until a
// heater is set up on the hub (state.htr.link != none).
lv_obj_t *buildHeater(lv_obj_t *page, int x, int y, int w, int h);
void updateHeater();

void buildWeatherWidget(lv_obj_t *statusBar);  // ui_weather.cpp
void buildForecast(lv_obj_t *box, int w, int h, const char *note = nullptr, bool bubble = false);
void updateWeather();
void openWeatherForTest();

// shared actions
void togglePump(int i);
void toggleInverter();
void toggleAircon();
void secureButton();  // arm / disarm, asks first (disarm takes the code once one is set)

}  // namespace ui
