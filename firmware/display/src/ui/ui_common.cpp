#include <string.h>
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
  lv_obj_set_style_bg_opa(o, col::CARD_OPA, 0);
  lv_obj_set_style_border_color(o, c(col::BORDER), 0);
  lv_obj_set_style_border_width(o, 1, 0);
  lv_obj_set_style_radius(o, 16, 0);
  lv_obj_set_style_pad_all(o, CARD_PAD, 0);
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
bool isExtraIcon(const char *sym) {
  return sym && (!strcmp(sym, ICON_WATER) || !strcmp(sym, ICON_TAP) || !strcmp(sym, ICON_LOCK) ||
                 !strcmp(sym, ICON_UNLOCK) || !strcmp(sym, ICON_SHIELD));
}

lv_obj_t *tile(lv_obj_t *parent, int w, int h, const char *sym, const char *text, lv_event_cb_t cb, void *ud) {
  lv_obj_t *b = lv_btn_create(parent);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_bg_color(b, c(col::TILE), 0);
  lv_obj_set_style_radius(b, 14, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_border_color(b, c(col::BORDER), 0);
  lv_obj_set_style_pad_all(b, 6, 0);
  lv_obj_t *ic = lbl(b, isExtraIcon(sym) ? (LARGE ? &ui_icons_24 : &ui_icons_20)
                                         : (LARGE ? &lv_font_montserrat_24 : &lv_font_montserrat_20),
                     col::MUTED, sym);
  lv_obj_align(ic, LV_ALIGN_TOP_LEFT, 2, 2);
  lv_obj_t *t = lbl(b, LARGE ? &lv_font_montserrat_16 : &lv_font_montserrat_14, col::TEXT, text);
  lv_obj_align(t, LV_ALIGN_BOTTOM_LEFT, 2, LARGE ? -19 : -16);
  lv_obj_t *s = lbl(b, LARGE ? &lv_font_montserrat_14 : &lv_font_montserrat_12, col::MUTED, "Off");
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
  lv_obj_set_width(t, PAGE_W * 3 / 4);
  lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(t, LV_ALIGN_BOTTOM_MID, RAIL_W / 2, -18);  // centred over the page, not the rail
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
  lv_obj_set_width(m, LARGE ? 500 : 420);
  lv_obj_center(m);
  lv_obj_add_event_cb(m, confirmCb, LV_EVENT_VALUE_CHANGED, new ConfirmCtx{onYes, ud});
}

// Temperature text colour, blended smoothly between stops. Air: blue at 10 C
// and below, neutral 17-23 C, amber at 27 C, strong red at 30 C and above.
// Fridge: blue at 0 C (freezing), neutral 2-5 C (safe), amber 6.5 C, red 8 C.
struct TempStop { float t; uint32_t rgb; };
static uint32_t blend(const TempStop *s, int n, float t) {
  if (t <= s[0].t) return s[0].rgb;
  for (int i = 1; i < n; i++) {
    if (t > s[i].t) continue;
    float f = (t - s[i - 1].t) / (s[i].t - s[i - 1].t);
    uint32_t a = s[i - 1].rgb, b = s[i].rgb, out = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
      float ca = (a >> sh) & 0xFF, cb = (b >> sh) & 0xFF;
      out |= (uint32_t)(ca + (cb - ca) * f + 0.5f) << sh;
    }
    return out;
  }
  return s[n - 1].rgb;
}

uint32_t tempColor(int role, float t) {
  static const TempStop air[] = {{10, col::WATER}, {17, col::TEXT}, {23, col::TEXT}, {27, col::WARN}, {30, col::ALARM}};
  static const TempStop fridge[] = {{0, col::WATER}, {2, col::TEXT}, {5, col::TEXT}, {6.5f, col::WARN}, {8, col::ALARM}};
  return role == cv::TEMP_FRIDGE ? blend(fridge, 5, t) : blend(air, 5, t);
}

uint32_t socColor(float soc) { return soc >= 50 ? col::CHARGE : soc >= 25 ? col::WARN : col::ALARM; }

// Tank 3 is grey water by default: "full" is the bad direction.
uint32_t tankColor(int i, float pct) {
  if (i == 2) return pct > 85 ? col::ALARM : pct > 70 ? col::WARN : col::MUTED;
  return pct < 15 ? col::ALARM : pct < 30 ? col::WARN : col::WATER;
}

