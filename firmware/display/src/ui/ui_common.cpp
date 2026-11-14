#include <Preferences.h>
#include <cstdarg>

#include "../app.h"
#include "../board.h"
#include "ui.h"

namespace ui {

// ================================================================ helpers
lv_obj_t *panel(lv_obj_t *parent, int x, int y, int w, int h) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_set_style_radius(o, 0, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

lv_obj_t *card(lv_obj_t *parent, int x, int y, int w, int h, const char *title, uint32_t accent) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, c(col::CARD), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(o, c(col::BORDER), 0);
  lv_obj_set_style_border_width(o, 1, 0);
  lv_obj_set_style_radius(o, 16, 0);
  lv_obj_set_style_pad_all(o, 14, 0);
  lv_obj_set_style_shadow_width(o, 18, 0);
  lv_obj_set_style_shadow_color(o, lv_color_black(), 0);
  lv_obj_set_style_shadow_opa(o, LV_OPA_30, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  if (title) {
    // Small accent dot + uppercase caption, like a modern dashboard tile.
    lv_obj_t *dot = lv_obj_create(o);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_pos(dot, 0, 4);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, c(accent), 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_t *t = lbl(o, &lv_font_montserrat_12, col::MUTED, title);
    lv_obj_set_style_text_letter_space(t, 1, 0);
    lv_obj_set_pos(t, 14, 0);
  }
  return o;
}

lv_obj_t *lbl(lv_obj_t *parent, const lv_font_t *f, uint32_t color, const char *text) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, c(color), 0);
  lv_label_set_text(l, text);
  return l;
}

// lv_label_set_text_fmt has no float support by default - use vsnprintf.
void setText(lv_obj_t *l, const char *fmt, ...) {
  char buf[96];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (strcmp(lv_label_get_text(l), buf) != 0) lv_label_set_text(l, buf);  // avoid redraws
}

