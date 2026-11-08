// POWER, WATER and CLIMATE pages.
#include "../app.h"
#include "ui.h"

namespace ui {

// ================================================================ POWER
static lv_obj_t *spName[cv::NUM_SOLAR], *spChip[cv::NUM_SOLAR], *spW[cv::NUM_SOLAR];
static lv_obj_t *spPv[cv::NUM_SOLAR], *spA[cv::NUM_SOLAR], *spToday[cv::NUM_SOLAR], *spMax[cv::NUM_SOLAR], *spErr[cv::NUM_SOLAR];
static lv_obj_t *bpRows[7];
static lv_obj_t *chart;
static lv_chart_series_t *serSolar, *serLoad;
static uint32_t seenMetaPower = UINT32_MAX;

static lv_obj_t *miniStat(lv_obj_t *parent, int x, int y, const char *cap) {
  lv_obj_t *l = lbl(parent, &lv_font_montserrat_12, col::MUTED, cap);
  lv_obj_set_pos(l, x, y);
  lv_obj_t *v = lbl(parent, &lv_font_montserrat_16, col::TEXT, "--");
  lv_obj_set_pos(v, x, y + 16);
  return v;
}

static void socFullCb(void *) {
  JsonDocument d;
  d["c"] = "soc_set";
  d["soc"] = 100;
  app::send(d);
  toast("Battery state of charge set to 100%", col::CHARGE);
}
static void socBtnCb(lv_event_t *) {
  confirm("Battery full?", "Set state of charge to 100%?\nOnly do this when the battery is fully charged.", socFullCb);
}

void buildPower(lv_obj_t *p) {
  for (int i = 0; i < cv::NUM_SOLAR; i++) {
    lv_obj_t *cd = card(p, 12 + i * 350, 12, 338, 196, i == 0 ? "SOLAR CONTROLLER 1" : "SOLAR CONTROLLER 2", col::SOLAR);
    spName[i] = lbl(cd, &lv_font_montserrat_20, col::TEXT, app::meta.solarName[i]);
    lv_obj_set_pos(spName[i], 0, 22);
    spChip[i] = chip(cd);
    lv_obj_align(spChip[i], LV_ALIGN_TOP_RIGHT, 0, 24);
    spW[i] = lbl(cd, &lv_font_montserrat_40, col::SOLAR, "-- W");
    lv_obj_set_pos(spW[i], 0, 50);
    spPv[i] = miniStat(cd, 0, 116, "PANEL");
    spA[i] = miniStat(cd, 74, 116, "CHARGE");
    spToday[i] = miniStat(cd, 148, 116, "TODAY");
    spMax[i] = miniStat(cd, 244, 116, "PEAK");
    spErr[i] = lbl(cd, &lv_font_montserrat_12, col::ALARM, "");
    lv_obj_align(spErr[i], LV_ALIGN_TOP_RIGHT, 0, 64);
  }

  lv_obj_t *b = card(p, 12, 220, 264, 204, "BATTERY DETAIL", col::CHARGE);
  static const char *caps[] = {"Voltage", "Current", "Charge", "Solar in", "Charger in", "Loads", "Source"};
  for (int i = 0; i < 7; i++) {
    lv_obj_t *cl = lbl(b, &lv_font_montserrat_14, col::MUTED, caps[i]);
    lv_obj_set_pos(cl, 0, 24 + i * 21);
    bpRows[i] = lbl(b, &lv_font_montserrat_14, col::TEXT, "--");
    lv_obj_align(bpRows[i], LV_ALIGN_TOP_RIGHT, 0, 24 + i * 21);
  }
  lv_obj_t *btn = button(b, 96, 24, "Set 100%", col::TILE, socBtnCb);
  lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, 0, -6);
  lv_obj_t *lb = lv_obj_get_child(btn, 0);
  lv_obj_set_style_text_font(lb, &lv_font_montserrat_12, 0);

  lv_obj_t *cc = card(p, 288, 220, 412, 204, "LAST 30 MIN  -  solar W / load W", col::SOLAR);
  chart = lv_chart_create(cc);
  lv_obj_set_size(chart, 384, 160);
  lv_obj_set_pos(chart, 0, 20);
  lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(chart, 180);  // one point / 10 s
  lv_chart_set_div_line_count(chart, 4, 0);
  lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 800);
  lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(chart, 0, 0);
  lv_obj_set_style_line_color(chart, c(col::BORDER), LV_PART_MAIN);
  lv_obj_set_style_size(chart, 0, LV_PART_INDICATOR);  // no point markers
  lv_obj_set_style_line_width(chart, 3, LV_PART_ITEMS);
  serSolar = lv_chart_add_series(chart, c(col::SOLAR), LV_CHART_AXIS_PRIMARY_Y);
  serLoad = lv_chart_add_series(chart, c(col::LOAD), LV_CHART_AXIS_PRIMARY_Y);
  lv_chart_set_all_value(chart, serSolar, LV_CHART_POINT_NONE);
  lv_chart_set_all_value(chart, serLoad, LV_CHART_POINT_NONE);
}

