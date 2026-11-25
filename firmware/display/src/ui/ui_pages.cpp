#include <math.h>
// POWER, WATER and CLIMATE pages.
#include "../app.h"
#include "ui.h"

namespace ui {

// ================================================================ POWER
static lv_obj_t *spName[cv::NUM_SOLAR], *spChip[cv::NUM_SOLAR], *spW[cv::NUM_SOLAR];
static lv_obj_t *spPv[cv::NUM_SOLAR], *spA[cv::NUM_SOLAR], *spToday[cv::NUM_SOLAR], *spMax[cv::NUM_SOLAR], *spErr[cv::NUM_SOLAR];
static lv_obj_t *spPvCap[cv::NUM_SOLAR];
static lv_obj_t *bpRows[7];
static lv_obj_t *chart;
static lv_chart_series_t *serSolar, *serLoad;
static uint32_t seenMetaPower = UINT32_MAX;

static lv_obj_t *miniStat(lv_obj_t *parent, int x, int y, const char *cap, lv_obj_t **capOut = nullptr) {
  lv_obj_t *l = lbl(parent, LARGE ? &lv_font_montserrat_14 : &lv_font_montserrat_12, col::MUTED, cap);
  lv_obj_set_pos(l, x, y);
  if (capOut) *capOut = l;
  lv_obj_t *v = lbl(parent, LARGE ? &lv_font_montserrat_20 : &lv_font_montserrat_16, col::TEXT, "--");
  lv_obj_set_pos(v, x, y + (LARGE ? 18 : 16));
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

// Top: one card per solar controller. Bottom: battery detail | 30-minute chart.
static constexpr int PW_TOP_H = (INNER_H - GAP) * 46 / 100, PW_BOT_H = INNER_H - GAP - PW_TOP_H;
static constexpr int PW_BOT_Y = GAP + PW_TOP_H + GAP;
static constexpr int PW_BAT_W = splitW(LARGE ? 37 : 39), PW_CHART_W = INNER_W - GAP - PW_BAT_W;

void buildPower(lv_obj_t *p) {
  constexpr int sw = colW(cv::NUM_SOLAR), statStep = innerOf(sw) / 4;
  for (int i = 0; i < cv::NUM_SOLAR; i++) {
    lv_obj_t *cd = card(p, colX(i, cv::NUM_SOLAR), GAP, sw, PW_TOP_H, i == 0 ? "SOLAR CONTROLLER 1" : "SOLAR CONTROLLER 2",
                        col::SOLAR);
    spName[i] = lbl(cd, &lv_font_montserrat_20, col::TEXT, app::meta.solarName[i]);
    lv_obj_set_pos(spName[i], 0, 22);
    spChip[i] = chip(cd);
    lv_obj_align(spChip[i], LV_ALIGN_TOP_RIGHT, 0, 24);
    spW[i] = lbl(cd, LARGE ? &lv_font_montserrat_48 : &lv_font_montserrat_40, col::SOLAR, "-- W");
    lv_obj_set_pos(spW[i], 0, 50);
    constexpr int statY = innerOf(PW_TOP_H) - (LARGE ? 42 : 38);  // stats row along the bottom
    spPv[i] = miniStat(cd, 0, statY, "PANEL", &spPvCap[i]);
    spA[i] = miniStat(cd, statStep - 6, statY, "CHARGE");
    spToday[i] = miniStat(cd, 2 * statStep - 12, statY, "TODAY");
    spMax[i] = miniStat(cd, 3 * statStep + 12, statY, "PEAK");
    spErr[i] = lbl(cd, &lv_font_montserrat_12, col::ALARM, "");
    lv_obj_align(spErr[i], LV_ALIGN_TOP_RIGHT, 0, 64);
  }

  lv_obj_t *b = card(p, GAP, PW_BOT_Y, PW_BAT_W, PW_BOT_H, "BATTERY DETAIL", col::CHARGE);
  static const char *caps[] = {"Voltage", "Current", "Charge", "Victron solar", "BMPRO (roof)", "Loads", "Source"};
  constexpr int rowStep = (innerOf(PW_BOT_H) - 24) / 7;
  const lv_font_t *rf = rowStep >= 30 ? &lv_font_montserrat_16 : &lv_font_montserrat_14;
  for (int i = 0; i < 7; i++) {
    lv_obj_t *cl = lbl(b, rf, col::MUTED, caps[i]);
    lv_obj_set_pos(cl, 0, 24 + i * rowStep);
    bpRows[i] = lbl(b, rf, col::TEXT, "--");
    lv_obj_align(bpRows[i], LV_ALIGN_TOP_RIGHT, 0, 24 + i * rowStep);
  }
  lv_obj_t *btn = button(b, 96, 24, "Set 100%", col::TILE, socBtnCb);
  lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, 0, -6);
  lv_obj_t *lb = lv_obj_get_child(btn, 0);
  lv_obj_set_style_text_font(lb, &lv_font_montserrat_12, 0);

  lv_obj_t *cc = card(p, GAP + PW_BAT_W + GAP, PW_BOT_Y, PW_CHART_W, PW_BOT_H, "LAST 30 MIN  -  solar W / load W",
                     col::SOLAR);
  chart = lv_chart_create(cc);
  lv_obj_set_size(chart, innerOf(PW_CHART_W), innerOf(PW_BOT_H) - 20);
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
    // Bluetooth Instant Readout has no PV voltage: show battery voltage instead.
    if (m.pvV >= 0) {
      setText(spPvCap[i], "PANEL");
      setText(spPv[i], "%.1f V", m.pvV);
    } else {
      setText(spPvCap[i], "BATTERY");
      setText(spPv[i], "%.2f V", m.batV);
    }
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
  // The BMPRO's own charger input: the roof panels (and the car's DC-DC while
  // towing). Mains is only added while the hub detects mains bulk charging.
  if (s.bat.acOn) setText(bpRows[4], "%.1f A  + mains %.0f A", s.bat.chargerA, s.bat.acA);
  else setText(bpRows[4], "%.1f A", s.bat.chargerA);
  setText(bpRows[5], "%.1f A", s.bat.loadA);
  setText(bpRows[6], "%s", s.bat.source == cv::BAT_SRC_BMPRO ? "BMPRO" : s.bat.source == cv::BAT_SRC_SHUNT ? "Shunt" : "None");
}

// ================================================================ WATER
static lv_obj_t *wBar[cv::NUM_TANKS], *wPct[cv::NUM_TANKS], *wName[cv::NUM_TANKS];
static lv_obj_t *pName[cv::NUM_PUMPS], *pBtn[cv::NUM_PUMPS], *pStatus[cv::NUM_PUMPS];
static uint32_t seenMetaWater = UINT32_MAX;

static void pumpBtnCb(lv_event_t *e) { togglePump((int)(intptr_t)lv_event_get_user_data(e)); }

void buildWater(lv_obj_t *p) {
  // Top: one card per tank. Bottom: one card per pump.
  constexpr int tankH = (INNER_H - GAP) * 58 / 100, pumpH = INNER_H - GAP - tankH, pumpY = GAP + tankH + GAP;
  for (int i = 0; i < cv::NUM_TANKS; i++) {
    lv_obj_t *cd = card(p, colX(i, cv::NUM_TANKS), GAP, colW(cv::NUM_TANKS), tankH, "TANK", col::WATER);
    wName[i] = lbl(cd, LARGE ? &lv_font_montserrat_24 : &lv_font_montserrat_20, col::TEXT, app::meta.tankName[i]);
    lv_obj_set_pos(wName[i], 0, 20);
    wBar[i] = vbar(cd, LARGE ? 84 : 70, innerOf(tankH) - 54, col::WATER);
    lv_obj_set_pos(wBar[i], 0, 54);
    wPct[i] = lbl(cd, LARGE ? &lv_font_montserrat_48 : &lv_font_montserrat_40, col::TEXT, "--");
    lv_obj_align(wPct[i], LV_ALIGN_BOTTOM_RIGHT, 0, -6);
  }
  constexpr int pw = colW(cv::NUM_PUMPS), btnW = LARGE ? 150 : 120;
  for (int i = 0; i < cv::NUM_PUMPS; i++) {
    lv_obj_t *cd = card(p, colX(i, cv::NUM_PUMPS), pumpY, pw, pumpH, "WATER PUMP", col::WATER);
    pName[i] = lbl(cd, LARGE ? &lv_font_montserrat_24 : &lv_font_montserrat_20, col::TEXT, app::meta.pumpName[i]);
    lv_obj_set_pos(pName[i], 0, 22);
    pStatus[i] = lbl(cd, LARGE ? &lv_font_montserrat_16 : &lv_font_montserrat_14, col::MUTED, "--");
    lv_obj_set_width(pStatus[i], innerOf(pw) - btnW - 16);
    lv_label_set_long_mode(pStatus[i], LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(pStatus[i], 0, LARGE ? 60 : 54);
    constexpr int btnH = innerOf(pumpH) - 20 < (LARGE ? 110 : 84) ? innerOf(pumpH) - 20 : (LARGE ? 110 : 84);
    pBtn[i] = button(cd, btnW, btnH, LV_SYMBOL_POWER, col::TILE, pumpBtnCb, (void *)(intptr_t)i);
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
static int acGridW, acGridH;
static lv_obj_t *wxCard, *wxBox, *wxCaption;  // forecast card (when aircon + inverter are off)
static int wxBoxW, wxBoxH;
static lv_obj_t *heaterCard;
static int heaterShown = -1;  // forecast card shares its row with the heater card once one is set up
static constexpr int HTR_W = splitW(LARGE ? 38 : 42);
static uint32_t wxSeen = UINT32_MAX;
static void buildAircon(lv_obj_t *p, int x, int y, int w, int h);
static void buildInverter(lv_obj_t *p, int x, int y, int w, int h);
static void updateAircon();
static void updateInverter();
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
  // 4 x 2 grid filling the preset area
  const int gw = acGridW, gh = acGridH;
  const int bw = (gw - 3 * 8) / 4, bh = (gh - 8) / 2 < 64 ? (gh - 8) / 2 : 64;
  for (int i = 0; i < n; i++) {
    acBtns[i] = button(acGrid, bw, bh, app::meta.acPresetName[i], col::TILE, acPresetCb, (void *)(intptr_t)i);
    lv_obj_set_pos(acBtns[i], (i % 4) * (bw + 8), (i / 4) * (bh + 8));
    lv_obj_t *l = lv_obj_get_child(acBtns[i], 0);
    lv_obj_set_style_text_font(l, LARGE ? &lv_font_montserrat_16 : &lv_font_montserrat_14, 0);
    lv_obj_set_width(l, bw - 8);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  }
}

void buildClimate(lv_obj_t *p) {
  // Top: one card per temperature. Bottom: aircon presets | inverter, or the
  // weather forecast while those controls are switched off (see ui.h).
  constexpr int tempH = LARGE ? 168 : 140, botY = GAP + tempH + GAP, botH = INNER_H - GAP - tempH;
  constexpr int acW = !HAS_AIRCON ? 0 : HAS_INVERTER ? splitW(66) : INNER_W;
  constexpr int invW = !HAS_INVERTER ? 0 : HAS_AIRCON ? INNER_W - GAP - acW : INNER_W;
  constexpr int invX = HAS_AIRCON ? GAP + acW + GAP : GAP;
  for (int i = 0; i < cv::NUM_TEMPS; i++) {
    lv_obj_t *cd = card(p, colX(i, cv::NUM_TEMPS), GAP, colW(cv::NUM_TEMPS), tempH, "TEMPERATURE", col::CLIMATE);
    cName[i] = lbl(cd, LARGE ? &lv_font_montserrat_20 : &lv_font_montserrat_16, col::MUTED, app::meta.tempName[i]);
    lv_obj_set_pos(cName[i], 0, 20);
    cVal[i] = lbl(cd, &lv_font_montserrat_48, col::TEXT, "--");
    lv_obj_set_pos(cVal[i], 0, 42);
    cMinMax[i] = lbl(cd, LARGE ? &lv_font_montserrat_14 : &lv_font_montserrat_12, col::MUTED, "");
    lv_obj_align(cMinMax[i], LV_ALIGN_BOTTOM_LEFT, 0, 0);
  }

  if (!HAS_AIRCON && !HAS_INVERTER) {
    // Diesel heater on the left (only once one is set up), forecast beside it.
    heaterCard = buildHeater(p, GAP, botY, HTR_W, botH);
    lv_obj_add_flag(heaterCard, LV_OBJ_FLAG_HIDDEN);
    wxCard = card(p, GAP, botY, INNER_W, botH, "FORECAST", col::WATER);
    wxCaption = lv_obj_get_child(wxCard, 1);  // card(): child 0 = dot, 1 = caption
    wxBox = panel(wxCard, 0, 0, innerOf(INNER_W), innerOf(botH));
    wxBoxW = innerOf(INNER_W);
    wxBoxH = innerOf(botH);
  }
  if (HAS_AIRCON) buildAircon(p, GAP, botY, acW, botH);
  if (HAS_INVERTER) buildInverter(p, invX, botY, invW, botH);
}

static void buildAircon(lv_obj_t *p, int x, int y, int acW, int botH) {
  lv_obj_t *ac = card(p, x, y, acW, botH, "AIR CONDITIONER", col::CLIMATE);
  acStatus = lbl(ac, LARGE ? &lv_font_montserrat_24 : &lv_font_montserrat_20, col::TEXT, "Off");
  lv_obj_set_pos(acStatus, 0, 20);
  acOff = button(ac, LARGE ? 110 : 96, LARGE ? 42 : 36, LV_SYMBOL_POWER " Off", col::TILE, acOffCb);
  lv_obj_align(acOff, LV_ALIGN_TOP_RIGHT, 0, 14);
  acGridW = innerOf(acW);
  acGridH = innerOf(botH) - (LARGE ? 70 : 62);
  acGrid = panel(ac, 0, LARGE ? 70 : 62, acGridW, acGridH);
  rebuildPresets();
}

static void buildInverter(lv_obj_t *p, int x, int y, int invW, int botH) {
  lv_obj_t *iv = card(p, x, y, invW, botH, "INVERTER 3000W", col::SOLAR);
  constexpr int invD = LARGE ? 160 : 120;
  invBtn = lv_btn_create(iv);
  lv_obj_set_size(invBtn, invD, invD);
  lv_obj_align(invBtn, LV_ALIGN_TOP_MID, 0, LARGE ? 32 : 26);
  lv_obj_set_style_radius(invBtn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_shadow_width(invBtn, 0, 0);
  lv_obj_add_event_cb(invBtn, invBtnCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *ic = lbl(invBtn, &lv_font_montserrat_48, col::MUTED, LV_SYMBOL_POWER);
  lv_obj_center(ic);
  invLbl = lbl(iv, LARGE ? &lv_font_montserrat_24 : &lv_font_montserrat_20, col::TEXT, "Off");
  lv_obj_align(invLbl, LV_ALIGN_BOTTOM_MID, 0, -24);
  invAc = lbl(iv, LARGE ? &lv_font_montserrat_14 : &lv_font_montserrat_12, col::MUTED, "");
  lv_obj_align(invAc, LV_ALIGN_BOTTOM_MID, 0, 0);
}

void updateClimate() {
  const cv::State &s = app::state;
  if (seenMetaClimate != app::metaVer) {
    seenMetaClimate = app::metaVer;
    for (int i = 0; i < cv::NUM_TEMPS; i++) lv_label_set_text(cName[i], app::meta.tempName[i]);
    if (HAS_AIRCON) rebuildPresets();
  }
  for (int i = 0; i < cv::NUM_TEMPS; i++) {
    const cv::Temp &t = s.temp[i];
    if (t.online) {
      setText(cVal[i], "%d\xC2\xB0", (int)lroundf(t.c));
      lv_obj_set_style_text_color(cVal[i], c(tempColor(i, t.c)), 0);
      if (t.batt >= 0 && t.batt <= 20)  // Bluetooth sensor needs a new battery
        setText(cMinMax[i], LV_SYMBOL_DOWN " %d\xC2\xB0  " LV_SYMBOL_UP " %d\xC2\xB0  " LV_SYMBOL_BATTERY_1 " %d%%",
                (int)lroundf(t.minC), (int)lroundf(t.maxC), t.batt);
      else
        setText(cMinMax[i], LV_SYMBOL_DOWN " %d\xC2\xB0   " LV_SYMBOL_UP " %d\xC2\xB0  today", (int)lroundf(t.minC), (int)lroundf(t.maxC));
    } else {
      setText(cVal[i], "--");
      lv_obj_set_style_text_color(cVal[i], c(col::DIM), 0);
      setText(cMinMax[i], "sensor offline");
    }
  }
  if (heaterCard) {
    int shown = s.htr.link != cv::HTR_LINK_NONE;
    if (shown != heaterShown) {  // heater set up / removed: re-flow the bottom row
      heaterShown = shown;
      int fx = shown ? GAP + HTR_W + GAP : GAP, fw = shown ? INNER_W - HTR_W - GAP : INNER_W;
      if (shown) lv_obj_clear_flag(heaterCard, LV_OBJ_FLAG_HIDDEN);
      else lv_obj_add_flag(heaterCard, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_pos(wxCard, fx, lv_obj_get_y(wxCard));
      lv_obj_set_width(wxCard, fw);
      wxBoxW = innerOf(fw);
      lv_obj_set_width(wxBox, wxBoxW);
      wxSeen = UINT32_MAX;  // rebuild the forecast at the new width
    }
    if (shown) updateHeater();
  }
  if (wxBox) {  // forecast card: rebuild on a new forecast, or once a minute for "updated N min ago"
    const cv::Weather &w = s.wx;
    uint32_t sig = w.valid ? (w.updated ^ (s.ts / 60)) + 1 : 0;
    if (sig != wxSeen) {
      wxSeen = sig;
      if (w.place[0]) setText(wxCaption, "FORECAST  -  %s", w.place);
      else setText(wxCaption, "FORECAST");
      buildForecast(wxBox, wxBoxW, wxBoxH);
    }
  }
  if (HAS_AIRCON) updateAircon();
  if (HAS_INVERTER) updateInverter();
}

static void updateAircon() {
  const cv::State &s = app::state;
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
}

static void updateInverter() {
  const cv::State &s = app::state;
  lv_obj_set_style_bg_color(invBtn, c(s.inv.on ? col::SOLAR : col::TILE), 0);
  lv_obj_set_style_text_color(lv_obj_get_child(invBtn, 0), c(s.inv.on ? col::BG : col::MUTED), 0);
  setText(invLbl, "%s", s.inv.on ? "On" : "Off");
  setText(invAc, "%s", s.inv.acPresent ? "240 V output present" : "No 240 V output");
}

}  // namespace ui