lv_obj_t *button(lv_obj_t *parent, int w, int h, const char *text, uint32_t bg, lv_event_cb_t cb, void *ud) {
  lv_obj_t *b = lv_btn_create(parent);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_bg_color(b, c(bg), 0);
  lv_obj_set_style_radius(b, 12, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_t *l = lbl(b, &lv_font_montserrat_16, col::TEXT, text);
  lv_obj_center(l);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
  return b;
}

// Square toggle tile: icon on top, caption + sub-caption below.
lv_obj_t *tile(lv_obj_t *parent, int w, int h, const char *sym, const char *text, lv_event_cb_t cb, void *ud) {
  lv_obj_t *b = lv_btn_create(parent);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_bg_color(b, c(col::TILE), 0);
  lv_obj_set_style_radius(b, 14, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_border_color(b, c(col::BORDER), 0);
  lv_obj_set_style_pad_all(b, 6, 0);
  lv_obj_t *ic = lbl(b, &lv_font_montserrat_20, col::MUTED, sym);
  lv_obj_align(ic, LV_ALIGN_TOP_LEFT, 2, 2);
  lv_obj_t *t = lbl(b, &lv_font_montserrat_14, col::TEXT, text);
  lv_obj_align(t, LV_ALIGN_BOTTOM_LEFT, 2, -16);
  lv_obj_t *s = lbl(b, &lv_font_montserrat_12, col::MUTED, "Off");
  lv_obj_set_width(s, w - 16);
  lv_label_set_long_mode(s, LV_LABEL_LONG_DOT);
  lv_obj_align(s, LV_ALIGN_BOTTOM_LEFT, 2, 0);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
  return b;
}

void setTile(lv_obj_t *t, bool on, uint32_t onColor, const char *sub) {
  lv_obj_set_style_bg_color(t, on ? lv_color_mix(c(onColor), c(col::TILE), 70) : c(col::TILE), 0);
  lv_obj_set_style_border_color(t, on ? c(onColor) : c(col::BORDER), 0);
  lv_obj_set_style_text_color(lv_obj_get_child(t, 0), on ? c(onColor) : c(col::MUTED), 0);
  setText(lv_obj_get_child(t, 2), "%s", sub ? sub : (on ? "On" : "Off"));
}

lv_obj_t *chip(lv_obj_t *parent) {
  lv_obj_t *l = lbl(parent, &lv_font_montserrat_12, col::TEXT, "");
  lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(l, 8, 0);
  lv_obj_set_style_pad_hor(l, 8, 0);
  lv_obj_set_style_pad_ver(l, 3, 0);
  return l;
}

void setChip(lv_obj_t *ch, const char *txt, uint32_t color) {
  setText(ch, "%s", txt);
  lv_obj_set_style_bg_color(ch, lv_color_mix(c(color), c(col::CARD), 60), 0);
  lv_obj_set_style_text_color(ch, c(color), 0);
}

lv_obj_t *vbar(lv_obj_t *parent, int w, int h, uint32_t color) {
  lv_obj_t *b = lv_bar_create(parent);
  lv_obj_set_size(b, w, h);
  lv_bar_set_range(b, 0, 100);
  lv_obj_set_style_bg_color(b, c(col::TILE), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(b, w > h ? h / 2 : 10, LV_PART_MAIN);
  lv_obj_set_style_radius(b, w > h ? h / 2 : 10, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(b, c(color), LV_PART_INDICATOR);
  // Subtle vertical gradient on the fill
  lv_obj_set_style_bg_grad_color(b, lv_color_mix(c(color), lv_color_black(), 170), LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_dir(b, w > h ? LV_GRAD_DIR_HOR : LV_GRAD_DIR_VER, LV_PART_INDICATOR);
  lv_obj_set_style_anim_time(b, 400, 0);
  return b;
}

void setBarColor(lv_obj_t *b, uint32_t color) {
  lv_obj_set_style_bg_color(b, c(color), LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_color(b, lv_color_mix(c(color), lv_color_black(), 170), LV_PART_INDICATOR);
}

void toast(const char *msg, uint32_t color) {
  lv_obj_t *t = lbl(lv_layer_top(), &lv_font_montserrat_16, col::TEXT, msg);
  lv_obj_set_style_bg_color(t, c(col::CARD), 0);
  lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(t, c(color), 0);
  lv_obj_set_style_border_width(t, 2, 0);
  lv_obj_set_style_radius(t, 12, 0);
  lv_obj_set_style_pad_all(t, 12, 0);
  lv_obj_set_width(t, 520);
  lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(t, LV_ALIGN_BOTTOM_MID, 44, -18);
  lv_obj_del_delayed(t, 3500);
}

struct ConfirmCtx {
  void (*fn)(void *);
  void *ud;
};

static void confirmCb(lv_event_t *e) {
  lv_obj_t *m = lv_event_get_current_target(e);
  auto *ctx = (ConfirmCtx *)lv_event_get_user_data(e);
  if (lv_msgbox_get_active_btn(m) == 0 && ctx->fn) ctx->fn(ctx->ud);
  delete ctx;
  lv_msgbox_close(m);
}

void confirm(const char *title, const char *msg, void (*onYes)(void *), void *ud) {
  static const char *btns[] = {"Yes", "Cancel", ""};
  lv_obj_t *m = lv_msgbox_create(nullptr, title, msg, btns, false);
  lv_obj_set_width(m, 420);
  lv_obj_center(m);
  lv_obj_add_event_cb(m, confirmCb, LV_EVENT_VALUE_CHANGED, new ConfirmCtx{onYes, ud});
}

uint32_t tempColor(int role, float t) {
  if (role == cv::TEMP_FRIDGE) return t <= 4.5f ? col::CHARGE : t <= 6.5f ? col::WARN : col::ALARM;
  if (t < 10) return col::WATER;
  if (t <= 27) return col::CHARGE;
  if (t <= 33) return col::WARN;
  return col::ALARM;
}

uint32_t socColor(float soc) { return soc >= 50 ? col::CHARGE : soc >= 25 ? col::WARN : col::ALARM; }

// Tank 3 is grey water by default: "full" is the bad direction.
uint32_t tankColor(int i, float pct) {
  if (i == 2) return pct > 85 ? col::ALARM : pct > 70 ? col::WARN : col::MUTED;
  return pct < 15 ? col::ALARM : pct < 30 ? col::WARN : col::WATER;
}

void fmtDuration(char *buf, size_t n, int32_t m) {
  if (m < 0) snprintf(buf, n, "--");
  else if (m >= 60 * 99) snprintf(buf, n, "99h+");
  else if (m >= 60) snprintf(buf, n, "%dh %02dm", (int)(m / 60), (int)(m % 60));
  else snprintf(buf, n, "%dm", (int)m);
}

// ================================================================ shell
static lv_obj_t *gPages[PAGE_COUNT];
static lv_obj_t *gNavBtns[PAGE_COUNT];
static lv_obj_t *gTitle, *gClock, *gIcoLink, *gIcoWifi, *gIcoCloud, *gBell, *gBellCount;
static lv_obj_t *gBanner, *gBannerText, *gBannerBtn;
static int gPage = -1;
static int gSleepMin = 5;

static const char *kPageNames[PAGE_COUNT] = {"Home", "Power", "Water", "Climate", "Level", "Alerts", "Settings"};
static const char *kPageIcons[PAGE_COUNT] = {LV_SYMBOL_HOME, LV_SYMBOL_CHARGE, LV_SYMBOL_LOOP, LV_SYMBOL_SHUFFLE,
                                             LV_SYMBOL_GPS, LV_SYMBOL_BELL, LV_SYMBOL_SETTINGS};

int sleepMinutes() { return gSleepMin; }

static void navCb(lv_event_t *e) { showPage((int)(intptr_t)lv_event_get_user_data(e)); }

void showPage(int p) {
  if (p == gPage) return;
  for (int i = 0; i < PAGE_COUNT; i++) {
    bool on = i == p;
    if (on) lv_obj_clear_flag(gPages[i], LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(gPages[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_opa(gNavBtns[i], on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(gNavBtns[i], 0), c(on ? col::ACCENT : col::MUTED), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(gNavBtns[i], 1), c(on ? col::TEXT : col::MUTED), 0);
  }
  gPage = p;
  lv_label_set_text(gTitle, kPageNames[p]);
  app::stateDirty = true;  // refresh the newly visible page right away
}

static void ackCb(lv_event_t *) { app::cmd("ack"); }
static void bannerCb(lv_event_t *) { showPage(PAGE_ALERTS); }

static void buildShell() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, c(col::BG), 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // Nav rail
  lv_obj_t *rail = panel(scr, 0, 0, 88, 480);
  lv_obj_set_style_bg_color(rail, c(col::RAIL), 0);
  lv_obj_set_style_bg_opa(rail, LV_OPA_COVER, 0);
  lv_obj_set_style_border_side(rail, LV_BORDER_SIDE_RIGHT, 0);
  lv_obj_set_style_border_color(rail, c(col::BORDER), 0);
  lv_obj_set_style_border_width(rail, 1, 0);
  lv_obj_t *logo = lbl(rail, &lv_font_montserrat_20, col::ACCENT, LV_SYMBOL_CHARGE);
  lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, 8);
  for (int i = 0; i < PAGE_COUNT; i++) {
    lv_obj_t *b = lv_btn_create(rail);
    lv_obj_set_size(b, 72, 58);
    lv_obj_set_pos(b, 8, 40 + i * 62);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, c(col::TILE), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
    lv_obj_t *ic = lbl(b, &lv_font_montserrat_24, col::MUTED, kPageIcons[i]);
    lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, 3);
    lv_obj_t *tx = lbl(b, &lv_font_montserrat_12, col::MUTED, kPageNames[i]);
    lv_obj_align(tx, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_add_event_cb(b, navCb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    gNavBtns[i] = b;
  }

  // Status bar
  lv_obj_t *bar = panel(scr, 88, 0, 712, 44);
  gTitle = lbl(bar, &lv_font_montserrat_20, col::TEXT, "Home");
  lv_obj_align(gTitle, LV_ALIGN_LEFT_MID, 16, 2);
  gBellCount = lbl(bar, &lv_font_montserrat_12, col::TEXT, "");
  lv_obj_set_style_bg_color(gBellCount, c(col::ALARM), 0);
  lv_obj_set_style_bg_opa(gBellCount, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(gBellCount, 8, 0);
  lv_obj_set_style_pad_hor(gBellCount, 5, 0);
  lv_obj_align(gBellCount, LV_ALIGN_RIGHT_MID, -12, -8);
  gBell = lbl(bar, &lv_font_montserrat_20, col::DIM, LV_SYMBOL_BELL);
  lv_obj_align(gBell, LV_ALIGN_RIGHT_MID, -22, 2);
  gIcoCloud = lbl(bar, &lv_font_montserrat_20, col::DIM, LV_SYMBOL_UPLOAD);
  lv_obj_align(gIcoCloud, LV_ALIGN_RIGHT_MID, -62, 2);
  gIcoWifi = lbl(bar, &lv_font_montserrat_20, col::DIM, LV_SYMBOL_WIFI);
  lv_obj_align(gIcoWifi, LV_ALIGN_RIGHT_MID, -98, 2);
  gIcoLink = lbl(bar, &lv_font_montserrat_20, col::DIM, LV_SYMBOL_USB);
  lv_obj_align(gIcoLink, LV_ALIGN_RIGHT_MID, -138, 2);
  gClock = lbl(bar, &lv_font_montserrat_20, col::TEXT, "--:--");
  lv_obj_align(gClock, LV_ALIGN_RIGHT_MID, -180, 2);
  buildWeatherWidget(bar);

  // Pages
  for (int i = 0; i < PAGE_COUNT; i++) {
    gPages[i] = panel(scr, 88, 44, 712, 436);
    lv_obj_add_flag(gPages[i], LV_OBJ_FLAG_HIDDEN);
  }

  // Alert banner (top layer so it floats over every page)
  gBanner = lv_obj_create(lv_layer_top());
  // Lives in the status bar (replacing the page title) so it never hides content.
  lv_obj_set_size(gBanner, 452, 36);
  lv_obj_set_pos(gBanner, 96, 4);
  lv_obj_set_style_radius(gBanner, 10, 0);
  lv_obj_set_style_border_width(gBanner, 0, 0);
  lv_obj_set_style_pad_hor(gBanner, 10, 0);
  lv_obj_set_style_pad_ver(gBanner, 0, 0);
  lv_obj_clear_flag(gBanner, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(gBanner, bannerCb, LV_EVENT_CLICKED, nullptr);
  gBannerText = lbl(gBanner, &lv_font_montserrat_14, col::TEXT, "");
  lv_obj_set_width(gBannerText, 346);
  lv_label_set_long_mode(gBannerText, LV_LABEL_LONG_DOT);
  lv_obj_align(gBannerText, LV_ALIGN_LEFT_MID, 0, 0);
  gBannerBtn = button(gBanner, 76, 28, LV_SYMBOL_OK " Ack", 0x000000, ackCb);
  lv_obj_set_style_text_font(lv_obj_get_child(gBannerBtn, 0), &lv_font_montserrat_14, 0);
  lv_obj_set_style_bg_opa(gBannerBtn, LV_OPA_30, 0);
  lv_obj_align(gBannerBtn, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_add_flag(gBanner, LV_OBJ_FLAG_HIDDEN);
}

static void updateShell() {
  const cv::State &s = app::state;
  updateWeather();
  lv_obj_set_style_text_color(gIcoLink, c(app::linkUp ? col::CHARGE : col::ALARM), 0);
  lv_obj_set_style_text_color(gIcoWifi, c(app::linkUp && s.wifi ? col::TEXT : col::DIM), 0);
  lv_obj_set_style_text_color(gIcoCloud, c(app::linkUp && s.cloud ? col::ACCENT : col::DIM), 0);

  if (s.ts > 1700000000) {
    time_t t = s.ts + s.tzOff;  // hub supplies its local UTC offset
    struct tm lt;
    gmtime_r(&t, &lt);
    setText(gClock, "%02d:%02d", lt.tm_hour, lt.tm_min);
  }

  int unacked = 0, worst = -1;
  const cv::ActiveAlert *top = nullptr;
  for (int i = 0; i < s.alertCount; i++) {
    if (!s.alerts[i].acked) {
      unacked++;
      if ((int)s.alerts[i].sev > worst) {
        worst = s.alerts[i].sev;
        top = &s.alerts[i];
      }
    }
  }
  if (s.alertCount) {
    lv_obj_clear_flag(gBellCount, LV_OBJ_FLAG_HIDDEN);
    setText(gBellCount, "%d", s.alertCount);
  } else {
    lv_obj_add_flag(gBellCount, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_set_style_text_color(gBell, c(s.alertCount ? col::TEXT : col::DIM), 0);

  // Banner: hub offline beats everything, then the worst un-acked alert.
  if (!app::linkUp) {
    lv_obj_clear_flag(gBanner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(gBanner, c(col::WARN), 0);
    setText(gBannerText, LV_SYMBOL_WARNING "  Hub not connected - check RS485 cable");
    lv_obj_add_flag(gBannerBtn, LV_OBJ_FLAG_HIDDEN);
  } else if (top && worst >= cv::SEV_WARN) {
    lv_obj_clear_flag(gBanner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(gBannerBtn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(gBanner, c(worst == cv::SEV_ALARM ? col::ALARM : col::WARN), 0);
    if (unacked > 1) setText(gBannerText, LV_SYMBOL_WARNING "  %s  (+%d more)", top->msg, unacked - 1);
    else setText(gBannerText, LV_SYMBOL_WARNING "  %s", top->msg);
  } else {
    lv_obj_add_flag(gBanner, LV_OBJ_FLAG_HIDDEN);
  }
}

// ================================================================ public
static void replyHandler(JsonObjectConst m);

void begin() {
  Preferences p;
  p.begin("disp", true);
  gSleepMin = p.getInt("sleep", 5);
  p.end();

  buildShell();
  buildHome(gPages[PAGE_HOME]);
  buildPower(gPages[PAGE_POWER]);
  buildWater(gPages[PAGE_WATER]);
  buildClimate(gPages[PAGE_CLIMATE]);
  buildLevel(gPages[PAGE_LEVEL]);
  buildAlerts(gPages[PAGE_ALERTS]);
  buildSettings(gPages[PAGE_SETTINGS]);
  showPage(PAGE_HOME);
  app::onReply(replyHandler);
}

void setSleepMinutes(int m) {
  gSleepMin = m;
  Preferences p;
  p.begin("disp", false);
  p.putInt("sleep", m);
  p.end();
}

void handleLearnReply(JsonObjectConst m);  // ui_alerts.cpp
void handleLocResults(JsonObjectConst m);  // ui_alerts.cpp

static void replyHandler(JsonObjectConst m) {
  const char *t = m["t"] | "";
  if (!strcmp(t, "err")) toast(m["msg"] | "Error", col::ALARM);
  else if (!strcmp(t, "ac_learn")) handleLearnReply(m);
  else if (!strcmp(t, "tank_cal")) toast("Tank calibration saved", col::CHARGE);
  else if (!strcmp(t, "level")) toast(m["msg"] | "", col::ACCENT);
  else if (!strcmp(t, "loc_results")) handleLocResults(m);
}

void loop() {
  static uint32_t last = 0;
  if (millis() - last < 200) return;  // 5 Hz UI refresh is plenty
  last = millis();
  flushRuleEdits();
  if (!app::stateDirty) return;
  updateShell();
  // Only the visible page is refreshed (Home always, it's the default view).
  switch (gPage) {
    case PAGE_HOME: updateHome(); break;
    case PAGE_POWER: updatePower(); break;
    case PAGE_WATER: updateWater(); break;
    case PAGE_CLIMATE: updateClimate(); break;
    case PAGE_LEVEL: updateLevel(); break;
    case PAGE_ALERTS: updateAlerts(); break;
    case PAGE_SETTINGS: updateSettings(); break;
  }
  // Power page keeps a history chart even when hidden.
  if (gPage != PAGE_POWER) updatePower();
  app::stateDirty = false;
}

// ================================================================ shared actions
void togglePump(int i) {
  const cv::Pump &p = app::state.pump[i];
  if (p.lockout && !p.on) {
    toast("Pump locked out - tank is empty", col::WARN);
    return;
  }
  app::cmdPump(i, !p.on);
}

void toggleInverter() { app::cmdInverter(!app::state.inv.on); }

void toggleAircon() {
  if (app::state.ac.on) {
    app::cmdAircon(-1);
  } else if (app::meta.acPresetCount > 0) {
    app::cmdAircon(app::state.ac.preset >= 0 ? app::state.ac.preset : 0);
  } else {
    toast("No aircon presets learned yet - see Settings", col::WARN);
    showPage(PAGE_SETTINGS);
  }
}

}  // namespace ui