void updatePower() {
  const cv::State &s = app::state;
  // history sampling runs regardless of visibility
  static uint32_t lastSample = 0;
  if (app::linkUp && millis() - lastSample >= 10000) {
    lastSample = millis();
    int sw = (int)s.solarW();
    int lw = (int)(s.bat.loadA * (s.bat.v > 0 ? s.bat.v : 12.8f));
    lv_chart_set_next_value(chart, serSolar, sw);
    lv_chart_set_next_value(chart, serLoad, lw);
    // Auto-scale: keep the biggest value in view, in 200 W steps.
    static int ymax = 800;
    int peak = 200;
    for (int i = 0; i < 180; i++) {
      peak = max<int>(peak, serSolar->y_points[i] == LV_CHART_POINT_NONE ? 0 : serSolar->y_points[i]);
      peak = max<int>(peak, serLoad->y_points[i] == LV_CHART_POINT_NONE ? 0 : serLoad->y_points[i]);
    }
    int want = ((peak * 11 / 10) / 200 + 1) * 200;
    if (want != ymax) {
      ymax = want;
      lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, ymax);
    }
  }
  if (lv_obj_has_flag(lv_obj_get_parent(lv_obj_get_parent(chart)), LV_OBJ_FLAG_HIDDEN)) return;

  if (seenMetaPower != app::metaVer) {
    seenMetaPower = app::metaVer;
    for (int i = 0; i < cv::NUM_SOLAR; i++) lv_label_set_text(spName[i], app::meta.solarName[i]);
  }
  for (int i = 0; i < cv::NUM_SOLAR; i++) {
    const cv::Solar &m = s.solar[i];
    if (!m.online) {
      setChip(spChip[i], "Offline", col::DIM);
      setText(spW[i], "-- W");
      setText(spPv[i], "--");
      setText(spA[i], "--");
      setText(spToday[i], "--");
      setText(spMax[i], "--");
      setText(spErr[i], "");
      continue;
    }
    uint32_t cc = m.cs == 3 ? col::SOLAR : m.cs == 4 ? col::ACCENT : m.cs == 5 ? col::CHARGE : col::MUTED;
    setChip(spChip[i], cv::chargeStateName(m.cs), m.err ? col::ALARM : cc);
    setText(spW[i], "%.0f W", m.pvW);
    setText(spPv[i], "%.1f V", m.pvV);
    setText(spA[i], "%.1f A", m.batA);
    setText(spToday[i], "%.2f kWh", m.yieldTodayKwh);
    setText(spMax[i], "%u W", m.maxWToday);
    if (m.err) setText(spErr[i], "Error code %u", m.err);
    else setText(spErr[i], "");
  }
  char ttg[24];
  fmtDuration(ttg, sizeof(ttg), s.bat.ttgMin);
  if (s.bat.online) {
    setText(bpRows[0], "%.2f V", s.bat.v);
    setText(bpRows[1], "%+.1f A", s.bat.a);
    setText(bpRows[2], "%.0f%%  (%s)", s.bat.soc, ttg);
  } else {
    for (int i = 0; i < 3; i++) setText(bpRows[i], "--");
  }
  setText(bpRows[3], "%.1f A", s.bat.solarA);
  setText(bpRows[4], "%.1f A", s.bat.chargerA);
  setText(bpRows[5], "%.1f A", s.bat.loadA);
  setText(bpRows[6], "%s", s.bat.source == cv::BAT_SRC_BMPRO ? "BMPRO" : s.bat.source == cv::BAT_SRC_SHUNT ? "Shunt" : "None");
}

// ================================================================ WATER
static lv_obj_t *wBar[cv::NUM_TANKS], *wPct[cv::NUM_TANKS], *wName[cv::NUM_TANKS];
static lv_obj_t *pName[cv::NUM_PUMPS], *pBtn[cv::NUM_PUMPS], *pStatus[cv::NUM_PUMPS];
static uint32_t seenMetaWater = UINT32_MAX;

static void pumpBtnCb(lv_event_t *e) { togglePump((int)(intptr_t)lv_event_get_user_data(e)); }