uint32_t gasColor(float pct) { return pct < 10 ? col::ALARM : pct < 25 ? col::WARN : col::GAS; }

void fmtDuration(char *buf, size_t n, int32_t m) {
  if (m < 0) snprintf(buf, n, "--");
  else if (m >= 60 * 99) snprintf(buf, n, "99h+");
  else if (m >= 60) snprintf(buf, n, "%dh %02dm", (int)(m / 60), (int)(m % 60));
  else snprintf(buf, n, "%dm", (int)m);
}

// ================================================================ shell
static lv_obj_t *gPages[PAGE_COUNT];
static constexpr int NAV_COUNT = 5;
static const int kRail[NAV_COUNT] = {PAGE_HOME, PAGE_POWER, PAGE_WATER, PAGE_CLIMATE, PAGE_MORE};
static lv_obj_t *gNavBtns[NAV_COUNT];
static lv_obj_t *gMoreAlerts;
static lv_obj_t *gTitle, *gClock, *gIcoLink, *gIcoWifi, *gIcoCloud, *gBell, *gBellCount;
static lv_obj_t *gBanner, *gBannerText, *gBannerBtn;
static lv_obj_t *gSecBtn, *gSecIcon, *gSecText;  // secure mode button, top of the nav rail
static int gPage = -1;

static const char *kPageNames[PAGE_COUNT] = {"Home", "Power", "Water", "Climate", "More", "Level", "Alerts", "Settings"};
static const char *kPageIcons[PAGE_COUNT] = {LV_SYMBOL_HOME,  LV_SYMBOL_CHARGE, ICON_WATER,     LV_SYMBOL_SHUFFLE,
                                             LV_SYMBOL_BARS,  LV_SYMBOL_GPS,    LV_SYMBOL_BELL, LV_SYMBOL_SETTINGS};
static bool isSubPage(int p) { return p == PAGE_LEVEL || p == PAGE_ALERTS || p == PAGE_SETTINGS; }

// Set from the phone (Setup tab); the hub keeps it so the display never writes flash.
int sleepSeconds() { return app::meta.screenSleepSec; }

static void navCb(lv_event_t *e) { showPage((int)(intptr_t)lv_event_get_user_data(e)); }

