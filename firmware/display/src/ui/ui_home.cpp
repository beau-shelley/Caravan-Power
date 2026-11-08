// HOME - everything important on one screen.
//  [ Battery ring  ] [ Energy flow: solar / charger / loads + per-MPPT  ]
//  [ Temperatures  ] [ Water tanks      ] [ Pump1 Pump2 Inverter Aircon ]
#include "../app.h"
#include "ui.h"

namespace ui {

static lv_obj_t *arcSoc, *lSoc, *lVolt, *lAmps, *lTtg, *lSrc;
static lv_obj_t *lSolarW, *lSolarA, *lChgA, *lChgSub, *lLoadA, *lLoadW;
static lv_obj_t *mpName[cv::NUM_SOLAR], *mpChip[cv::NUM_SOLAR], *mpBar[cv::NUM_SOLAR], *mpW[cv::NUM_SOLAR];
static lv_obj_t *tName[cv::NUM_TEMPS], *tVal[cv::NUM_TEMPS];
static lv_obj_t *tkBar[cv::NUM_TANKS], *tkPct[cv::NUM_TANKS], *tkName[cv::NUM_TANKS];
static lv_obj_t *tilePump[cv::NUM_PUMPS], *tileInv, *tileAc;
static uint32_t seenMeta = UINT32_MAX;

static void pumpCb(lv_event_t *e) { togglePump((int)(intptr_t)lv_event_get_user_data(e)); }
static void invCb(lv_event_t *) { toggleInverter(); }
static void acCb(lv_event_t *) { toggleAircon(); }
static void goPower(lv_event_t *) { showPage(PAGE_POWER); }
static void goWater(lv_event_t *) { showPage(PAGE_WATER); }
static void goClimate(lv_event_t *) { showPage(PAGE_CLIMATE); }

static lv_obj_t *stat(lv_obj_t *parent, int x, const char *caption, uint32_t color, lv_obj_t **big, lv_obj_t **sub) {
  lv_obj_t *cap = lbl(parent, &lv_font_montserrat_12, col::MUTED, caption);
  lv_obj_set_pos(cap, x, 24);
  *big = lbl(parent, &lv_font_montserrat_28, color, "--");
  lv_obj_set_pos(*big, x, 42);
  *sub = lbl(parent, &lv_font_montserrat_14, col::MUTED, "");
  lv_obj_set_pos(*sub, x, 76);
  return cap;
}

void buildHome(lv_obj_t *p) {
  // ---------------- battery
  lv_obj_t *b = card(p, 12, 12, 264, 228, "BATTERY", col::CHARGE);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(b, goPower, LV_EVENT_CLICKED, nullptr);
  arcSoc = lv_arc_create(b);
  lv_obj_set_size(arcSoc, 150, 150);
  lv_obj_align(arcSoc, LV_ALIGN_TOP_MID, 0, 14);
  lv_arc_set_rotation(arcSoc, 135);
  lv_arc_set_bg_angles(arcSoc, 0, 270);
  lv_arc_set_range(arcSoc, 0, 100);
  lv_arc_set_value(arcSoc, 0);
  lv_obj_remove_style(arcSoc, nullptr, LV_PART_KNOB);
  lv_obj_clear_flag(arcSoc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(arcSoc, 14, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arcSoc, 14, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arcSoc, c(col::TILE), LV_PART_MAIN);
  lv_obj_set_style_arc_color(arcSoc, c(col::CHARGE), LV_PART_INDICATOR);
  lSoc = lbl(b, &lv_font_montserrat_40, col::TEXT, "--%");
  lv_obj_align_to(lSoc, arcSoc, LV_ALIGN_CENTER, 0, -8);
  lVolt = lbl(b, &lv_font_montserrat_16, col::MUTED, "-- V");
  lv_obj_align_to(lVolt, arcSoc, LV_ALIGN_CENTER, 0, 28);
  lAmps = lbl(b, &lv_font_montserrat_24, col::TEXT, "-- A");
  lv_obj_align(lAmps, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lTtg = lbl(b, &lv_font_montserrat_12, col::MUTED, "");
  lv_obj_set_style_text_align(lTtg, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(lTtg, LV_ALIGN_BOTTOM_RIGHT, 0, -2);
  lSrc = lbl(b, &lv_font_montserrat_12, col::DIM, "");
  lv_obj_align(lSrc, LV_ALIGN_TOP_RIGHT, 0, 0);

  // ---------------- energy flow
  lv_obj_t *f = card(p, 288, 12, 412, 228, "ENERGY FLOW", col::SOLAR);
  lv_obj_add_flag(f, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(f, goPower, LV_EVENT_CLICKED, nullptr);
  stat(f, 0, LV_SYMBOL_IMAGE " SOLAR", col::SOLAR, &lSolarW, &lSolarA);
  stat(f, 136, LV_SYMBOL_CHARGE " CHARGING", col::CHARGE, &lChgA, &lChgSub);
  stat(f, 262, LV_SYMBOL_POWER " LOADS", col::LOAD, &lLoadA, &lLoadW);
  for (int i = 0; i < cv::NUM_SOLAR; i++) {
    int y = 112 + i * 44;
    mpName[i] = lbl(f, &lv_font_montserrat_16, col::TEXT, app::meta.solarName[i]);
    lv_obj_set_pos(mpName[i], 0, y);
    mpChip[i] = chip(f);
    lv_obj_set_pos(mpChip[i], 0, y + 22);
    mpBar[i] = vbar(f, 170, 10, col::SOLAR);
    lv_obj_set_pos(mpBar[i], 112, y + 8);
    lv_bar_set_range(mpBar[i], 0, 700);  // 100/50 @ 12 V ~ 700 W max
    mpW[i] = lbl(f, &lv_font_montserrat_20, col::SOLAR, "-- W");
    lv_obj_set_style_text_align(mpW[i], LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(mpW[i], 90);
    lv_obj_set_pos(mpW[i], 292, y + 2);
  }

  // ---------------- temperatures
  lv_obj_t *t = card(p, 12, 252, 228, 172, "TEMPERATURE", col::CLIMATE);
  lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(t, goClimate, LV_EVENT_CLICKED, nullptr);
  for (int i = 0; i < cv::NUM_TEMPS; i++) {
    int y = 26 + i * 42;
    tName[i] = lbl(t, &lv_font_montserrat_16, col::MUTED, app::meta.tempName[i]);
    lv_obj_set_pos(tName[i], 0, y + 6);
    tVal[i] = lbl(t, &lv_font_montserrat_28, col::TEXT, "--");
    lv_obj_align(tVal[i], LV_ALIGN_TOP_RIGHT, 0, y);
  }

  // ---------------- water
  lv_obj_t *w = card(p, 252, 252, 236, 172, "WATER", col::WATER);
  lv_obj_add_flag(w, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(w, goWater, LV_EVENT_CLICKED, nullptr);
  for (int i = 0; i < cv::NUM_TANKS; i++) {
    int x = 8 + i * 72;
    tkBar[i] = vbar(w, 38, 86, col::WATER);
    lv_obj_set_pos(tkBar[i], x + 8, 24);
    lv_obj_clear_flag(tkBar[i], LV_OBJ_FLAG_CLICKABLE);
    tkPct[i] = lbl(w, &lv_font_montserrat_16, col::TEXT, "--");
    lv_obj_set_width(tkPct[i], 56);
    lv_obj_set_style_text_align(tkPct[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(tkPct[i], x - 1, 114);
    tkName[i] = lbl(w, &lv_font_montserrat_12, col::MUTED, app::meta.tankName[i]);
    lv_obj_set_width(tkName[i], 66);
    lv_label_set_long_mode(tkName[i], LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(tkName[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(tkName[i], x - 6, 132);
  }

  // ---------------- quick controls
  lv_obj_t *q = card(p, 500, 252, 200, 172, "CONTROLS", col::ACCENT);
  lv_obj_set_style_pad_all(q, 10, 0);
  for (int i = 0; i < cv::NUM_PUMPS; i++) {
    tilePump[i] = tile(q, 84, 64, LV_SYMBOL_LOOP, i == 0 ? "Pump 1" : "Pump 2", pumpCb, (void *)(intptr_t)i);
    lv_obj_set_pos(tilePump[i], i * 92, 20);
  }
  tileInv = tile(q, 84, 64, LV_SYMBOL_POWER, "Inverter", invCb);
  lv_obj_set_pos(tileInv, 0, 90);
  tileAc = tile(q, 84, 64, LV_SYMBOL_SHUFFLE, "Aircon", acCb);
  lv_obj_set_pos(tileAc, 92, 90);
}

void updateHome() {
  const cv::State &s = app::state;
  char buf[24];

  if (seenMeta != app::metaVer) {
    seenMeta = app::metaVer;
    for (int i = 0; i < cv::NUM_SOLAR; i++) lv_label_set_text(mpName[i], app::meta.solarName[i]);
    for (int i = 0; i < cv::NUM_TEMPS; i++) lv_label_set_text(tName[i], app::meta.tempName[i]);
    for (int i = 0; i < cv::NUM_TANKS; i++) lv_label_set_text(tkName[i], app::meta.tankName[i]);
  }

  // battery
  if (s.bat.online) {
    lv_arc_set_value(arcSoc, (int)(s.bat.soc + 0.5f));
    lv_obj_set_style_arc_color(arcSoc, c(socColor(s.bat.soc)), LV_PART_INDICATOR);
    setText(lSoc, "%d%%", (int)(s.bat.soc + 0.5f));
    setText(lVolt, "%.2f V", s.bat.v);
    bool chg = s.bat.a > 0.2f;
    setText(lAmps, "%s%.1f A", chg ? "+" : "", s.bat.a);
    lv_obj_set_style_text_color(lAmps, c(chg ? col::CHARGE : s.bat.a < -0.2f ? col::LOAD : col::TEXT), 0);
    fmtDuration(buf, sizeof(buf), s.bat.ttgMin);
    if (s.bat.ttgMin < 0) setText(lTtg, "idle");
    else setText(lTtg, "%s\nto %s", buf, chg ? "full" : "empty");
    setText(lSrc, "%s", s.bat.source == cv::BAT_SRC_BMPRO ? "BMPRO" : "shunt");
  } else {
    lv_arc_set_value(arcSoc, 0);
    setText(lSoc, "--%%");
    setText(lVolt, "no data");
    setText(lAmps, "-- A");
    setText(lTtg, "");
  }

  // energy flow
  float solarW = s.solarW();
  setText(lSolarW, "%.0f W", solarW);
  setText(lSolarA, "%.1f A", s.bat.solarA);
  setText(lChgA, "%.1f A", s.bat.chargeA);
  setText(lChgSub, s.bat.chargerA > 0.5f ? "mains %.1f A" : "solar only", s.bat.chargerA);
  setText(lLoadA, "%.1f A", s.bat.loadA);
  setText(lLoadW, "%.0f W", s.bat.loadA * (s.bat.v > 0 ? s.bat.v : 12.8f));
  for (int i = 0; i < cv::NUM_SOLAR; i++) {
    const cv::Solar &m = s.solar[i];
    if (m.online) {
      uint32_t cc = m.err ? col::ALARM
                    : m.cs == 3 ? col::SOLAR
                    : m.cs == 4 ? col::ACCENT
                    : m.cs == 5 ? col::CHARGE : col::MUTED;
      setChip(mpChip[i], m.err ? "Error" : cv::chargeStateName(m.cs), cc);
      lv_bar_set_value(mpBar[i], (int)m.pvW, LV_ANIM_ON);
      setText(mpW[i], "%.0f W", m.pvW);
    } else {
      setChip(mpChip[i], "Offline", col::DIM);
      lv_bar_set_value(mpBar[i], 0, LV_ANIM_OFF);
      setText(mpW[i], "-- W");
    }
  }

  // temps
  for (int i = 0; i < cv::NUM_TEMPS; i++) {
    if (s.temp[i].online) {
      setText(tVal[i], "%.1f\xC2\xB0", s.temp[i].c);
      lv_obj_set_style_text_color(tVal[i], c(tempColor(i, s.temp[i].c)), 0);
    } else {
      setText(tVal[i], "--");
      lv_obj_set_style_text_color(tVal[i], c(col::DIM), 0);
    }
  }

  // tanks
  for (int i = 0; i < cv::NUM_TANKS; i++) {
    if (s.tank[i].online) {
      lv_bar_set_value(tkBar[i], (int)s.tank[i].pct, LV_ANIM_ON);
      setBarColor(tkBar[i], tankColor(i, s.tank[i].pct));
      setText(tkPct[i], "%d%%", (int)(s.tank[i].pct + 0.5f));
    } else {
      lv_bar_set_value(tkBar[i], 0, LV_ANIM_OFF);
      setText(tkPct[i], "--");
    }
  }

  // controls
  for (int i = 0; i < cv::NUM_PUMPS; i++) {
    const cv::Pump &pm = s.pump[i];
    const char *sub = pm.lockout ? "Locked" : pm.on ? "Running" : "Off";
    setTile(tilePump[i], pm.on, pm.lockout ? col::WARN : col::WATER, sub);
  }
  setTile(tileInv, s.inv.on, col::SOLAR, s.inv.on ? (s.inv.acPresent ? "240V on" : "On") : "Off");
  const char *acSub = "Off";
  if (s.ac.on && s.ac.preset >= 0 && s.ac.preset < app::meta.acPresetCount)
    acSub = app::meta.acPresetName[s.ac.preset];
  setTile(tileAc, s.ac.on, col::CLIMATE, acSub);
}

}  // namespace ui
