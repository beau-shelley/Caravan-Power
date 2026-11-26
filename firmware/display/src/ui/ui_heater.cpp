// Diesel heater card (Climate page): on/off, thermostat or power level,
// and the heater's status. Everything is sent to the hub, which talks to the
// heater; this card only shows what the hub reports (same as the phone).
#include <math.h>
#include <string.h>

#include "../app.h"
#include "ui.h"

namespace ui {

static lv_obj_t *hCard, *hChip, *hPower, *hPowerIco, *hStatus, *hSub;
static lv_obj_t *hMinus, *hPlus, *hSet, *hSetUnit, *hSetCap, *hModeT, *hModeL, *hDial;
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

  // Dial: a 270 degree arc showing the set point (or power level), with the
  // number in the middle and - / + either side. Display only (not draggable),
  // so a stray touch can't change the heater; drawn like the battery ring.
  const int rh = LARGE ? 44 : 38;                       // readings row at the bottom
  const int mh = LARGE ? 40 : 34;                       // mode row above it
  const int D = H - 24 - rh - mh - (LARGE ? 18 : 12);   // dial diameter: whatever is left
  const int dy = 22;
  hDial = lv_arc_create(hCard);
  lv_obj_set_size(hDial, D, D);
  lv_obj_align(hDial, LV_ALIGN_TOP_MID, 0, dy);
  lv_arc_set_rotation(hDial, 135);
  lv_arc_set_bg_angles(hDial, 0, 270);
  lv_arc_set_range(hDial, cv::HEATER_MIN_C, cv::HEATER_MAX_C);
  lv_obj_clear_flag(hDial, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(hDial, D / 13, LV_PART_MAIN);
  lv_obj_set_style_arc_width(hDial, D / 13, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(hDial, c(col::TILE), LV_PART_MAIN);
  lv_obj_set_style_arc_color(hDial, c(col::HEAT), LV_PART_INDICATOR);
  // Knob: white dot with a heater-orange ring at the end of the arc.
  lv_obj_set_style_bg_color(hDial, lv_color_white(), LV_PART_KNOB);
  lv_obj_set_style_border_color(hDial, c(col::HEAT), LV_PART_KNOB);
  lv_obj_set_style_border_width(hDial, 4, LV_PART_KNOB);
  lv_obj_set_style_pad_all(hDial, 3, LV_PART_KNOB);
  lv_obj_set_style_shadow_width(hDial, 0, LV_PART_KNOB);

  lv_obj_t *cap = lbl(hCard, &lv_font_montserrat_12, col::MUTED, "SET TO");
  lv_obj_set_style_text_letter_space(cap, 2, 0);
  lv_obj_align_to(cap, hDial, LV_ALIGN_CENTER, 0, -(D / 4) - 4);
  hSetCap = cap;
  hSet = lbl(hCard, LARGE ? &lv_font_montserrat_48 : &lv_font_montserrat_40, col::TEXT, "--");
  lv_obj_set_width(hSet, D - 40);
  lv_obj_set_style_text_align(hSet, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align_to(hSet, hDial, LV_ALIGN_CENTER, 0, -6);
  hSetUnit = lbl(hCard, &lv_font_montserrat_14, col::MUTED, "");  // "/ 10" in power-level mode
  lv_obj_set_width(hSetUnit, D - 40);
  lv_obj_set_style_text_align(hSetUnit, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align_to(hSetUnit, hDial, LV_ALIGN_CENTER, 0, D / 6 + 2);
  hSub = lbl(hCard, &lv_font_montserrat_12, col::MUTED, "");
  lv_obj_set_width(hSub, D * 62 / 100);
  lv_label_set_long_mode(hSub, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(hSub, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align_to(hSub, hDial, LV_ALIGN_CENTER, 0, D / 4 + 10);
  hStatus = lbl(hCard, &lv_font_montserrat_12, col::TEXT, "");  // kept for updateHeater(); the chip says it
  lv_obj_add_flag(hStatus, LV_OBJ_FLAG_HIDDEN);

  // - / + : round, either side of the bottom of the dial
  const int bd = LARGE ? 58 : 48;
  const int sy = dy + D - bd;
  hMinus = roundBtn(hCard, bd, bd, LV_SYMBOL_MINUS, stepCb, (void *)(intptr_t)-1);
  lv_obj_set_pos(hMinus, 0, sy);
  hPlus = roundBtn(hCard, bd, bd, LV_SYMBOL_PLUS, stepCb, (void *)(intptr_t)1);
  lv_obj_set_pos(hPlus, W - bd, sy);
  for (lv_obj_t *b : {hMinus, hPlus}) lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);

  // [ Thermostat | Power level ]  [ power ]
  const int my = dy + D + (LARGE ? 10 : 6), pw = LARGE ? 64 : 54, mw = (W - pw - 8 - 8) / 2;
  hPower = lv_btn_create(hCard);
  lv_obj_set_size(hPower, pw, mh);
  lv_obj_set_pos(hPower, W - pw, my);
  lv_obj_set_style_radius(hPower, 12, 0);
  lv_obj_set_style_shadow_width(hPower, 0, 0);
  lv_obj_set_style_border_width(hPower, 1, 0);
  lv_obj_add_event_cb(hPower, powerCb, LV_EVENT_CLICKED, nullptr);
  hPowerIco = lbl(hPower, LARGE ? &lv_font_montserrat_24 : &lv_font_montserrat_20, col::MUTED, LV_SYMBOL_POWER);
  lv_obj_center(hPowerIco);
  hModeT = button(hCard, mw, mh, "Thermostat", col::TILE, modeCb, (void *)(intptr_t)0);
  lv_obj_set_pos(hModeT, 0, my);
  hModeL = button(hCard, mw, mh, "Power level", col::TILE, modeCb, (void *)(intptr_t)1);
  lv_obj_set_pos(hModeL, mw + 8, my);
  for (lv_obj_t *b : {hModeT, hModeL}) lv_obj_set_style_text_font(lv_obj_get_child(b, 0), &lv_font_montserrat_14, 0);

  // Readings: cabin | heater | supply (Bluetooth only)
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
  // Selected mode: warm gradient pill with dark text (as in the app)
  lv_obj_set_style_bg_color(b, on ? lv_color_mix(lv_color_white(), c(col::HEAT), 110) : c(col::TILE), 0);
  lv_obj_set_style_bg_grad_color(b, on ? c(col::HEAT) : c(col::TILE), 0);
  lv_obj_set_style_bg_grad_dir(b, on ? LV_GRAD_DIR_VER : LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_border_color(b, on ? c(col::HEAT) : c(col::BORDER), 0);
  lv_obj_set_style_text_color(lv_obj_get_child(b, 0), on ? c(col::RAIL) : c(col::MUTED), 0);
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
  lv_obj_set_style_bg_color(hPower, lit ? lv_color_mix(lv_color_white(), c(pc), 70) : c(col::TILE), 0);
  lv_obj_set_style_bg_grad_color(hPower, lit ? c(pc) : c(col::TILE), 0);
  lv_obj_set_style_bg_grad_dir(hPower, lit ? LV_GRAD_DIR_VER : LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_border_color(hPower, lit ? c(pc) : c(col::BORDER), 0);
  lv_obj_set_style_text_color(hPowerIco, c(lit ? col::RAIL : col::MUTED), 0);
  // Dial: heater orange while it runs (amber lighting, blue cooling), grey when off
  uint32_t dc = lost ? col::DIM : fault ? col::ALARM : lit ? pc : col::DIM;
  lv_obj_set_style_arc_color(hDial, c(dc), LV_PART_INDICATOR);
  lv_obj_set_style_border_color(hDial, c(dc), LV_PART_KNOB);
  lv_obj_set_style_opa(hPower, lost ? LV_OPA_50 : LV_OPA_COVER, 0);

  // Set point, whole degrees, same blue-to-red scale as the thermometers
  bool lvl = h.mode == cv::HTR_MODE_LEVEL;
  if (lvl) {
    lv_arc_set_range(hDial, 1, cv::HEATER_LEVELS);
    lv_arc_set_value(hDial, shownLevel());
    setText(hSetCap, "%s", "LEVEL");
    setText(hSet, "%d", shownLevel());
    setText(hSetUnit, "of %d", cv::HEATER_LEVELS);
    lv_obj_set_style_text_color(hSet, c(col::HEAT), 0);
  } else {
    lv_arc_set_range(hDial, cv::HEATER_MIN_C, cv::HEATER_MAX_C);
    lv_arc_set_value(hDial, shownSet());
    setText(hSetCap, "%s", "SET TO");
    setText(hSet, "%d\xC2\xB0", shownSet());
    setText(hSetUnit, "%s", "");
    lv_obj_set_style_text_color(hSet, c(tempColor(cv::TEMP_INSIDE, shownSet())), 0);
  }
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
