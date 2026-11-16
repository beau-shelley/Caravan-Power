// Diesel heater card (Climate page): on/off, thermostat or power level,
// and the heater's status. Everything is sent to the hub, which talks to the
// heater; this card only shows what the hub reports (same as the phone).
#include <math.h>
#include <string.h>

#include "../app.h"
#include "ui.h"

namespace ui {

static lv_obj_t *hCard, *hChip, *hPower, *hPowerIco, *hStatus, *hSub;
static lv_obj_t *hMinus, *hPlus, *hSet, *hSetUnit, *hModeT, *hModeL;
static lv_obj_t *hStats, *hStatVal[3], *hNoStatus;

// A tap shows its new set point straight away; the hub's status catches up
// within a couple of seconds (and wins after that).
static int pendSet = -1, pendLevel = -1;
static uint32_t pendMs = 0;
static constexpr uint32_t PEND_MS = 4000;
static bool pending() { return pendMs && millis() - pendMs < PEND_MS; }

static int shownSet() { return pending() && pendSet >= 0 ? pendSet : app::state.htr.setC; }
static int shownLevel() { return pending() && pendLevel >= 0 ? pendLevel : app::state.htr.level; }

static void powerCb(lv_event_t *) {
  const cv::Heater &h = app::state.htr;
  app::cmdHeaterOn(!h.running());
}

static void stepCb(lv_event_t *e) {
  int d = (int)(intptr_t)lv_event_get_user_data(e);
  const cv::Heater &h = app::state.htr;
  if (h.mode == cv::HTR_MODE_LEVEL) {
    int v = shownLevel() + d;
    if (v < 1 || v > cv::HEATER_LEVELS) return;
    pendLevel = v;
    pendSet = -1;
    app::cmdHeaterSet("level", v);
  } else {
    int v = shownSet() + d;
    if (v < cv::HEATER_MIN_C || v > cv::HEATER_MAX_C) return;
    pendSet = v;
    pendLevel = -1;
    app::cmdHeaterSet("temp", v);
  }
  pendMs = millis() | 1;
  updateHeater();
}

static void modeCb(lv_event_t *e) {
  bool level = (intptr_t)lv_event_get_user_data(e) != 0;
  pendMs = 0;
  app::cmdHeaterMode(level);
}

static lv_obj_t *roundBtn(lv_obj_t *p, int w, int h, const char *sym, lv_event_cb_t cb, void *ud) {
  lv_obj_t *b = button(p, w, h, sym, col::TILE, cb, ud);
  lv_obj_set_style_text_font(lv_obj_get_child(b, 0), &lv_font_montserrat_28, 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_border_color(b, c(col::BORDER), 0);
  return b;
}

lv_obj_t *buildHeater(lv_obj_t *p, int x, int y, int w, int h) {
  const int W = innerOf(w), H = innerOf(h);
  hCard = card(p, x, y, w, h, "DIESEL HEATER", col::HEAT);
  hChip = chip(hCard);
  lv_obj_align(hChip, LV_ALIGN_TOP_RIGHT, 0, -3);

  constexpr int D = LARGE ? 88 : 72;  // round on/off button
  hPower = lv_btn_create(hCard);
  lv_obj_set_size(hPower, D, D);
  lv_obj_set_pos(hPower, 0, 26);
  lv_obj_set_style_radius(hPower, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_shadow_width(hPower, 0, 0);
  lv_obj_set_style_border_width(hPower, 2, 0);
  lv_obj_add_event_cb(hPower, powerCb, LV_EVENT_CLICKED, nullptr);
  hPowerIco = lbl(hPower, LARGE ? &lv_font_montserrat_40 : &lv_font_montserrat_28, col::MUTED, LV_SYMBOL_POWER);
  lv_obj_center(hPowerIco);

  const int tx = D + 16;
  hStatus = lbl(hCard, LARGE ? &lv_font_montserrat_24 : &lv_font_montserrat_20, col::TEXT, "Off");
  lv_obj_set_pos(hStatus, tx, 26 + D / 2 - (LARGE ? 30 : 26));
  hSub = lbl(hCard, LARGE ? &lv_font_montserrat_14 : &lv_font_montserrat_12, col::MUTED, "");
  lv_obj_set_width(hSub, W - tx);
  lv_label_set_long_mode(hSub, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(hSub, tx, 26 + D / 2 + 2);

  // Set point: [-]  24°  [+]
  const int sy = 26 + D + (LARGE ? 14 : 8), bw = LARGE ? 64 : 54, bh = LARGE ? 56 : 46;
  hMinus = roundBtn(hCard, bw, bh, LV_SYMBOL_MINUS, stepCb, (void *)(intptr_t)-1);
  lv_obj_set_pos(hMinus, 0, sy);
  hPlus = roundBtn(hCard, bw, bh, LV_SYMBOL_PLUS, stepCb, (void *)(intptr_t)1);
  lv_obj_set_pos(hPlus, W - bw, sy);
  hSet = lbl(hCard, LARGE ? &lv_font_montserrat_48 : &lv_font_montserrat_40, col::TEXT, "--");
  lv_obj_align(hSet, LV_ALIGN_TOP_MID, 0, sy + bh / 2 - (LARGE ? 28 : 24));
  hSetUnit = lbl(hCard, &lv_font_montserrat_14, col::MUTED, "");
  lv_obj_align_to(hSetUnit, hSet, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -8);

  // Thermostat | Power level
  const int my = sy + bh + (LARGE ? 12 : 8), mh = LARGE ? 40 : 34, mw = (W - 8) / 2;
  hModeT = button(hCard, mw, mh, "Thermostat", col::TILE, modeCb, (void *)(intptr_t)0);
  lv_obj_set_pos(hModeT, 0, my);
  hModeL = button(hCard, mw, mh, "Power level", col::TILE, modeCb, (void *)(intptr_t)1);
  lv_obj_set_pos(hModeL, mw + 8, my);
  for (lv_obj_t *b : {hModeT, hModeL}) lv_obj_set_style_text_font(lv_obj_get_child(b, 0), &lv_font_montserrat_14, 0);

  // Readings: cabin | heater | supply (Bluetooth only)
  const int rh = LARGE ? 44 : 38;
  hStats = panel(hCard, 0, H - rh, W, rh);
  static const char *caps[3] = {"Cabin", "Heater", "Supply"};
  for (int i = 0; i < 3; i++) {
    lv_obj_t *cap = lbl(hStats, &lv_font_montserrat_12, col::MUTED, caps[i]);
    lv_obj_set_pos(cap, i * W / 3, 0);
    hStatVal[i] = lbl(hStats, LARGE ? &lv_font_montserrat_20 : &lv_font_montserrat_16, col::TEXT, "--");
    lv_obj_set_pos(hStatVal[i], i * W / 3, LARGE ? 16 : 14);
  }
  hNoStatus = lbl(hCard, LARGE ? &lv_font_montserrat_14 : &lv_font_montserrat_12, col::MUTED,
                  "433 MHz remote: the heater doesn't report back");
  lv_obj_set_width(hNoStatus, W);
  lv_label_set_long_mode(hNoStatus, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(hNoStatus, 0, H - rh + 4);
  lv_obj_add_flag(hNoStatus, LV_OBJ_FLAG_HIDDEN);
  return hCard;
}

static void setSeg(lv_obj_t *b, bool on) {
  lv_obj_set_style_bg_color(b, on ? lv_color_mix(c(col::HEAT), c(col::TILE), 80) : c(col::TILE), 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_border_color(b, on ? c(col::HEAT) : c(col::BORDER), 0);
  lv_obj_set_style_text_color(lv_obj_get_child(b, 0), on ? c(col::HEAT) : c(col::MUTED), 0);
}

void updateHeater() {
  if (!hCard) return;
  const cv::Heater &h = app::state.htr;
  const bool ble = h.link == cv::HTR_LINK_BLE, rf = h.link == cv::HTR_LINK_RF;
  const bool lost = ble && !h.online;
  const bool fault = h.hasStatus() && h.err;

  // Status chip + headline
  const char *word = lost ? "Not connected" : fault ? "Fault" : cv::heaterPhaseText(h.phase);
  uint32_t tone = lost ? col::DIM
                  : fault ? col::ALARM
                  : h.phase == cv::HTR_RUNNING ? col::HEAT
                  : h.phase == cv::HTR_STARTING ? col::WARN
                  : h.phase == cv::HTR_COOLING ? col::WATER
                                               : col::MUTED;
  setChip(hChip, word, tone);
  setText(hStatus, "%s", lost ? "Not connected" : fault ? "Fault" : h.running() ? "On" : "Off");
  lv_obj_set_style_text_color(hStatus, c(fault ? col::ALARM : lost ? col::DIM : col::TEXT), 0);
  if (lost) setText(hSub, "Looking for the heater over Bluetooth");
  else if (fault) setText(hSub, "%s", cv::heaterErrorText(h.err));
  else if (rf) setText(hSub, "%s", h.running() ? "Start sent by remote" : "Stop sent by remote");
  else if (h.phase == cv::HTR_STARTING) setText(hSub, "Glow plug on, lighting");
  else if (h.phase == cv::HTR_COOLING) setText(hSub, "Fan runs on to cool it");
  else if (h.running()) setText(hSub, h.mode == cv::HTR_MODE_LEVEL ? "Fixed power level" : "Holding the set temperature");
  else setText(hSub, "Tap to start");
  lv_obj_set_style_text_color(hSub, c(fault ? col::ALARM : col::MUTED), 0);

  // Round power button lights up while it runs (amber while lighting, blue cooling)
  bool lit = h.running() || h.phase == cv::HTR_COOLING;
  uint32_t pc = h.phase == cv::HTR_COOLING ? col::WATER : h.phase == cv::HTR_STARTING ? col::WARN : col::HEAT;
  lv_obj_set_style_bg_color(hPower, lit ? lv_color_mix(c(pc), c(col::TILE), 90) : c(col::TILE), 0);
  lv_obj_set_style_border_color(hPower, lit ? c(pc) : c(col::BORDER), 0);
  lv_obj_set_style_text_color(hPowerIco, c(lit ? pc : col::MUTED), 0);
  lv_obj_set_style_opa(hPower, lost ? LV_OPA_50 : LV_OPA_COVER, 0);

  // Set point, whole degrees, same blue-to-red scale as the thermometers
  bool lvl = h.mode == cv::HTR_MODE_LEVEL;
  if (lvl) {
    setText(hSet, "%d", shownLevel());
    setText(hSetUnit, "/ %d", cv::HEATER_LEVELS);
    lv_obj_set_style_text_color(hSet, c(col::HEAT), 0);
  } else {
    setText(hSet, "%d\xC2\xB0", shownSet());
    setText(hSetUnit, "%s", "set");
    lv_obj_set_style_text_color(hSet, c(tempColor(cv::TEMP_INSIDE, shownSet())), 0);
  }
  lv_obj_align_to(hSetUnit, hSet, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -8);
  setSeg(hModeT, !lvl);
  setSeg(hModeL, lvl);
  for (lv_obj_t *b : {hMinus, hPlus, hModeT, hModeL}) lv_obj_set_style_opa(b, lost ? LV_OPA_50 : LV_OPA_COVER, 0);

  // Readings: only Bluetooth reports them
  if (rf) {
    lv_obj_add_flag(hStats, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(hNoStatus, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_clear_flag(hStats, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(hNoStatus, LV_OBJ_FLAG_HIDDEN);
    if (h.hasStatus()) {
      setText(hStatVal[0], "%d\xC2\xB0", (int)lroundf(h.cabinC));
      lv_obj_set_style_text_color(hStatVal[0], c(tempColor(cv::TEMP_INSIDE, h.cabinC)), 0);
      // The heat exchanger runs at 100-200 C when it's working, so it isn't on
      // the room-temperature colour scale: plain text, orange while hot.
      setText(hStatVal[1], "%d\xC2\xB0", (int)lroundf(h.bodyC));
      lv_obj_set_style_text_color(hStatVal[1], c(h.bodyC >= 60 ? col::HEAT : col::TEXT), 0);
      setText(hStatVal[2], "%.1f V", h.supplyV);
      lv_obj_set_style_text_color(hStatVal[2], c(h.supplyV > 0 && h.supplyV < 11.5f ? col::WARN : col::TEXT), 0);
    } else {
      for (lv_obj_t *v : hStatVal) {
        setText(v, "--");
        lv_obj_set_style_text_color(v, c(col::DIM), 0);
      }
    }
  }
}

}  // namespace ui