void buildWater(lv_obj_t *p) {
  for (int i = 0; i < cv::NUM_TANKS; i++) {
    lv_obj_t *cd = card(p, 12 + i * 232, 12, 220, 252, "TANK", col::WATER);
    wName[i] = lbl(cd, &lv_font_montserrat_20, col::TEXT, app::meta.tankName[i]);
    lv_obj_set_pos(wName[i], 0, 20);
    wBar[i] = vbar(cd, 70, 170, col::WATER);
    lv_obj_set_pos(wBar[i], 0, 54);
    wPct[i] = lbl(cd, &lv_font_montserrat_40, col::TEXT, "--");
    lv_obj_align(wPct[i], LV_ALIGN_BOTTOM_RIGHT, 0, -6);
  }
  for (int i = 0; i < cv::NUM_PUMPS; i++) {
    lv_obj_t *cd = card(p, 12 + i * 350, 276, 338, 148, "WATER PUMP", col::WATER);
    pName[i] = lbl(cd, &lv_font_montserrat_20, col::TEXT, app::meta.pumpName[i]);
    lv_obj_set_pos(pName[i], 0, 22);
    pStatus[i] = lbl(cd, &lv_font_montserrat_14, col::MUTED, "--");
    lv_obj_set_width(pStatus[i], 170);
    lv_label_set_long_mode(pStatus[i], LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(pStatus[i], 0, 54);
    pBtn[i] = button(cd, 120, 84, LV_SYMBOL_POWER, col::TILE, pumpBtnCb, (void *)(intptr_t)i);
    lv_obj_set_style_text_font(lv_obj_get_child(pBtn[i], 0), &lv_font_montserrat_28, 0);
    lv_obj_align(pBtn[i], LV_ALIGN_RIGHT_MID, 0, 10);
  }
}

void updateWater() {
  const cv::State &s = app::state;
  if (seenMetaWater != app::metaVer) {
    seenMetaWater = app::metaVer;
    for (int i = 0; i < cv::NUM_TANKS; i++) lv_label_set_text(wName[i], app::meta.tankName[i]);
    for (int i = 0; i < cv::NUM_PUMPS; i++) lv_label_set_text(pName[i], app::meta.pumpName[i]);
  }
  for (int i = 0; i < cv::NUM_TANKS; i++) {
    if (s.tank[i].online) {
      lv_bar_set_value(wBar[i], (int)s.tank[i].pct, LV_ANIM_ON);
      setBarColor(wBar[i], tankColor(i, s.tank[i].pct));
      setText(wPct[i], "%d%%", (int)(s.tank[i].pct + 0.5f));
    } else {
      lv_bar_set_value(wBar[i], 0, LV_ANIM_OFF);
      setText(wPct[i], "--");
    }
  }
  for (int i = 0; i < cv::NUM_PUMPS; i++) {
    const cv::Pump &pm = s.pump[i];
    uint32_t bg = pm.lockout ? col::WARN : pm.on ? col::WATER : col::TILE;
    lv_obj_set_style_bg_color(pBtn[i], c(bg), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(pBtn[i], 0), c(pm.on ? col::BG : col::MUTED), 0);
    if (pm.lockout) setText(pStatus[i], "Locked out - tank empty. Refill to re-enable.");
    else if (pm.on) setText(pStatus[i], "Running %um %02us%s", pm.runSec / 60, pm.runSec % 60,
                            pm.sensed ? "" : "\n(no feedback)");
    else setText(pStatus[i], "Off");
  }
}

// ================================================================ CLIMATE
static lv_obj_t *cName[cv::NUM_TEMPS], *cVal[cv::NUM_TEMPS], *cMinMax[cv::NUM_TEMPS];
static lv_obj_t *acGrid, *acStatus, *acOff;
static lv_obj_t *acBtns[cv::MAX_AC_PRESETS];
static lv_obj_t *invBtn, *invLbl, *invAc;
static uint32_t seenMetaClimate = UINT32_MAX;

static void acPresetCb(lv_event_t *e) { app::cmdAircon((int)(intptr_t)lv_event_get_user_data(e)); }
static void acOffCb(lv_event_t *) { app::cmdAircon(-1); }
static void invBtnCb(lv_event_t *) { toggleInverter(); }

static void rebuildPresets() {
  lv_obj_clean(acGrid);
  int n = app::meta.acPresetCount;
  for (int i = 0; i < cv::MAX_AC_PRESETS; i++) acBtns[i] = nullptr;
  if (n == 0) {
    lv_obj_t *h = lbl(acGrid, &lv_font_montserrat_14, col::MUTED,
                      "No presets yet.\nGo to Settings > Aircon to learn codes\nfrom your Houghton remote.");
    lv_obj_set_pos(h, 0, 10);
    return;
  }
  for (int i = 0; i < n; i++) {
    acBtns[i] = button(acGrid, 96, 52, app::meta.acPresetName[i], col::TILE, acPresetCb, (void *)(intptr_t)i);
    lv_obj_set_pos(acBtns[i], (i % 4) * 104, (i / 4) * 60);
    lv_obj_t *l = lv_obj_get_child(acBtns[i], 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_set_width(l, 88);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  }
}

void buildClimate(lv_obj_t *p) {
  for (int i = 0; i < cv::NUM_TEMPS; i++) {
    lv_obj_t *cd = card(p, 12 + i * 232, 12, 220, 140, "TEMPERATURE", col::CLIMATE);
    cName[i] = lbl(cd, &lv_font_montserrat_16, col::MUTED, app::meta.tempName[i]);
    lv_obj_set_pos(cName[i], 0, 20);
    cVal[i] = lbl(cd, &lv_font_montserrat_48, col::TEXT, "--");
    lv_obj_set_pos(cVal[i], 0, 42);
    cMinMax[i] = lbl(cd, &lv_font_montserrat_12, col::MUTED, "");
    lv_obj_align(cMinMax[i], LV_ALIGN_BOTTOM_LEFT, 0, 0);
  }

  lv_obj_t *ac = card(p, 12, 164, 448, 260, "AIR CONDITIONER", col::CLIMATE);
  acStatus = lbl(ac, &lv_font_montserrat_20, col::TEXT, "Off");
  lv_obj_set_pos(acStatus, 0, 20);
  acOff = button(ac, 96, 36, LV_SYMBOL_POWER " Off", col::TILE, acOffCb);
  lv_obj_align(acOff, LV_ALIGN_TOP_RIGHT, 0, 14);
  acGrid = panel(ac, 0, 62, 420, 172);
  rebuildPresets();

  lv_obj_t *iv = card(p, 472, 164, 228, 260, "INVERTER 3000W", col::SOLAR);
  invBtn = lv_btn_create(iv);
  lv_obj_set_size(invBtn, 120, 120);
  lv_obj_align(invBtn, LV_ALIGN_TOP_MID, 0, 26);
  lv_obj_set_style_radius(invBtn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_shadow_width(invBtn, 0, 0);
  lv_obj_add_event_cb(invBtn, invBtnCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *ic = lbl(invBtn, &lv_font_montserrat_48, col::MUTED, LV_SYMBOL_POWER);
  lv_obj_center(ic);
  invLbl = lbl(iv, &lv_font_montserrat_20, col::TEXT, "Off");
  lv_obj_align(invLbl, LV_ALIGN_BOTTOM_MID, 0, -24);
  invAc = lbl(iv, &lv_font_montserrat_12, col::MUTED, "");
  lv_obj_align(invAc, LV_ALIGN_BOTTOM_MID, 0, 0);
}

void updateClimate() {
  const cv::State &s = app::state;
  if (seenMetaClimate != app::metaVer) {
    seenMetaClimate = app::metaVer;
    for (int i = 0; i < cv::NUM_TEMPS; i++) lv_label_set_text(cName[i], app::meta.tempName[i]);
    rebuildPresets();
  }
  for (int i = 0; i < cv::NUM_TEMPS; i++) {
    const cv::Temp &t = s.temp[i];
    if (t.online) {
      setText(cVal[i], "%.1f\xC2\xB0", t.c);
      lv_obj_set_style_text_color(cVal[i], c(tempColor(i, t.c)), 0);
      setText(cMinMax[i], LV_SYMBOL_DOWN " %.1f\xC2\xB0   " LV_SYMBOL_UP " %.1f\xC2\xB0  today", t.minC, t.maxC);
    } else {
      setText(cVal[i], "--");
      lv_obj_set_style_text_color(cVal[i], c(col::DIM), 0);
      setText(cMinMax[i], "sensor offline");
    }
  }
  for (int i = 0; i < app::meta.acPresetCount; i++) {
    if (!acBtns[i]) continue;
    bool on = s.ac.on && s.ac.preset == i;
    lv_obj_set_style_bg_color(acBtns[i], c(on ? col::CLIMATE : col::TILE), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(acBtns[i], 0), c(on ? col::BG : col::TEXT), 0);
  }
  if (s.ac.on && s.ac.preset >= 0 && s.ac.preset < app::meta.acPresetCount)
    setText(acStatus, "On - %s", app::meta.acPresetName[s.ac.preset]);
  else
    setText(acStatus, "Off");

  lv_obj_set_style_bg_color(invBtn, c(s.inv.on ? col::SOLAR : col::TILE), 0);
  lv_obj_set_style_text_color(lv_obj_get_child(invBtn, 0), c(s.inv.on ? col::BG : col::MUTED), 0);
  setText(invLbl, "%s", s.inv.on ? "On" : "Off");
  setText(invAc, "%s", s.inv.acPresent ? "240 V output present" : "No 240 V output");
}

}  // namespace ui
