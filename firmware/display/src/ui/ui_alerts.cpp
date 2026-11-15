// ALERTS (active alerts + read-only rules) and SETTINGS pages.

#include "../app.h"
#include "ui.h"

namespace ui {

// ================================================================ ALERTS
// The rules are shown read-only here: they're edited on the phone (web
// dashboard / Android app), which is quicker than the touchscreen.
static lv_obj_t *alList, *alEmpty, *ruleList;
static uint32_t seenRules = UINT32_MAX;
static uint32_t seenAlertSig = UINT32_MAX;

struct RuleRow {
  uint8_t id;
  lv_obj_t *state, *name, *desc;
};
static RuleRow rows[cv::MAX_RULES];
static int rowCount = 0;

// Active alerts | rules, side by side.
static constexpr int AL_W = splitW(40), RULES_W = INNER_W - GAP - AL_W;
static constexpr int RULE_LIST_W = innerOf(RULES_W), STATE_W = LARGE ? 52 : 44;

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

static void refreshRow(int i) {
  cv::Rule *r = app::rules.find(rows[i].id);
  if (!r) return;
  char buf[96];
  describeRule(*r, buf, sizeof(buf));
  setText(rows[i].desc, "%s", buf);
  setText(rows[i].name, "%s", r->name);
  uint32_t sc = r->sev == cv::SEV_ALARM ? col::ALARM : r->sev == cv::SEV_WARN ? col::WARN : col::ACCENT;
  lv_obj_set_style_text_color(rows[i].name, c(r->enabled ? sc : col::DIM), 0);
  lv_obj_set_style_text_color(rows[i].desc, c(r->enabled ? col::MUTED : col::DIM), 0);
  setText(rows[i].state, "%s", r->enabled ? "On" : "Off");
  lv_obj_set_style_text_color(rows[i].state, c(r->enabled ? col::CHARGE : col::DIM), 0);
}

static void rebuildRules() {
  lv_obj_clean(ruleList);
  rowCount = 0;
  for (int i = 0; i < app::rules.count && i < cv::MAX_RULES; i++) {
    const cv::Rule &r = app::rules.rules[i];
    lv_obj_t *row = lv_obj_create(ruleList);
    lv_obj_set_size(row, lv_pct(100), LARGE ? 58 : 50);
    lv_obj_set_style_bg_color(row, c(col::TILE), 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_pad_all(row, 6, 0);
    lv_obj_clear_flag(row, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    RuleRow &rr = rows[rowCount];
    rr.id = r.id;
    rr.name = lbl(row, LARGE ? &lv_font_montserrat_16 : &lv_font_montserrat_14, col::TEXT, "");
    lv_obj_set_pos(rr.name, 4, 0);
    rr.desc = lbl(row, LARGE ? &lv_font_montserrat_14 : &lv_font_montserrat_12, col::MUTED, "");
    lv_obj_set_width(rr.desc, RULE_LIST_W - 12 - 8 - STATE_W);
    lv_label_set_long_mode(rr.desc, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(rr.desc, 4, LARGE ? 24 : 21);
    rr.state = lbl(row, &lv_font_montserrat_14, col::CHARGE, "");
    lv_obj_align(rr.state, LV_ALIGN_RIGHT_MID, -4, 0);
    rowCount++;
    refreshRow(rowCount - 1);
  }
}

void buildAlerts(lv_obj_t *p) {
  lv_obj_t *a = card(p, GAP, GAP, AL_W, INNER_H, "ACTIVE ALERTS", col::ALARM);
  lv_obj_t *ack = button(a, LARGE ? 104 : 92, LARGE ? 34 : 30, LV_SYMBOL_OK " Ack all", col::TILE, ackAllCb);
  lv_obj_set_style_text_font(lv_obj_get_child(ack, 0), LARGE ? &lv_font_montserrat_14 : &lv_font_montserrat_12, 0);
  lv_obj_align(ack, LV_ALIGN_TOP_RIGHT, 0, -6);
  alList = lv_obj_create(a);
  lv_obj_set_size(alList, innerOf(AL_W), innerOf(INNER_H) - 34);
  lv_obj_set_pos(alList, 0, 32);
  lv_obj_set_style_bg_opa(alList, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(alList, 0, 0);
  lv_obj_set_style_pad_all(alList, 0, 0);
  lv_obj_set_style_pad_row(alList, 8, 0);
  lv_obj_set_flex_flow(alList, LV_FLEX_FLOW_COLUMN);
  alEmpty = lbl(a, &lv_font_montserrat_16, col::CHARGE, LV_SYMBOL_OK "  All clear");
  lv_obj_align(alEmpty, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t *r = card(p, GAP + AL_W + GAP, GAP, RULES_W, INNER_H, "ALERT RULES  -  change these on your phone", col::WARN);
  ruleList = lv_obj_create(r);
  lv_obj_set_size(ruleList, RULE_LIST_W, innerOf(INNER_H) - 20);
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
    int shown = 0;
    for (int i = 0; i < s.alertCount; i++) {
      const cv::ActiveAlert &al = s.alerts[i];
      if (al.acked) continue;  // acknowledged alerts drop off the list
      shown++;
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
        snprintf(when, sizeof(when), "since %02d:%02d", lt.tm_hour, lt.tm_min);
      }
      lv_obj_t *w = lbl(it, &lv_font_montserrat_12, col::MUTED, when);
      lv_obj_set_flex_flow(it, LV_FLEX_FLOW_COLUMN);
      (void)w;
    }
    if (shown) lv_obj_add_flag(alEmpty, LV_OBJ_FLAG_HIDDEN);
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

// ================================================================ SETTINGS
static lv_obj_t *sRemote, *sInfo, *sUrl, *sSleep, *sPresetDd, *learnBox, *learnLbl;
static lv_obj_t *kbModal, *kbTa;
static int learnSlot = -1;

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
  lv_obj_set_size(learnBox, LARGE ? 540 : 460, LARGE ? 250 : 220);
  lv_obj_center(learnBox);
  lv_obj_set_style_bg_color(learnBox, c(col::CARD), 0);
  lv_obj_set_style_border_color(learnBox, c(col::CLIMATE), 0);
  lv_obj_set_style_border_width(learnBox, 2, 0);
  lv_obj_set_style_radius(learnBox, 16, 0);
  lv_obj_t *sp = lv_spinner_create(learnBox, 1000, 60);
  lv_obj_set_size(sp, 60, 60);
  lv_obj_align(sp, LV_ALIGN_TOP_MID, 0, 6);
  learnLbl = lbl(learnBox, &lv_font_montserrat_16, col::TEXT, "");
  lv_obj_set_width(learnLbl, LARGE ? 500 : 420);
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
  lv_obj_set_size(kbModal, SCR_W, SCR_H);
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
  lv_obj_set_width(kbTa, SCR_W / 2);
  lv_obj_align(kbTa, LV_ALIGN_TOP_MID, 0, 50);
  lv_obj_t *kb = lv_keyboard_create(kbModal);
  lv_obj_set_size(kb, SCR_W - 20, SCR_H * 7 / 12);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_textarea(kb, kbTa);
  lv_obj_add_event_cb(kb, kbEventCb, LV_EVENT_ALL, nullptr);
}

static void learnNameDone(const char *name) { startLearn(learnSlot, name[0] ? name : "Preset"); }
static void askName(int slot, const char *initial) {
  learnSlot = slot;
  askText("Name this aircon preset (e.g. Cool 22 auto)", initial, 15, learnNameDone);
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

static lv_obj_t *settingsCard(lv_obj_t *parent, const char *title, int h, uint32_t accent) {
  lv_obj_t *cd = card(parent, 0, 0, INNER_W - 12, h, title, accent);  // 12 px left for the scrollbar
  return cd;
}

void buildSettings(lv_obj_t *p) {
  lv_obj_t *col = lv_obj_create(p);
  lv_obj_set_size(col, PAGE_W, PAGE_H);
  lv_obj_set_pos(col, 0, 0);
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(col, 0, 0);
  lv_obj_set_style_pad_all(col, 12, 0);
  lv_obj_set_style_pad_row(col, 12, 0);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);

  // ---- all setup is done on the phone (the touchscreen is too slow for forms)
  lv_obj_t *ph = settingsCard(col, "SET UP FROM YOUR PHONE", LARGE ? 160 : 140, col::ACCENT);
  lv_obj_t *l0 = lbl(ph, &lv_font_montserrat_16, col::MUTED, "On the caravan Wi-Fi, open this on your phone and tap Setup:");
  lv_obj_set_pos(l0, 0, 26);
  sUrl = lbl(ph, LARGE ? &lv_font_montserrat_28 : &lv_font_montserrat_24, col::TEXT, "http://caravan.local");
  lv_obj_set_pos(sUrl, 0, 54);
  lv_obj_t *l1 = lbl(ph, LARGE ? &lv_font_montserrat_14 : &lv_font_montserrat_12, col::MUTED,
                     "Alert rules, weather location, tank calibration, temperature sensors,\n"
                     "Victron and BMPRO links, and this screen's sleep time are all set there.");
  lv_obj_set_pos(l1, 0, LARGE ? 104 : 94);

  // ---- this display
  lv_obj_t *a = settingsCard(col, "THIS DISPLAY", 130, col::ACCENT);
  sSleep = lbl(a, &lv_font_montserrat_16, col::TEXT, "");
  lv_obj_set_pos(sSleep, 0, 30);
  lv_obj_t *l2 = lbl(a, &lv_font_montserrat_16, col::TEXT, "Allow remote control\n(cloud dashboard)");
  lv_obj_align(l2, LV_ALIGN_TOP_RIGHT, -70, 22);
  sRemote = lv_switch_create(a);
  lv_obj_align(sRemote, LV_ALIGN_TOP_RIGHT, 0, 26);
  lv_obj_add_event_cb(sRemote, remoteCb, LV_EVENT_VALUE_CHANGED, nullptr);
  sInfo = lbl(a, &lv_font_montserrat_12, col::MUTED, "");
  lv_obj_set_pos(sInfo, 0, 78);

  // ---- aircon learning (only when aircon control is switched on, see ui.h).
  // Stays here because you point the aircon remote at the hub's IR receiver.
  if (HAS_AIRCON) {
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
  }
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
    if (sPresetDd) lv_dropdown_set_options(sPresetDd, opts.length() ? opts.c_str() : "(none)");
    if (app::meta.remoteControl) lv_obj_add_state(sRemote, LV_STATE_CHECKED);
    else lv_obj_clear_state(sRemote, LV_STATE_CHECKED);
    int sm = app::meta.screenSleepMin;
    if (sm) setText(sSleep, "Screen sleeps after %d min  " LV_SYMBOL_RIGHT " change on phone", sm);
    else setText(sSleep, "Screen never sleeps  " LV_SYMBOL_RIGHT " change on phone");
  }
  if (s.ip[0]) setText(sUrl, "http://caravan.local   or   http://%s", s.ip);
  else setText(sUrl, "http://caravan.local");
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
