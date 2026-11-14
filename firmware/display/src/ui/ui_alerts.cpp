// ALERTS (active alerts + editable rules) and SETTINGS pages.
#include <Preferences.h>

#include "../app.h"
#include "ui.h"

namespace ui {

void setSleepMinutes(int m);

// ================================================================ ALERTS
static lv_obj_t *alList, *alEmpty, *ruleList;
static uint32_t seenRules = UINT32_MAX;
static uint32_t seenAlertSig = UINT32_MAX;

struct RuleRow {
  uint8_t id;
  lv_obj_t *sw, *name, *desc;
};
static RuleRow rows[cv::MAX_RULES];
static int rowCount = 0;

// Local edits are debounced so tapping "+" five times sends one update.
static uint8_t pendingId = 0;
static uint32_t pendingAtMs = 0;

static void ackAllCb(lv_event_t *) { app::cmd("ack"); }

static void describeRule(const cv::Rule &r, char *buf, size_t n) {
  const cv::MetricInfo &mi = cv::metricInfo(r.metric);
  char dly[16] = "";
  if (r.delaySec >= 60) snprintf(dly, sizeof(dly), " for %um", r.delaySec / 60);
  else if (r.delaySec) snprintf(dly, sizeof(dly), " for %us", r.delaySec);
  const char *act = r.action == cv::ACT_AIRCON_OFF     ? "  > aircon off"
                    : r.action == cv::ACT_INVERTER_OFF ? "  > inverter off"
                    : r.action == cv::ACT_PUMPS_OFF    ? "  > pumps off" : "";
  snprintf(buf, n, "%s %s %.*f%s%s%s", mi.label, r.cmp == cv::CMP_GT ? ">" : "<", mi.step < 1 ? 1 : 0,
           r.threshold, mi.unit, dly, act);
}

static cv::Rule *ruleById(uint8_t id) { return app::rules.find(id); }

static void refreshRow(int i) {
  cv::Rule *r = ruleById(rows[i].id);
  if (!r) return;
  char buf[96];
  describeRule(*r, buf, sizeof(buf));
  setText(rows[i].desc, "%s", buf);
  setText(rows[i].name, "%s", r->name);
  uint32_t sc = r->sev == cv::SEV_ALARM ? col::ALARM : r->sev == cv::SEV_WARN ? col::WARN : col::ACCENT;
  lv_obj_set_style_text_color(rows[i].name, c(r->enabled ? sc : col::DIM), 0);
  if (r->enabled) lv_obj_add_state(rows[i].sw, LV_STATE_CHECKED);
  else lv_obj_clear_state(rows[i].sw, LV_STATE_CHECKED);
}

static void ruleSwCb(lv_event_t *e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  cv::Rule *r = ruleById(rows[i].id);
  if (!r) return;
  r->enabled = lv_obj_has_state(rows[i].sw, LV_STATE_CHECKED);
  refreshRow(i);
  app::cmdRule(*r);
}

static void ruleStep(lv_event_t *e, int dir) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  cv::Rule *r = ruleById(rows[i].id);
  if (!r) return;
  r->threshold += dir * cv::metricInfo(r->metric).step;
  refreshRow(i);
  pendingId = r->id;
  pendingAtMs = millis() + 1200;
}
static void ruleMinusCb(lv_event_t *e) { ruleStep(e, -1); }
static void rulePlusCb(lv_event_t *e) { ruleStep(e, +1); }