void showPage(int p) {
  if (p == gPage) return;
  for (int i = 0; i < PAGE_COUNT; i++) {
    if (i == p) lv_obj_clear_flag(gPages[i], LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(gPages[i], LV_OBJ_FLAG_HIDDEN);
  }
  int railPage = isSubPage(p) ? PAGE_MORE : p;  // sub-pages light up "More"
  for (int i = 0; i < NAV_COUNT; i++) {
    bool on = kRail[i] == railPage;
    lv_obj_set_style_bg_opa(gNavBtns[i], on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(gNavBtns[i], 0), c(on ? col::ACCENT : col::MUTED), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(gNavBtns[i], 1), c(on ? col::TEXT : col::MUTED), 0);
  }
  gPage = p;
  // Sub-pages get a back arrow in the title (tap it to return to More).
  if (isSubPage(p)) setText(gTitle, LV_SYMBOL_LEFT "  %s", kPageNames[p]);
  else lv_label_set_text(gTitle, kPageNames[p]);
  app::stateDirty = true;  // refresh the newly visible page right away
}

static void ackCb(lv_event_t *) { app::cmd("ack"); }
static void titleCb(lv_event_t *) {
  if (isSubPage(gPage)) showPage(PAGE_MORE);
}
static void bellCb(lv_event_t *) { showPage(PAGE_ALERTS); }

// "More" page: big tiles for the things you only need now and then.
static void moreTileCb(lv_event_t *e) { showPage((int)(intptr_t)lv_event_get_user_data(e)); }
static lv_obj_t *moreTile(lv_obj_t *p, int i, int page, const char *sym, const char *title, const char *sub,
                          uint32_t accent) {
  constexpr int w = colW(3), h = LARGE ? 280 : 220;
  lv_obj_t *b = lv_btn_create(p);
  lv_obj_set_size(b, w, h);
  lv_obj_set_pos(b, colX(i, 3), (PAGE_H - h) / 2);  // vertically centred in the page
  lv_obj_set_style_bg_color(b, c(col::CARD), 0);
  lv_obj_set_style_border_color(b, c(col::BORDER), 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_radius(b, 18, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_add_event_cb(b, moreTileCb, LV_EVENT_CLICKED, (void *)(intptr_t)page);
  constexpr int top = (h - 220) / 2 + 28;  // icon / title / caption block centred in the tile
  lv_obj_t *ic = lbl(b, &lv_font_montserrat_48, accent, sym);
  lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, top);
  lv_obj_t *t = lbl(b, LARGE ? &lv_font_montserrat_24 : &lv_font_montserrat_20, col::TEXT, title);
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, top + 84);
  lv_obj_t *s = lbl(b, LARGE ? &lv_font_montserrat_16 : &lv_font_montserrat_14, col::MUTED, sub);
  lv_obj_set_width(s, w - 26);
  lv_obj_set_style_text_align(s, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(s, LV_LABEL_LONG_WRAP);
  lv_obj_align(s, LV_ALIGN_TOP_MID, 0, top + (LARGE ? 124 : 118));
  return s;
}

// ---------------------------------------------------------------- system restart
// More -> Restart system: asks the hub to restart, waits up to 4 s for it to
// say so, then restarts the display too. If the hub doesn't answer, the
// display still restarts (and says so). Both boards also have a watchdog that
// restarts them by itself if the firmware hangs (see main.cpp).
static uint32_t gRestartAt = 0;  // when the restart was asked for (0 = not restarting)
static bool gHubAcked = false;
static lv_obj_t *gRestartMsg = nullptr;

static void restartDo(void *) {
  gRestartAt = millis() | 1;
  gHubAcked = false;
  lv_obj_t *ov = lv_obj_create(lv_layer_top());
  lv_obj_set_size(ov, SCR_W, SCR_H);
  lv_obj_set_style_bg_color(ov, c(col::BG), 0);
  lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(ov, 0, 0);
  lv_obj_set_style_radius(ov, 0, 0);
  gRestartMsg = lbl(ov, LARGE ? &lv_font_montserrat_28 : &lv_font_montserrat_24, col::TEXT,
                    LV_SYMBOL_REFRESH "  Restarting the hub...");
  lv_obj_set_width(gRestartMsg, SCR_W - 80);
  lv_obj_set_style_text_align(gRestartMsg, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(gRestartMsg, LV_LABEL_LONG_WRAP);
  lv_obj_center(gRestartMsg);
  app::cmd("restart");
}

static void restartCb(lv_event_t *) {
  confirm("Restart system", "Restart the display and the hub?\nEverything is back in about 20 seconds.", restartDo);
}

static void restartLoop() {
  if (!gRestartAt) return;
  static uint32_t decidedAt = 0;
  uint32_t now = millis();
  if (!decidedAt && (gHubAcked || now - gRestartAt > 4000)) {
    decidedAt = now | 1;
    if (gHubAcked) lv_label_set_text(gRestartMsg, LV_SYMBOL_REFRESH "  Hub is restarting.\nRestarting the display...");
    else {
      lv_label_set_text(gRestartMsg, LV_SYMBOL_WARNING "  The hub didn't respond.\nRestarting the display only.");
      lv_obj_set_style_text_color(gRestartMsg, c(col::WARN), 0);
    }
  }
  if (decidedAt && now - decidedAt > 2000) ESP.restart();  // long enough to read the message
}

static void buildMore(lv_obj_t *p) {
  moreTile(p, 0, PAGE_LEVEL, LV_SYMBOL_GPS, "Caravan level", "Bubble level and wheel / jockey heights", col::ACCENT);
  gMoreAlerts = moreTile(p, 1, PAGE_ALERTS, LV_SYMBOL_BELL, "Alerts & rules", "", col::WARN);
  moreTile(p, 2, PAGE_SETTINGS, LV_SYMBOL_SETTINGS, "Settings",
           HAS_AIRCON ? "Aircon, weather location, display" : "Weather location, display", col::MUTED);

  // Below the tiles, kept small so it isn't pressed by accident (and it asks first).
  lv_obj_t *rb = lv_btn_create(p);
  lv_obj_set_size(rb, LARGE ? 260 : 220, LARGE ? 54 : 46);
  lv_obj_align(rb, LV_ALIGN_BOTTOM_MID, 0, -12);
  lv_obj_set_style_bg_color(rb, c(col::TILE), 0);
  lv_obj_set_style_border_color(rb, c(col::BORDER), 0);
  lv_obj_set_style_border_width(rb, 1, 0);
  lv_obj_set_style_radius(rb, 14, 0);
  lv_obj_set_style_shadow_width(rb, 0, 0);
  lv_obj_add_event_cb(rb, restartCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *rl = lbl(rb, LARGE ? &lv_font_montserrat_16 : &lv_font_montserrat_14, col::MUTED,
                     LV_SYMBOL_REFRESH "  Restart system");
  lv_obj_center(rl);
}

static void updateMore() {
  int n = 0;  // acknowledged alerts don't count (they're hidden everywhere)
  for (int i = 0; i < app::state.alertCount; i++) n += !app::state.alerts[i].acked;
  if (n) setText(gMoreAlerts, "%d active alert%s", n, n == 1 ? "" : "s");
  else setText(gMoreAlerts, "All clear. Edit alert thresholds here");
}
static void bannerCb(lv_event_t *) { showPage(PAGE_ALERTS); }

static void buildShell() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, c(col::BG), 0);
  // The camo X graphic from the side of the van, behind the cards.
  lv_obj_set_style_bg_img_src(scr, &tex_bg, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // Nav rail
  lv_obj_t *rail = panel(scr, 0, 0, RAIL_W, SCR_H);
  lv_obj_set_style_bg_color(rail, c(col::RAIL), 0);
  lv_obj_set_style_bg_opa(rail, LV_OPA_COVER, 0);
  // Thin lime pinstripe down the edge, like the line on the van's stripe.
  lv_obj_set_style_border_side(rail, LV_BORDER_SIDE_RIGHT, 0);
  lv_obj_set_style_border_color(rail, lv_color_mix(c(col::ACCENT), c(col::RAIL), 150), 0);
  lv_obj_set_style_border_width(rail, 2, 0);
  // Secure mode (anti-theft) button where the logo used to be: on every page,
  // shows the state at a glance, tap to arm or disarm.
#ifdef DISPLAY_DEMO_MODE
  constexpr int secH = 42;
  lv_obj_t *demoTag = lbl(rail, &lv_font_montserrat_12, col::WARN, "DEMO");
  lv_obj_align(demoTag, LV_ALIGN_TOP_MID, 0, secH + 6);
#else
  constexpr int secH = LARGE ? 50 : 44;
#endif
  gSecBtn = lv_btn_create(rail);
  lv_obj_set_size(gSecBtn, RAIL_W - 16, secH);
  lv_obj_align(gSecBtn, LV_ALIGN_TOP_MID, 0, 6);
  lv_obj_set_style_pad_all(gSecBtn, 0, 0);
  lv_obj_set_style_radius(gSecBtn, 12, 0);
  lv_obj_set_style_shadow_width(gSecBtn, 0, 0);
  lv_obj_set_style_border_width(gSecBtn, 1, 0);
  lv_obj_set_style_border_color(gSecBtn, c(col::BORDER), 0);
  lv_obj_set_style_bg_color(gSecBtn, c(col::TILE), 0);
  lv_obj_set_ext_click_area(gSecBtn, 6);
  lv_obj_add_event_cb(gSecBtn, [](lv_event_t *) { secureButton(); }, LV_EVENT_CLICKED, nullptr);
  gSecIcon = lbl(gSecBtn, &ui_icons_20, col::MUTED, ICON_UNLOCK);
  lv_obj_align(gSecIcon, LV_ALIGN_TOP_MID, 0, 4);
  gSecText = lbl(gSecBtn, &lv_font_montserrat_12, col::MUTED, "Arm");
  lv_obj_align(gSecText, LV_ALIGN_BOTTOM_MID, 0, -3);
  // Nav buttons share the rail height below the logo.
  constexpr int navTop = LARGE ? 60 : 52, navStep = (SCR_H - navTop - 4) / NAV_COUNT;
  constexpr int navBtn = navStep - 12 < RAIL_W - 16 ? navStep - 12 : RAIL_W - 16;
  for (int i = 0; i < NAV_COUNT; i++) {
    int pg = kRail[i];
    lv_obj_t *b = lv_btn_create(rail);
    lv_obj_set_size(b, navBtn, navBtn);
    lv_obj_set_pos(b, (RAIL_W - navBtn) / 2, navTop + i * navStep + (navStep - 12 - navBtn) / 2);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, c(col::TILE), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
    lv_obj_t *ic = lbl(b, isExtraIcon(kPageIcons[pg]) ? &ui_icons_28 : &lv_font_montserrat_28, col::MUTED, kPageIcons[pg]);
    lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, LARGE ? 12 : 8);
    lv_obj_t *tx = lbl(b, LARGE ? &lv_font_montserrat_14 : &lv_font_montserrat_12, col::MUTED, kPageNames[pg]);
    lv_obj_align(tx, LV_ALIGN_BOTTOM_MID, 0, LARGE ? -10 : -6);
    lv_obj_add_event_cb(b, navCb, LV_EVENT_CLICKED, (void *)(intptr_t)pg);
    gNavBtns[i] = b;
  }

  // Status bar
  lv_obj_t *bar = panel(scr, RAIL_W, 0, PAGE_W, BAR_H);
  // Header strip: subtle camo (baked into tex_bg) with a green line under it.
  lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_color(bar, lv_color_mix(c(col::ACCENT), c(col::RAIL), 150), 0);
  lv_obj_set_style_border_width(bar, 2, 0);
  gTitle = lbl(bar, LARGE ? &lv_font_montserrat_24 : &lv_font_montserrat_20, col::TEXT, "Home");
  lv_obj_align(gTitle, LV_ALIGN_LEFT_MID, 16, 2);
  lv_obj_add_flag(gTitle, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(gTitle, 10);
  lv_obj_add_event_cb(gTitle, titleCb, LV_EVENT_CLICKED, nullptr);
  gBellCount = lbl(bar, &lv_font_montserrat_12, col::TEXT, "");
  lv_obj_set_style_bg_color(gBellCount, c(col::ALARM), 0);
  lv_obj_set_style_bg_opa(gBellCount, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(gBellCount, 8, 0);
  lv_obj_set_style_pad_hor(gBellCount, 5, 0);
  lv_obj_align(gBellCount, LV_ALIGN_RIGHT_MID, -12, -8);
  gBell = lbl(bar, &lv_font_montserrat_20, col::DIM, LV_SYMBOL_BELL);
  lv_obj_add_flag(gBell, LV_OBJ_FLAG_CLICKABLE);  // bell = shortcut to Alerts
  lv_obj_set_ext_click_area(gBell, 14);
  lv_obj_add_event_cb(gBell, bellCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_align(gBell, LV_ALIGN_RIGHT_MID, -22, 2);
  gIcoCloud = lbl(bar, &lv_font_montserrat_20, col::DIM, LV_SYMBOL_UPLOAD);
  lv_obj_align(gIcoCloud, LV_ALIGN_RIGHT_MID, -62, 2);
  gIcoWifi = lbl(bar, &lv_font_montserrat_20, col::DIM, LV_SYMBOL_WIFI);
  lv_obj_align(gIcoWifi, LV_ALIGN_RIGHT_MID, -98, 2);
  gIcoLink = lbl(bar, &lv_font_montserrat_20, col::DIM, LV_SYMBOL_USB);
  lv_obj_align(gIcoLink, LV_ALIGN_RIGHT_MID, -138, 2);
  gClock = lbl(bar, LARGE ? &lv_font_montserrat_24 : &lv_font_montserrat_20, col::TEXT, "--:--");
  lv_obj_align(gClock, LV_ALIGN_RIGHT_MID, -180, 2);
  buildWeatherWidget(bar);

  // Pages
  for (int i = 0; i < PAGE_COUNT; i++) {
    gPages[i] = panel(scr, RAIL_W, BAR_H, PAGE_W, PAGE_H);
    lv_obj_add_flag(gPages[i], LV_OBJ_FLAG_HIDDEN);
  }

  // Alert banner (top layer so it floats over every page)
  gBanner = lv_obj_create(lv_layer_top());
  // Lives in the status bar (replacing the page title) so it never hides content.
  // Spans from the title to just left of the weather widget (see ui_weather.cpp).
  // On a small screen that leaves too little room, so it covers the widget instead.
  constexpr int bannerW = LARGE ? PAGE_W - WX_RIGHT_EDGE - 8 - 16 : 452;
  lv_obj_set_size(gBanner, bannerW, BAR_H - 8);
  lv_obj_set_pos(gBanner, RAIL_W + 8, 4);
  lv_obj_set_style_radius(gBanner, 10, 0);
  lv_obj_set_style_border_width(gBanner, 0, 0);
  lv_obj_set_style_pad_hor(gBanner, 10, 0);
  lv_obj_set_style_pad_ver(gBanner, 0, 0);
  lv_obj_clear_flag(gBanner, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(gBanner, bannerCb, LV_EVENT_CLICKED, nullptr);
  gBannerText = lbl(gBanner, &lv_font_montserrat_14, col::TEXT, "");
  lv_obj_set_width(gBannerText, bannerW - 2 * 10 - 76 - 10);
  lv_label_set_long_mode(gBannerText, LV_LABEL_LONG_DOT);
  lv_obj_align(gBannerText, LV_ALIGN_LEFT_MID, 0, 0);
  gBannerBtn = button(gBanner, 76, 28, LV_SYMBOL_OK " Ack", 0x000000, ackCb);
  lv_obj_set_style_text_font(lv_obj_get_child(gBannerBtn, 0), &lv_font_montserrat_14, 0);
  lv_obj_set_style_bg_opa(gBannerBtn, LV_OPA_30, 0);
  lv_obj_align(gBannerBtn, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_add_flag(gBanner, LV_OBJ_FLAG_HIDDEN);
}

static void updateSecure() {
  const cv::Secure &x = app::state.sec;
  uint32_t col = col::MUTED;
  const char *icon = ICON_UNLOCK;
  char txt[16] = "Arm";
  bool on = app::linkUp;
  switch (on ? x.mode : cv::SEC_OFF) {
    case cv::SEC_ARMING:
      col = col::WARN;
      icon = ICON_LOCK;
      snprintf(txt, sizeof(txt), "%u s", x.armLeft);
      break;
    case cv::SEC_ARMED:
      col = col::SECURE;
      icon = ICON_LOCK;
      strcpy(txt, "Armed");
      break;
    case cv::SEC_ENTRY:  // movement: counting down to the alarm
      col = (millis() / 500) % 2 ? col::WARN : col::ALARM;
      icon = ICON_SHIELD;
      snprintf(txt, sizeof(txt), "%u:%02u", x.armLeft / 60, x.armLeft % 60);
      break;
    case cv::SEC_TRIGGERED:
      col = (millis() / 500) % 2 ? col::CRIT : col::ALARM;  // flashes
      icon = ICON_SHIELD;
      strcpy(txt, "ALARM");
      break;
    default: break;
  }
  bool lit = on && x.mode != cv::SEC_OFF;
  lv_obj_set_style_bg_color(gSecBtn, lit ? lv_color_mix(c(col), c(col::TILE), 70) : c(col::TILE), 0);
  lv_obj_set_style_border_color(gSecBtn, lit ? c(col) : c(col::BORDER), 0);
  lv_obj_set_style_text_color(gSecIcon, c(col), 0);
  lv_obj_set_style_text_color(gSecText, c(lit ? col::TEXT : col::MUTED), 0);
  if (strcmp(lv_label_get_text(gSecIcon), icon)) lv_label_set_text(gSecIcon, icon);
  setText(gSecText, "%s", txt);
}

static void secureArm(void *) { app::cmdSecure(true); toast("Secure mode arms in 60 seconds - time to leave", col::WARN); }
static void secureDisarm(void *) { app::cmdSecure(false); toast("Secure mode off", col::CHARGE); }

// ---- disarm keypad (once a code is set on the phone)
static lv_obj_t *gPad = nullptr, *gPadDots = nullptr, *gPadMsg = nullptr;
static char gPadCode[9] = "";

static void padClose() {
  if (gPad) lv_obj_del(gPad);
  gPad = nullptr;
  gPadCode[0] = 0;
}

static void padDots() {
  char d[24] = "";
  size_t n = strlen(gPadCode);
  for (size_t i = 0; i < n; i++) strcat(d, LV_SYMBOL_BULLET " ");
  lv_label_set_text(gPadDots, n ? d : "Enter code");
  lv_obj_set_style_text_color(gPadDots, c(n ? col::TEXT : col::MUTED), 0);
}

static void padCb(lv_event_t *e) {
  lv_obj_t *m = lv_event_get_target(e);
  const char *k = lv_btnmatrix_get_btn_text(m, lv_btnmatrix_get_selected_btn(m));
  if (!k) return;
  size_t n = strlen(gPadCode);
  if (!strcmp(k, LV_SYMBOL_BACKSPACE)) {
    if (n) gPadCode[n - 1] = 0;
  } else if (!strcmp(k, LV_SYMBOL_OK)) {
    if (n < 4) {
      lv_label_set_text(gPadMsg, "The code is 4 to 8 digits");
      return;
    }
    app::cmdSecure(false, gPadCode);  // the hub checks it; "Wrong code" comes back as a toast
    padClose();
    return;
  } else if (n < 8) {
    gPadCode[n] = k[0];
    gPadCode[n + 1] = 0;
  }
  padDots();
}

static void secureKeypad(const char *title, const char *why) {
  padClose();
  gPad = lv_obj_create(lv_layer_top());
  lv_obj_set_size(gPad, SCR_W, SCR_H);
  lv_obj_set_style_bg_color(gPad, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(gPad, LV_OPA_70, 0);
  lv_obj_set_style_border_width(gPad, 0, 0);
  lv_obj_set_style_radius(gPad, 0, 0);
  lv_obj_clear_flag(gPad, LV_OBJ_FLAG_SCROLLABLE);
  constexpr int bw = LARGE ? 420 : 380, bh = LARGE ? 540 : 450;
  lv_obj_t *box = card(gPad, (SCR_W - bw) / 2, (SCR_H - bh) / 2, bw, bh, nullptr);
  lv_obj_t *t = lbl(box, LARGE ? &lv_font_montserrat_24 : &lv_font_montserrat_20, col::TEXT, title);
  lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_t *x = button(box, 44, 36, LV_SYMBOL_CLOSE, col::TILE, [](lv_event_t *) { padClose(); });
  lv_obj_align(x, LV_ALIGN_TOP_RIGHT, 0, -4);
  gPadMsg = lbl(box, &lv_font_montserrat_14, col::MUTED, why);
  lv_obj_set_width(gPadMsg, bw - 2 * CARD_PAD);
  lv_label_set_long_mode(gPadMsg, LV_LABEL_LONG_WRAP);
  lv_obj_align(gPadMsg, LV_ALIGN_TOP_LEFT, 0, 36);
  gPadDots = lbl(box, &lv_font_montserrat_28, col::MUTED, "");
  lv_obj_align(gPadDots, LV_ALIGN_TOP_MID, 0, 84);
  static const char *keys[] = {"1", "2", "3", "\n", "4", "5", "6", "\n", "7", "8", "9", "\n",
                               LV_SYMBOL_BACKSPACE, "0", LV_SYMBOL_OK, ""};
  lv_obj_t *m = lv_btnmatrix_create(box);
  lv_btnmatrix_set_map(m, keys);
  lv_obj_set_size(m, bw - 2 * CARD_PAD, bh - 2 * CARD_PAD - 128);
  lv_obj_align(m, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_opa(m, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(m, 0, 0);
  lv_obj_set_style_pad_all(m, 0, 0);
  lv_obj_set_style_pad_gap(m, 10, 0);
  lv_obj_set_style_bg_color(m, c(col::TILE), LV_PART_ITEMS);
  lv_obj_set_style_text_color(m, c(col::TEXT), LV_PART_ITEMS);
  lv_obj_set_style_text_font(m, &lv_font_montserrat_28, LV_PART_ITEMS);
  lv_obj_set_style_radius(m, 12, LV_PART_ITEMS);
  lv_obj_set_style_shadow_width(m, 0, LV_PART_ITEMS);
  lv_obj_add_event_cb(m, padCb, LV_EVENT_VALUE_CHANGED, nullptr);
  padDots();
}

void secureButton() {
  const cv::Secure &x = app::state.sec;
  static char msg[320];
  if (!app::linkUp) {
    toast("Hub not connected", col::WARN);
  } else if (x.mode == cv::SEC_OFF) {
    if (!x.sensor) {
      toast("Secure mode needs the level sensor, and it isn't answering", col::WARN);
      return;
    }
    snprintf(msg, sizeof(msg),
             "Sounds the alarm and alerts your phone if the van is jacked up, hitched or towed. "
             "Wind rocking the van is ignored. If it moves, you get 5 minutes to disarm%s.\n\n"
             "You have 60 seconds to leave. Sensitivity: %s (change it on the phone).",
             app::meta.secCodeSet ? " with your code" : "", cv::secureSensText(x.sens));
    confirm("Arm secure mode?", msg, secureArm);
  } else if (app::meta.secCodeSet) {
    // Disarming here takes the code; the phone needs none (it has its own lock screen).
    if (x.mode == cv::SEC_ENTRY)
      snprintf(msg, sizeof(msg), "Movement: %s. The alarm goes off in %u:%02u.", cv::secureCauseText(x.cause),
               x.armLeft / 60, x.armLeft % 60);
    else if (x.mode == cv::SEC_TRIGGERED)
      snprintf(msg, sizeof(msg), "ALARM: %s.", cv::secureCauseText(x.cause));
    else
      snprintf(msg, sizeof(msg), "Enter your code to turn secure mode off.");
    secureKeypad("Disarm", msg);
  } else if (x.mode == cv::SEC_ENTRY || x.mode == cv::SEC_TRIGGERED) {
    snprintf(msg, sizeof(msg), "%s: %s.\n\nTurn secure mode off?\n(Set a disarm code on the phone so only you can do this.)",
             x.mode == cv::SEC_ENTRY ? "Movement" : "ALARM", cv::secureCauseText(x.cause));
    confirm(x.mode == cv::SEC_ENTRY ? "Movement detected" : "Theft alarm", msg, secureDisarm);
  } else {
    snprintf(msg, sizeof(msg), "%s. Movement now: tilt %.1f of %.1f deg, turned %.0f of %.0f deg, shaking %u of %u s.\n\nTurn secure mode off?",
             x.mode == cv::SEC_ARMING ? "Arming" : "Armed", x.tilt, x.tiltLim, x.turn, x.turnLim, x.shake, x.shakeLim);
    confirm("Secure mode", msg, secureDisarm);
  }
}

static void updateShell() {
  const cv::State &s = app::state;
  updateWeather();
  updateSecure();
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
  // The bell only counts alerts nobody has acknowledged yet.
  if (unacked) {
    lv_obj_clear_flag(gBellCount, LV_OBJ_FLAG_HIDDEN);
    setText(gBellCount, "%d", unacked);
  } else {
    lv_obj_add_flag(gBellCount, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_set_style_text_color(gBell, c(unacked ? col::TEXT : col::DIM), 0);

  // Banner: hub offline beats everything, then the worst un-acked alert.
  if (!app::linkUp) {
    lv_obj_clear_flag(gBanner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(gBanner, c(col::WARN), 0);
    setText(gBannerText, LV_SYMBOL_WARNING "  Hub not connected - check RS485 cable");
    lv_obj_add_flag(gBannerBtn, LV_OBJ_FLAG_HIDDEN);
  } else if (top && worst >= cv::SEV_WARN) {
    lv_obj_clear_flag(gBanner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(gBannerBtn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(gBanner, c(sevColor(worst)), 0);
    if (unacked > 1) setText(gBannerText, LV_SYMBOL_WARNING "  %s  (+%d more)", top->msg, unacked - 1);
    else setText(gBannerText, LV_SYMBOL_WARNING "  %s", top->msg);
  } else {
    lv_obj_add_flag(gBanner, LV_OBJ_FLAG_HIDDEN);
  }
}

// ================================================================ public
static void replyHandler(JsonObjectConst m);

void begin() {
  buildShell();
  buildHome(gPages[PAGE_HOME]);
  buildPower(gPages[PAGE_POWER]);
  buildWater(gPages[PAGE_WATER]);
  buildClimate(gPages[PAGE_CLIMATE]);
  buildMore(gPages[PAGE_MORE]);
  buildLevel(gPages[PAGE_LEVEL]);
  buildAlerts(gPages[PAGE_ALERTS]);
  buildSettings(gPages[PAGE_SETTINGS]);
  showPage(PAGE_HOME);
  app::onReply(replyHandler);
}

void handleLearnReply(JsonObjectConst m);  // ui_alerts.cpp

static void replyHandler(JsonObjectConst m) {
  const char *t = m["t"] | "";
  if (!strcmp(t, "err")) toast(m["msg"] | "Error", col::ALARM);
  else if (!strcmp(t, "ac_learn")) handleLearnReply(m);
  else if (!strcmp(t, "tank_cal")) toast("Tank calibration saved", col::CHARGE);
  else if (!strcmp(t, "level")) toast(m["msg"] | "", col::ACCENT);
  else if (!strcmp(t, "restarting")) gHubAcked = true;
}

void loop() {
  restartLoop();
  static uint32_t last = 0;
  if (millis() - last < 200) return;  // 5 Hz UI refresh is plenty
  last = millis();
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
    case PAGE_MORE: updateMore(); break;
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
