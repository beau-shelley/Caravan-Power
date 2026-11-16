#include <math.h>
// HOME - everything important on one screen.
//  [ Battery ring  ] [ Energy flow: solar / charger / loads + per-MPPT  ]
//  [ Temperatures  ] [ Water tanks      ] [ Gas bottle | Internal / External pumps ]
#include "../app.h"
#include "ui.h"

namespace ui {

static lv_obj_t *arcSoc, *lSoc, *lVolt, *lAmps, *lTtg, *lSrc;
static lv_obj_t *lSolarW, *lSolarA, *lChgA, *lChgSub, *lLoadA, *lLoadW;
static lv_obj_t *mpName[cv::NUM_SOLAR], *mpChip[cv::NUM_SOLAR], *mpBar[cv::NUM_SOLAR], *mpW[cv::NUM_SOLAR];
static lv_obj_t *tName[cv::NUM_TEMPS], *tVal[cv::NUM_TEMPS];
static lv_obj_t *tkBar[cv::NUM_TANKS], *tkPct[cv::NUM_TANKS], *tkName[cv::NUM_TANKS];
static lv_obj_t *tilePump[cv::NUM_PUMPS], *tileInv, *tileAc;
static lv_obj_t *gasBar, *gasPct;
static uint32_t seenMeta = UINT32_MAX;

static void pumpCb(lv_event_t *e) { togglePump((int)(intptr_t)lv_event_get_user_data(e)); }
static void invCb(lv_event_t *) { toggleInverter(); }
static void acCb(lv_event_t *) { toggleAircon(); }
static void goPower(lv_event_t *) { showPage(PAGE_POWER); }
static void goWater(lv_event_t *) { showPage(PAGE_WATER); }
static void goClimate(lv_event_t *) { showPage(PAGE_CLIMATE); }

static const lv_font_t *const kBigFont = LARGE ? &lv_font_montserrat_40 : &lv_font_montserrat_28;

static lv_obj_t *stat(lv_obj_t *parent, int x, const char *caption, uint32_t color, lv_obj_t **big, lv_obj_t **sub) {
  lv_obj_t *cap = lbl(parent, LARGE ? &lv_font_montserrat_14 : &lv_font_montserrat_12, col::MUTED, caption);
  lv_obj_set_pos(cap, x, 24);
  *big = lbl(parent, kBigFont, color, "--");
  lv_obj_set_pos(*big, x, 42);
  *sub = lbl(parent, LARGE ? &lv_font_montserrat_16 : &lv_font_montserrat_14, col::MUTED, "");
  lv_obj_set_pos(*sub, x, LARGE ? 88 : 76);
  return cap;
}

// Two rows of cards. Top: battery | energy flow. Bottom: temps | water | controls.
static constexpr int TOP_H = (INNER_H - GAP) * (LARGE ? 54 : 57) / 100, BOT_H = INNER_H - GAP - TOP_H;
static constexpr int BOT_Y = GAP + TOP_H + GAP;
static constexpr int BAT_W = splitW(LARGE ? 37 : 39), FLOW_W = INNER_W - GAP - BAT_W;

void buildHome(lv_obj_t *p) {
  // ---------------- battery
  lv_obj_t *b = card(p, GAP, GAP, BAT_W, TOP_H, "BATTERY", col::CHARGE);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(b, goPower, LV_EVENT_CLICKED, nullptr);
  constexpr int arcD = innerOf(TOP_H) - 50;  // leave room for the amps row underneath
  arcSoc = lv_arc_create(b);
  lv_obj_set_size(arcSoc, arcD, arcD);
  lv_obj_align(arcSoc, LV_ALIGN_TOP_MID, 0, 14);
  lv_arc_set_rotation(arcSoc, 135);
  lv_arc_set_bg_angles(arcSoc, 0, 270);
  lv_arc_set_range(arcSoc, 0, 100);
  lv_arc_set_value(arcSoc, 0);
  lv_obj_remove_style(arcSoc, nullptr, LV_PART_KNOB);
  lv_obj_clear_flag(arcSoc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(arcSoc, arcD / 11, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arcSoc, arcD / 11, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arcSoc, c(col::TILE), LV_PART_MAIN);
  lv_obj_set_style_arc_color(arcSoc, c(col::CHARGE), LV_PART_INDICATOR);
  lSoc = lbl(b, LARGE ? &lv_font_montserrat_48 : &lv_font_montserrat_40, col::TEXT, "--%");
  lv_obj_align_to(lSoc, arcSoc, LV_ALIGN_CENTER, 0, -8);
  lVolt = lbl(b, LARGE ? &lv_font_montserrat_20 : &lv_font_montserrat_16, col::MUTED, "-- V");
  lv_obj_align_to(lVolt, arcSoc, LV_ALIGN_CENTER, 0, LARGE ? 34 : 28);
  lAmps = lbl(b, LARGE ? &lv_font_montserrat_28 : &lv_font_montserrat_24, col::TEXT, "-- A");
  lv_obj_align(lAmps, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lTtg = lbl(b, LARGE ? &lv_font_montserrat_14 : &lv_font_montserrat_12, col::MUTED, "");
  lv_obj_set_style_text_align(lTtg, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_align(lTtg, LV_ALIGN_BOTTOM_RIGHT, 0, -2);
  lSrc = lbl(b, &lv_font_montserrat_12, col::DIM, "");
  lv_obj_align(lSrc, LV_ALIGN_TOP_RIGHT, 0, 0);

  // ---------------- energy flow
  lv_obj_t *f = card(p, GAP + BAT_W + GAP, GAP, FLOW_W, TOP_H, "ENERGY FLOW", col::SOLAR);
  lv_obj_add_flag(f, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(f, goPower, LV_EVENT_CLICKED, nullptr);
  constexpr int fw = innerOf(FLOW_W), statW = fw / 3;
  stat(f, 0, LV_SYMBOL_IMAGE " SOLAR", col::SOLAR, &lSolarW, &lSolarA);
  stat(f, statW, LV_SYMBOL_CHARGE " CHARGING", col::CHARGE, &lChgA, &lChgSub);
  stat(f, 2 * statW, LV_SYMBOL_POWER " LOADS", col::LOAD, &lLoadA, &lLoadW);
  // One row per solar controller, spread over the space under the stats.
  constexpr int rowTop = LARGE ? 128 : 112, rowStep = (innerOf(TOP_H) - rowTop) / cv::NUM_SOLAR;
  constexpr int nameW = LARGE ? 130 : 112, wattW = LARGE ? 100 : 90;
  for (int i = 0; i < cv::NUM_SOLAR; i++) {
    int y = rowTop + i * rowStep;
    mpName[i] = lbl(f, LARGE ? &lv_font_montserrat_20 : &lv_font_montserrat_16, col::TEXT, app::meta.solarName[i]);
    lv_obj_set_width(mpName[i], nameW - 8);
    lv_label_set_long_mode(mpName[i], LV_LABEL_LONG_DOT);
    lv_obj_set_pos(mpName[i], 0, y);
    mpChip[i] = chip(f);
    lv_obj_set_pos(mpChip[i], 0, y + (LARGE ? 28 : 22));
    mpBar[i] = vbar(f, fw - nameW - wattW - 10, LARGE ? 12 : 10, col::SOLAR);
    lv_obj_set_pos(mpBar[i], nameW, y + 8);
    lv_bar_set_range(mpBar[i], 0, 700);  // 100/50 @ 12 V ~ 700 W max
    mpW[i] = lbl(f, LARGE ? &lv_font_montserrat_24 : &lv_font_montserrat_20, col::SOLAR, "-- W");
    lv_obj_set_style_text_align(mpW[i], LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(mpW[i], wattW);
    lv_obj_set_pos(mpW[i], fw - wattW, y + 2);
  }

  constexpr int bw = colW(3), bh = innerOf(BOT_H);

  // ---------------- temperatures
  lv_obj_t *t = card(p, colX(0, 3), BOT_Y, bw, BOT_H, "TEMPERATURE", col::CLIMATE);
  lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(t, goClimate, LV_EVENT_CLICKED, nullptr);
  constexpr int tStep = (bh - 24) / cv::NUM_TEMPS;
  for (int i = 0; i < cv::NUM_TEMPS; i++) {
    int y = 24 + i * tStep + (tStep - 34) / 2;
    tName[i] = lbl(t, LARGE ? &lv_font_montserrat_20 : &lv_font_montserrat_16, col::MUTED, app::meta.tempName[i]);
    lv_obj_set_pos(tName[i], 0, y + 6);
    tVal[i] = lbl(t, &lv_font_montserrat_28, col::TEXT, "--");
    lv_obj_align(tVal[i], LV_ALIGN_TOP_RIGHT, 0, y);
  }

  // ---------------- water
  lv_obj_t *w = card(p, colX(1, 3), BOT_Y, bw, BOT_H, "WATER", col::WATER);
  lv_obj_add_flag(w, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(w, goWater, LV_EVENT_CLICKED, nullptr);
  constexpr int slot = innerOf(bw) / cv::NUM_TANKS, tankBarW = LARGE ? 46 : 38;
  constexpr int tankBarH = bh - 24 - 46;  // caption above, % and name below
  for (int i = 0; i < cv::NUM_TANKS; i++) {
    int x = i * slot;
    tkBar[i] = vbar(w, tankBarW, tankBarH, col::WATER);
    lv_obj_set_pos(tkBar[i], x + (slot - tankBarW) / 2, 24);
    lv_obj_clear_flag(tkBar[i], LV_OBJ_FLAG_CLICKABLE);
    tkPct[i] = lbl(w, LARGE ? &lv_font_montserrat_20 : &lv_font_montserrat_16, col::TEXT, "--");
    lv_obj_set_width(tkPct[i], slot);
    lv_obj_set_style_text_align(tkPct[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(tkPct[i], x, 24 + tankBarH + 4);
    tkName[i] = lbl(w, LARGE ? &lv_font_montserrat_14 : &lv_font_montserrat_12, col::MUTED, app::meta.tankName[i]);
    lv_obj_set_width(tkName[i], slot - 2);
    lv_label_set_long_mode(tkName[i], LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(tkName[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(tkName[i], x + 1, 24 + tankBarH + (LARGE ? 28 : 22));
  }

  // ---------------- gas bottle + quick controls. Gas gauge (BMPRO SmartSense)
  // on the left; the pump tiles stacked at half height on the right (plus
  // inverter / aircon in a second column when those features are on, ui.h).
  constexpr int nTiles = cv::NUM_PUMPS + HAS_INVERTER + HAS_AIRCON;
  constexpr int tCols = nTiles > cv::NUM_PUMPS ? 2 : 1, tRows = (nTiles + tCols - 1) / tCols;
  lv_obj_t *q = card(p, colX(2, 3), BOT_Y, bw, BOT_H, nTiles > cv::NUM_PUMPS ? "GAS & CONTROLS" : "GAS & PUMPS",
                     col::GAS);
  lv_obj_set_style_pad_all(q, 10, 0);
  constexpr int qw = bw - 20, qh = BOT_H - 20, tg = 8;
  constexpr int gasW = (tCols == 1 ? qw * 50 / 100 : qw * 34 / 100);
  // Shaped like a 9 kg LPG bottle: about two-thirds as wide as it is tall.
  constexpr int gasBarH = qh - 20 - (LARGE ? 50 : 42);  // % and name below
  constexpr int shroudH = 16, bodyH = gasBarH - shroudH + 4;
  constexpr int gasBarW = bodyH * 66 / 100 < gasW - 8 ? bodyH * 66 / 100 : gasW - 8;
  // The bottle: a wide rounded body with the carry-handle shroud on top.
  lv_obj_t *shroud = lv_obj_create(q);
  lv_obj_remove_style_all(shroud);
  lv_obj_set_size(shroud, gasBarW * 58 / 100, shroudH + 8);
  lv_obj_set_style_border_width(shroud, 5, 0);
  lv_obj_set_style_border_color(shroud, c(col::TILE), 0);
  lv_obj_set_style_border_opa(shroud, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(shroud, 8, 0);
  lv_obj_set_pos(shroud, (gasW - gasBarW * 58 / 100) / 2, 20);
  gasBar = vbar(q, gasBarW, bodyH, col::GAS);
  lv_obj_set_style_radius(gasBar, gasBarW / 4, 0);
  lv_obj_set_style_radius(gasBar, gasBarW / 4, LV_PART_INDICATOR);
  lv_obj_set_pos(gasBar, (gasW - gasBarW) / 2, 20 + shroudH);
  lv_obj_clear_flag(gasBar, LV_OBJ_FLAG_CLICKABLE);
  gasPct = lbl(q, LARGE ? &lv_font_montserrat_20 : &lv_font_montserrat_16, col::TEXT, "--");
  lv_obj_set_width(gasPct, gasW);
  lv_obj_set_style_text_align(gasPct, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(gasPct, 0, 20 + gasBarH + 4);
  lv_obj_t *gasName = lbl(q, LARGE ? &lv_font_montserrat_14 : &lv_font_montserrat_12, col::MUTED, "Gas");
  lv_obj_set_width(gasName, gasW);
  lv_obj_set_style_text_align(gasName, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(gasName, 0, 20 + gasBarH + (LARGE ? 28 : 22));

  constexpr int tx0 = gasW + tg, tw = (qw - tx0 - (tCols - 1) * tg) / tCols;
  constexpr int th = (qh - 20 - (tRows - 1) * tg) / tRows;
  int n = 0;
  auto place = [&](lv_obj_t *t) {
    lv_obj_set_pos(t, tx0 + (n / tRows) * (tw + tg), 20 + (n % tRows) * (th + tg));
    n++;
  };
  for (int i = 0; i < cv::NUM_PUMPS; i++) {
    tilePump[i] = tile(q, tw, th, LV_SYMBOL_LOOP, app::meta.pumpName[i], pumpCb, (void *)(intptr_t)i);
    place(tilePump[i]);
  }
  if (HAS_INVERTER) {
    tileInv = tile(q, tw, th, LV_SYMBOL_POWER, "Inverter", invCb);
    place(tileInv);
  }
  if (HAS_AIRCON) {
    tileAc = tile(q, tw, th, LV_SYMBOL_SHUFFLE, "Aircon", acCb);
    place(tileAc);
  }
}

void updateHome() {
  const cv::State &s = app::state;
  char buf[24];

  if (seenMeta != app::metaVer) {
    seenMeta = app::metaVer;
    for (int i = 0; i < cv::NUM_SOLAR; i++) lv_label_set_text(mpName[i], app::meta.solarName[i]);
    for (int i = 0; i < cv::NUM_TEMPS; i++) lv_label_set_text(tName[i], app::meta.tempName[i]);
    for (int i = 0; i < cv::NUM_TANKS; i++) lv_label_set_text(tkName[i], app::meta.tankName[i]);
    for (int i = 0; i < cv::NUM_PUMPS; i++) lv_label_set_text(lv_obj_get_child(tilePump[i], 1), app::meta.pumpName[i]);
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
      setText(tVal[i], "%d\xC2\xB0", (int)lroundf(s.temp[i].c));
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

  // gas bottle
  if (s.gas.online) {
    lv_bar_set_value(gasBar, (int)s.gas.pct, LV_ANIM_ON);
    setBarColor(gasBar, gasColor(s.gas.pct));
    setText(gasPct, "%d%%", (int)(s.gas.pct + 0.5f));
    lv_obj_set_style_text_color(gasPct, c(col::TEXT), 0);
  } else {
    lv_bar_set_value(gasBar, 0, LV_ANIM_OFF);
    setText(gasPct, "--");
    lv_obj_set_style_text_color(gasPct, c(col::DIM), 0);
  }

  // controls
  for (int i = 0; i < cv::NUM_PUMPS; i++) {
    const cv::Pump &pm = s.pump[i];
    const char *sub = pm.lockout ? "Locked" : pm.on ? "Running" : "Off";
    setTile(tilePump[i], pm.on, pm.lockout ? col::WARN : col::WATER, sub);
  }
  if (tileInv) setTile(tileInv, s.inv.on, col::SOLAR, s.inv.on ? (s.inv.acPresent ? "240V on" : "On") : "Off");
  if (tileAc) {
    const char *acSub = "Off";
    if (s.ac.on && s.ac.preset >= 0 && s.ac.preset < app::meta.acPresetCount)
      acSub = app::meta.acPresetName[s.ac.preset];
    setTile(tileAc, s.ac.on, col::CLIMATE, acSub);
  }
}

}  // namespace ui