static void rebuildRules() {
  lv_obj_clean(ruleList);
  rowCount = 0;
  for (int i = 0; i < app::rules.count && i < cv::MAX_RULES; i++) {
    const cv::Rule &r = app::rules.rules[i];
    lv_obj_t *row = lv_obj_create(ruleList);
    lv_obj_set_size(row, lv_pct(100), 58);
    lv_obj_set_style_bg_color(row, c(col::TILE), 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_pad_all(row, 6, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    RuleRow &rr = rows[rowCount];
    rr.id = r.id;
    rr.sw = lv_switch_create(row);
    lv_obj_set_size(rr.sw, 44, 24);
    lv_obj_align(rr.sw, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(rr.sw, ruleSwCb, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)rowCount);
    rr.name = lbl(row, &lv_font_montserrat_14, col::TEXT, "");
    lv_obj_set_pos(rr.name, 54, 2);
    rr.desc = lbl(row, &lv_font_montserrat_12, col::MUTED, "");
    lv_obj_set_width(rr.desc, 190);
    lv_label_set_long_mode(rr.desc, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(rr.desc, 54, 24);
    lv_obj_t *mn = button(row, 36, 36, LV_SYMBOL_MINUS, col::CARD, ruleMinusCb, (void *)(intptr_t)rowCount);
    lv_obj_align(mn, LV_ALIGN_RIGHT_MID, -42, 0);
    lv_obj_t *pl = button(row, 36, 36, LV_SYMBOL_PLUS, col::CARD, rulePlusCb, (void *)(intptr_t)rowCount);
    lv_obj_align(pl, LV_ALIGN_RIGHT_MID, 0, 0);
    rowCount++;
    refreshRow(rowCount - 1);
  }
}

void buildAlerts(lv_obj_t *p) {
  lv_obj_t *a = card(p, 12, 12, 300, 412, "ACTIVE ALERTS", col::ALARM);
  lv_obj_t *ack = button(a, 92, 30, LV_SYMBOL_OK " Ack all", col::TILE, ackAllCb);
  lv_obj_set_style_text_font(lv_obj_get_child(ack, 0), &lv_font_montserrat_12, 0);
  lv_obj_align(ack, LV_ALIGN_TOP_RIGHT, 0, -6);
  alList = lv_obj_create(a);
  lv_obj_set_size(alList, 272, 350);
  lv_obj_set_pos(alList, 0, 32);
  lv_obj_set_style_bg_opa(alList, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(alList, 0, 0);
  lv_obj_set_style_pad_all(alList, 0, 0);
  lv_obj_set_style_pad_row(alList, 8, 0);
  lv_obj_set_flex_flow(alList, LV_FLEX_FLOW_COLUMN);
  alEmpty = lbl(a, &lv_font_montserrat_16, col::CHARGE, LV_SYMBOL_OK "  All clear");
  lv_obj_align(alEmpty, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t *r = card(p, 324, 12, 376, 412, "ALERT RULES  -  tap +/- to adjust", col::WARN);
  ruleList = lv_obj_create(r);
  lv_obj_set_size(ruleList, 348, 364);
  lv_obj_set_pos(ruleList, 0, 22);
  lv_obj_set_style_bg_opa(ruleList, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ruleList, 0, 0);
  lv_obj_set_style_pad_all(ruleList, 0, 0);
  lv_obj_set_style_pad_row(ruleList, 8, 0);
  lv_obj_set_flex_flow(ruleList, LV_FLEX_FLOW_COLUMN);
}

void updateAlerts() {
  const cv::State &s = app::state;
  // Only rebuild the alert list when it actually changed.
  uint32_t sig = s.alertCount;
  for (int i = 0; i < s.alertCount; i++) sig = sig * 31 + s.alerts[i].ruleId * 7 + s.alerts[i].acked + (uint32_t)(s.alerts[i].value * 10);
  if (sig != seenAlertSig) {
    seenAlertSig = sig;
    lv_obj_clean(alList);
    for (int i = 0; i < s.alertCount; i++) {
      const cv::ActiveAlert &al = s.alerts[i];
      uint32_t cc = al.sev == cv::SEV_ALARM ? col::ALARM : al.sev == cv::SEV_WARN ? col::WARN : col::ACCENT;
      lv_obj_t *it = lv_obj_create(alList);
      lv_obj_set_size(it, lv_pct(100), LV_SIZE_CONTENT);
      lv_obj_set_style_bg_color(it, lv_color_mix(c(cc), c(col::CARD), 50), 0);
      lv_obj_set_style_border_side(it, LV_BORDER_SIDE_LEFT, 0);
      lv_obj_set_style_border_width(it, 4, 0);
      lv_obj_set_style_border_color(it, c(cc), 0);
      lv_obj_set_style_radius(it, 10, 0);
      lv_obj_set_style_pad_all(it, 10, 0);
      lv_obj_t *m = lbl(it, &lv_font_montserrat_14, col::TEXT, al.msg);
      lv_obj_set_width(m, lv_pct(100));
      lv_label_set_long_mode(m, LV_LABEL_LONG_WRAP);
      char when[32] = "";
      if (al.since > 1700000000) {
        time_t t = al.since + s.tzOff;
        struct tm lt;
        gmtime_r(&t, &lt);
        snprintf(when, sizeof(when), "since %02d:%02d%s", lt.tm_hour, lt.tm_min, al.acked ? " - acknowledged" : "");
      } else if (al.acked) {
        strcpy(when, "acknowledged");
      }
      lv_obj_t *w = lbl(it, &lv_font_montserrat_12, col::MUTED, when);
      lv_obj_set_flex_flow(it, LV_FLEX_FLOW_COLUMN);
      (void)w;
    }
    if (s.alertCount) lv_obj_add_flag(alEmpty, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(alEmpty, LV_OBJ_FLAG_HIDDEN);
  }

  if (seenRules != app::rulesVer) {
    seenRules = app::rulesVer;
    bool same = rowCount == app::rules.count;
    for (int i = 0; same && i < rowCount; i++) same = rows[i].id == app::rules.rules[i].id;
    if (same) for (int i = 0; i < rowCount; i++) refreshRow(i);  // keep scroll position
    else rebuildRules();
  }
}

// Called from main loop via ui::loop -> cheap; flushes debounced rule edits.
void flushRuleEdits() {
  if (pendingId && (int32_t)(millis() - pendingAtMs) >= 0) {
    if (cv::Rule *r = ruleById(pendingId)) app::cmdRule(*r);
    pendingId = 0;
  }
}

// ================================================================ SETTINGS
static lv_obj_t *sRemote, *sInfo, *sSleep, *sPresetDd, *learnBox, *learnLbl;
static lv_obj_t *kbModal, *kbTa, *sTankCard;
static int learnSlot = -1;

static void sleepCb(lv_event_t *e) {
  static const int mins[] = {0, 1, 5, 15, 30};
  setSleepMinutes(mins[lv_dropdown_get_selected(lv_event_get_target(e))]);
}

static void remoteCb(lv_event_t *e) {
  JsonDocument d;
  d["c"] = "remote_control";
  d["on"] = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
  app::send(d);
}

static void closeLearn() {
  if (learnBox) {
    lv_obj_del(learnBox);
    learnBox = nullptr;
  }
}

static void learnCancelCb(lv_event_t *) { closeLearn(); }

static void startLearn(int slot, const char *name) {
  JsonDocument d;
  d["c"] = "ac_learn";
  d["slot"] = slot;
  d["name"] = name;
  app::send(d);
  closeLearn();
  learnBox = lv_obj_create(lv_layer_top());
  lv_obj_set_size(learnBox, 460, 220);
  lv_obj_center(learnBox);
  lv_obj_set_style_bg_color(learnBox, c(col::CARD), 0);
  lv_obj_set_style_border_color(learnBox, c(col::CLIMATE), 0);
  lv_obj_set_style_border_width(learnBox, 2, 0);
  lv_obj_set_style_radius(learnBox, 16, 0);
  lv_obj_t *sp = lv_spinner_create(learnBox, 1000, 60);
  lv_obj_set_size(sp, 60, 60);
  lv_obj_align(sp, LV_ALIGN_TOP_MID, 0, 6);
  learnLbl = lbl(learnBox, &lv_font_montserrat_16, col::TEXT, "");
  lv_obj_set_width(learnLbl, 420);
  lv_label_set_long_mode(learnLbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(learnLbl, LV_TEXT_ALIGN_CENTER, 0);
  setText(learnLbl, "Point the aircon remote at the IR receiver\nand press the button for \"%s\"", name);
  lv_obj_align(learnLbl, LV_ALIGN_CENTER, 0, 20);
  lv_obj_t *cb = button(learnBox, 100, 34, "Cancel", col::TILE, learnCancelCb);
  lv_obj_align(cb, LV_ALIGN_BOTTOM_MID, 0, 0);
}

// Reply from hub about the learn session (polled below while the box is open).
void handleLearnReply(JsonObjectConst m) {
  const char *st = m["state"] | "";
  if (!learnBox) return;
  if (!strcmp(st, "ok")) {
    closeLearn();
    toast("Aircon code learned", col::CHARGE);
    app::cmd("hello");  // refresh preset names
  } else if (!strcmp(st, "timeout")) {
    closeLearn();
    toast("No IR code received - try again closer to the receiver", col::WARN);
  }
}

// Generic on-screen keyboard prompt.
static void (*kbDone)(const char *text) = nullptr;

static void kbEventCb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_READY) {
    String text = lv_textarea_get_text(kbTa);
    lv_obj_del(kbModal);
    kbModal = nullptr;
    if (kbDone) kbDone(text.c_str());
  } else if (code == LV_EVENT_CANCEL) {
    lv_obj_del(kbModal);
    kbModal = nullptr;
  }
}

static void askText(const char *title, const char *initial, int maxLen, void (*done)(const char *)) {
  kbDone = done;
  kbModal = lv_obj_create(lv_layer_top());
  lv_obj_set_size(kbModal, 800, 480);
  lv_obj_set_pos(kbModal, 0, 0);
  lv_obj_set_style_bg_color(kbModal, c(col::BG), 0);
  lv_obj_set_style_bg_opa(kbModal, LV_OPA_90, 0);
  lv_obj_set_style_border_width(kbModal, 0, 0);
  lv_obj_t *t = lbl(kbModal, &lv_font_montserrat_20, col::TEXT, title);
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 10);
  kbTa = lv_textarea_create(kbModal);
  lv_textarea_set_one_line(kbTa, true);
  lv_textarea_set_max_length(kbTa, maxLen);
  lv_textarea_set_text(kbTa, initial);
  lv_obj_set_width(kbTa, 420);
  lv_obj_align(kbTa, LV_ALIGN_TOP_MID, 0, 50);
  lv_obj_t *kb = lv_keyboard_create(kbModal);
  lv_obj_set_size(kb, 780, 280);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_textarea(kb, kbTa);
  lv_obj_add_event_cb(kb, kbEventCb, LV_EVENT_ALL, nullptr);
}

static void learnNameDone(const char *name) { startLearn(learnSlot, name[0] ? name : "Preset"); }
static void askName(int slot, const char *initial) {
  learnSlot = slot;
  askText("Name this aircon preset (e.g. Cool 22 auto)", initial, 15, learnNameDone);
}

// ---------------- weather location (town search, no GPS needed)
static lv_obj_t *sLocLbl, *locBox;

static void locSearchDone(const char *q) {
  if (strlen(q) < 2) return;
  JsonDocument d;
  d["c"] = "location_search";
  d["q"] = q;
  app::send(d);
  toast("Searching...", col::ACCENT);
}
static void locSearchCb(lv_event_t *) { askText("Town or place name (e.g. Mudgee)", "", 40, locSearchDone); }

struct LocChoice {
  float lat, lon;
  char name[24];
};
static LocChoice locChoices[6];

static void closeLocBox(lv_event_t *) {
  if (locBox) lv_obj_del_async(locBox);  // may be called from one of its own buttons
  locBox = nullptr;
}
static void locPickCb(lv_event_t *e) {
  const LocChoice *ch = (const LocChoice *)lv_event_get_user_data(e);
  JsonDocument d;
  d["c"] = "location_set";
  d["lat"] = ch->lat;
  d["lon"] = ch->lon;
  d["name"] = ch->name;
  d["src"] = 3;
  app::send(d);
  toast("Weather location set - forecast updating", col::CHARGE);
  closeLocBox(nullptr);
}

void handleLocResults(JsonObjectConst m) {
  closeLocBox(nullptr);
  if (m["err"].is<const char *>()) {
    toast(m["err"], col::WARN);
    return;
  }
  JsonArrayConst list = m["list"];
  if (list.size() == 0) {
    toast("No places found - check the spelling", col::WARN);
    return;
  }
  size_t n = list.size() > 6 ? 6 : list.size();
  locBox = lv_obj_create(lv_layer_top());
  lv_obj_set_size(locBox, 520, 128 + 54 * n);
  lv_obj_center(locBox);
  lv_obj_set_style_bg_color(locBox, c(col::CARD), 0);
  lv_obj_set_style_border_color(locBox, c(col::WATER), 0);
  lv_obj_set_style_border_width(locBox, 2, 0);
  lv_obj_set_style_radius(locBox, 16, 0);
  lv_obj_set_flex_flow(locBox, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(locBox, 6, 0);
  lbl(locBox, &lv_font_montserrat_16, col::MUTED, "Pick your location");
  int i = 0;
  for (JsonObjectConst r : list) {
    if (i >= 6) break;
    LocChoice &ch = locChoices[i++];
    ch.lat = r["lat"] | 0.0f;
    ch.lon = r["lon"] | 0.0f;
    snprintf(ch.name, sizeof(ch.name), "%s", r["name"] | "");
    char label[64];
    const char *area = r["area"] | "";
    snprintf(label, sizeof(label), "%s%s%s", ch.name, area[0] ? "  -  " : "", area);
    lv_obj_t *b = button(locBox, 488, 46, label, col::TILE, locPickCb, &ch);
    lv_obj_set_style_text_font(lv_obj_get_child(b, 0), &lv_font_montserrat_14, 0);
  }
  button(locBox, 488, 40, "Cancel", col::BG, closeLocBox);
}

static void learnNewCb(lv_event_t *) {
  if (app::meta.acPresetCount >= cv::MAX_AC_PRESETS) {
    toast("Maximum 8 presets - delete one first", col::WARN);
    return;
  }
  askName(app::meta.acPresetCount, "");
}
static void learnOffCb(lv_event_t *) { startLearn(-1, "OFF"); }
static void delPresetDo(void *ud) {
  JsonDocument d;
  d["c"] = "ac_del";
  d["slot"] = (int)(intptr_t)ud;
  app::send(d);
  app::cmd("hello");
}
static void delPresetCb(lv_event_t *) {
  if (!app::meta.acPresetCount) return;
  int sel = lv_dropdown_get_selected(sPresetDd);
  confirm("Delete preset", app::meta.acPresetName[sel], delPresetDo, (void *)(intptr_t)sel);
}

struct CalCtx {
  int tank;
  bool full;
};
static CalCtx calCtx[cv::NUM_TANKS * 2];
static void calDo(void *ud) {
  auto *cc = (CalCtx *)ud;
  JsonDocument d;
  d["c"] = "tank_cal";
  d["i"] = cc->tank;
  d["point"] = cc->full ? "full" : "empty";
  app::send(d);
}
static void calCb(lv_event_t *e) {
  auto *cc = (CalCtx *)lv_event_get_user_data(e);
  static char msg[96];
  snprintf(msg, sizeof(msg), "Is %s %s right now?\nThe current sender reading will be saved.",
           app::meta.tankName[cc->tank], cc->full ? "FULL" : "EMPTY");
  confirm("Calibrate tank", msg, calDo, cc);
}

static void resetRulesDo(void *) { app::cmd("rules_reset"); }
static void resetRulesCb(lv_event_t *) {
  confirm("Reset alert rules", "Replace all alert rules with the defaults?", resetRulesDo);
}

static lv_obj_t *settingsCard(lv_obj_t *parent, const char *title, int h, uint32_t accent) {
  lv_obj_t *cd = card(parent, 0, 0, 676, h, title, accent);
  return cd;
}

void buildSettings(lv_obj_t *p) {
  lv_obj_t *col = lv_obj_create(p);
  lv_obj_set_size(col, 712, 436);
  lv_obj_set_pos(col, 0, 0);
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(col, 0, 0);
  lv_obj_set_style_pad_all(col, 12, 0);
  lv_obj_set_style_pad_row(col, 12, 0);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);

  // ---- screen + connectivity
  lv_obj_t *a = settingsCard(col, "DISPLAY & CONNECTIVITY", 130, col::ACCENT);
  lv_obj_t *l1 = lbl(a, &lv_font_montserrat_16, col::TEXT, "Screen sleep");
  lv_obj_set_pos(l1, 0, 30);
  sSleep = lv_dropdown_create(a);
  lv_dropdown_set_options(sSleep, "Never\n1 min\n5 min\n15 min\n30 min");
  int sm = sleepMinutes();
  lv_dropdown_set_selected(sSleep, sm == 0 ? 0 : sm == 1 ? 1 : sm == 5 ? 2 : sm == 15 ? 3 : 4);
  lv_obj_set_width(sSleep, 130);
  lv_obj_set_pos(sSleep, 150, 22);
  lv_obj_add_event_cb(sSleep, sleepCb, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_t *l2 = lbl(a, &lv_font_montserrat_16, col::TEXT, "Allow remote control\n(phone / web)");
  lv_obj_set_pos(l2, 330, 22);
  sRemote = lv_switch_create(a);
  lv_obj_align(sRemote, LV_ALIGN_TOP_RIGHT, 0, 26);
  lv_obj_add_event_cb(sRemote, remoteCb, LV_EVENT_VALUE_CHANGED, nullptr);
  sInfo = lbl(a, &lv_font_montserrat_12, col::MUTED, "");
  lv_obj_set_pos(sInfo, 0, 78);

  // ---- aircon learning
  lv_obj_t *b = settingsCard(col, "AIRCON  -  learn codes from your remote", 120, col::CLIMATE);
  lv_obj_t *bn = button(b, 170, 40, LV_SYMBOL_PLUS " Learn preset", col::TILE, learnNewCb);
  lv_obj_set_pos(bn, 0, 26);
  lv_obj_t *bo = button(b, 150, 40, LV_SYMBOL_POWER " Learn OFF", col::TILE, learnOffCb);
  lv_obj_set_pos(bo, 180, 26);
  sPresetDd = lv_dropdown_create(b);
  lv_obj_set_width(sPresetDd, 170);
  lv_obj_set_pos(sPresetDd, 352, 28);
  lv_obj_t *bd = button(b, 110, 40, LV_SYMBOL_TRASH " Delete", col::TILE, delPresetCb);
  lv_obj_align(bd, LV_ALIGN_TOP_RIGHT, 0, 26);
  lv_obj_t *hint = lbl(b, &lv_font_montserrat_12, col::MUTED,
                       "Set the remote to the mode/temp/fan you want, then press its button when asked.");
  lv_obj_set_pos(hint, 0, 76);

  // ---- weather location
  lv_obj_t *w = settingsCard(col, "WEATHER LOCATION", 96, col::WATER);
  sLocLbl = lbl(w, &lv_font_montserrat_16, col::TEXT, "Not set");
  lv_obj_set_pos(sLocLbl, 0, 30);
  lv_obj_t *wl = lbl(w, &lv_font_montserrat_12, col::MUTED, "Or tap \"Set weather location here\" on the phone level page.");
  lv_obj_set_pos(wl, 0, 56);
  lv_obj_t *ws = button(w, 170, 40, LV_SYMBOL_GPS " Search town", col::TILE, locSearchCb);
  lv_obj_align(ws, LV_ALIGN_TOP_RIGHT, 0, 22);

  // ---- tank calibration
  lv_obj_t *t = settingsCard(col, "WATER TANK CALIBRATION", 70 + cv::NUM_TANKS * 46, col::WATER);
  sTankCard = t;  // hidden when levels come from the BMPRO (calibrated there)
  for (int i = 0; i < cv::NUM_TANKS; i++) {
    int y = 26 + i * 46;
    lv_obj_t *n = lbl(t, &lv_font_montserrat_16, col::TEXT, app::meta.tankName[i]);
    lv_obj_set_pos(n, 0, y + 8);
    calCtx[i * 2] = {i, false};
    calCtx[i * 2 + 1] = {i, true};
    lv_obj_t *e = button(t, 130, 38, "Set EMPTY", col::TILE, calCb, &calCtx[i * 2]);
    lv_obj_set_pos(e, 360, y);
    lv_obj_t *f = button(t, 130, 38, "Set FULL", col::TILE, calCb, &calCtx[i * 2 + 1]);
    lv_obj_set_pos(f, 500, y);
  }

  // ---- maintenance
  lv_obj_t *m = settingsCard(col, "ALERTS", 90, col::WARN);
  lv_obj_t *rr = button(m, 220, 40, LV_SYMBOL_REFRESH " Reset alert rules", col::TILE, resetRulesCb);
  lv_obj_set_pos(rr, 0, 26);
  lv_obj_t *hl = lbl(m, &lv_font_montserrat_12, col::MUTED,
                     "Full rule editing (new rules, delays, actions)\nis on the web dashboard.");
  lv_obj_set_pos(hl, 240, 30);
}

void updateSettings() {
  static uint32_t seen = UINT32_MAX;
  const cv::State &s = app::state;
  if (seen != app::metaVer) {
    seen = app::metaVer;
    String opts;
    for (int i = 0; i < app::meta.acPresetCount; i++) {
      if (i) opts += "\n";
      opts += app::meta.acPresetName[i];
    }
    lv_dropdown_set_options(sPresetDd, opts.length() ? opts.c_str() : "(none)");
    static const char *srcName[] = {"", "GPS", "phone", "town search"};
    if (app::meta.locSrc) setText(sLocLbl, "%s  (%s)", app::meta.locName[0] ? app::meta.locName : "Unnamed spot",
                                  srcName[app::meta.locSrc & 3]);
    else setText(sLocLbl, "Not set - no forecast yet");
    if (app::meta.tankCal) lv_obj_clear_flag(sTankCard, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(sTankCard, LV_OBJ_FLAG_HIDDEN);
    if (app::meta.remoteControl) lv_obj_add_state(sRemote, LV_STATE_CHECKED);
    else lv_obj_clear_state(sRemote, LV_STATE_CHECKED);
  }
  setText(sInfo, "Hub link: %s   Wi-Fi: %s   Cloud: %s   IP: %s   Hub fw %s   Link errors: %u",
          app::linkUp ? "OK" : "DOWN", s.wifi ? "up" : "down", s.cloud ? "connected" : "offline",
          s.ip[0] ? s.ip : "-", app::meta.fw, (unsigned)app::linkErrors);

  // Poll learn status once a second while the learn dialog is showing.
  static uint32_t lastPoll = 0;
  if (learnBox && millis() - lastPoll > 1000) {
    lastPoll = millis();
    app::cmd("ac_learn_status");
  }
}

}  // namespace ui
