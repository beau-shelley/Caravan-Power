// LEVEL page - bubble level + "raise this wheel / jockey by N mm" advice.
// Top-down view: front (hitch) at the top, door side (left) on the left.
#include "../app.h"
#include "Leveling.h"
#include "ui.h"

namespace ui {

static constexpr int CX = 192, CY = 196;     // bullseye centre inside the card
static constexpr float PX_PER_DEG = 160 / 3.0f;
static constexpr float LEVEL_TOL = 0.3f;     // degrees counted as "level"

static lv_obj_t *bubble, *lRoll, *lPitch, *lAdvice, *lStatus, *lDims, *lOffline;

static lv_obj_t *ring(lv_obj_t *p, int r, uint32_t color, int width) {
  lv_obj_t *o = lv_obj_create(p);
  lv_obj_set_size(o, r * 2, r * 2);
  lv_obj_set_pos(o, CX - r, CY - r);
  lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_color(o, c(color), 0);
  lv_obj_set_style_border_width(o, width, 0);
  lv_obj_clear_flag(o, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  return o;
}

static void zeroDo(void *) { app::cmd("level_zero"); }
static void frontDo(void *) { app::cmd("level_front"); }
static void zeroCb(lv_event_t *) {
  confirm("Set level",
          "Is the van exactly level right now?\nCheck with a spirit level on the floor,\n"
          "then keep still for 2 seconds.",
          zeroDo);
}
static void frontCb(lv_event_t *) {
  confirm("Learn front",
          "Wind the jockey wheel UP so the front\nis 2-3 cm higher than level, then press Yes.\n"
          "(Only needed once, after Set level.)",
          frontDo);
}

void buildLevel(lv_obj_t *p) {
  lv_obj_t *cd = card(p, 12, 12, 412, 412, "BUBBLE LEVEL", col::ACCENT);
  // Van outline (top view) with A-frame pointing to the front
  lv_obj_t *van = lv_obj_create(cd);
  lv_obj_set_size(van, 150, 230);
  lv_obj_set_pos(van, CX - 75, CY - 100);
  lv_obj_set_style_radius(van, 18, 0);
  lv_obj_set_style_bg_color(van, c(col::TILE), 0);
  lv_obj_set_style_bg_opa(van, LV_OPA_50, 0);
  lv_obj_set_style_border_color(van, c(col::DIM), 0);
  lv_obj_set_style_border_width(van, 2, 0);
  lv_obj_clear_flag(van, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  static lv_point_t aframe[] = {{CX - 50, CY - 100}, {CX, CY - 150}, {CX + 50, CY - 100}};
  lv_obj_t *af = lv_line_create(cd);
  lv_line_set_points(af, aframe, 3);
  lv_obj_set_style_line_color(af, c(col::DIM), 0);
  lv_obj_set_style_line_width(af, 3, 0);

  ring(cd, 160, col::BORDER, 2);   // 3 deg
  ring(cd, 107, col::BORDER, 2);   // 2 deg
  ring(cd, 53, col::DIM, 2);       // 1 deg
  ring(cd, (int)(LEVEL_TOL * PX_PER_DEG) + 18, col::CHARGE, 2);  // level zone
  static lv_point_t hline[] = {{CX - 170, CY}, {CX + 170, CY}};
  static lv_point_t vline[] = {{CX, CY - 170}, {CX, CY + 170}};
  for (lv_point_t *pts : {hline, vline}) {
    lv_obj_t *l = lv_line_create(cd);
    lv_line_set_points(l, pts, 2);
    lv_obj_set_style_line_color(l, c(col::BORDER), 0);
    lv_obj_set_style_line_width(l, 1, 0);
  }
  lv_obj_t *fr = lbl(cd, &lv_font_montserrat_12, col::MUTED, LV_SYMBOL_UP " FRONT");
  lv_obj_set_pos(fr, CX + 8, 18);
  lv_obj_t *lt = lbl(cd, &lv_font_montserrat_12, col::MUTED, "LEFT\n(door)");
  lv_obj_set_pos(lt, 0, CY - 40);
  lv_obj_t *rt = lbl(cd, &lv_font_montserrat_12, col::MUTED, "RIGHT");
  lv_obj_set_pos(rt, 340, CY - 40);

  bubble = lv_obj_create(cd);
  lv_obj_set_size(bubble, 36, 36);
  lv_obj_set_pos(bubble, CX - 18, CY - 18);
  lv_obj_set_style_radius(bubble, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(bubble, 0, 0);
  lv_obj_set_style_bg_color(bubble, c(col::CHARGE), 0);
  lv_obj_set_style_shadow_width(bubble, 20, 0);
  lv_obj_set_style_shadow_color(bubble, c(col::CHARGE), 0);
  lv_obj_set_style_shadow_opa(bubble, LV_OPA_50, 0);
  lv_obj_clear_flag(bubble, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));

  lOffline = lbl(cd, &lv_font_montserrat_16, col::WARN, "Level sensor not connected");
  lv_obj_align(lOffline, LV_ALIGN_BOTTOM_MID, 0, 0);

  // ---------------- advice
  lv_obj_t *a = card(p, 436, 12, 264, 412, "ADJUST", col::ACCENT);
  lv_obj_t *c1 = lbl(a, &lv_font_montserrat_12, col::MUTED, "SIDE TO SIDE");
  lv_obj_set_pos(c1, 0, 24);
  lRoll = lbl(a, &lv_font_montserrat_24, col::TEXT, "--");
  lv_obj_set_pos(lRoll, 0, 40);
  lv_obj_t *c2 = lbl(a, &lv_font_montserrat_12, col::MUTED, "FRONT TO BACK");
  lv_obj_set_pos(c2, 0, 78);
  lPitch = lbl(a, &lv_font_montserrat_24, col::TEXT, "--");
  lv_obj_set_pos(lPitch, 0, 94);
  lAdvice = lbl(a, &lv_font_montserrat_20, col::TEXT, "");
  lv_obj_set_width(lAdvice, 236);
  lv_label_set_long_mode(lAdvice, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_line_space(lAdvice, 8, 0);
  lv_obj_set_pos(lAdvice, 0, 140);
  lStatus = lbl(a, &lv_font_montserrat_12, col::MUTED, "");
  lv_obj_set_width(lStatus, 236);
  lv_label_set_long_mode(lStatus, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(lStatus, 0, 250);
  lDims = lbl(a, &lv_font_montserrat_12, col::DIM, "");
  lv_obj_set_pos(lDims, 0, 300);
  lv_obj_t *bz = button(a, 114, 42, "Set level", col::TILE, zeroCb);
  lv_obj_align(bz, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_t *bf = button(a, 114, 42, "Learn front", col::TILE, frontCb);
  lv_obj_align(bf, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
}

void updateLevel() {
  const cv::Level &l = app::state.lvl;
  if (!l.online) {
    lv_obj_clear_flag(lOffline, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(bubble, LV_OBJ_FLAG_HIDDEN);
    setText(lRoll, "--");
    setText(lPitch, "--");
    setText(lAdvice, "");
    setText(lStatus, "Connect an MPU-6050 to the hub's I2C bus.");
    return;
  }
  lv_obj_add_flag(lOffline, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(bubble, LV_OBJ_FLAG_HIDDEN);

  // The bubble floats to the HIGH side, like a real spirit level.
  float bx = -l.roll * PX_PER_DEG, by = -l.pitch * PX_PER_DEG;
  float r = sqrtf(bx * bx + by * by);
  if (r > 160) {
    bx *= 160 / r;
    by *= 160 / r;
  }
  lv_obj_set_pos(bubble, CX - 18 + (int)bx, CY - 18 + (int)by);
  bool rollOk = fabsf(l.roll) <= LEVEL_TOL, pitchOk = fabsf(l.pitch) <= LEVEL_TOL;
  uint32_t bc = (rollOk && pitchOk) ? col::CHARGE : (fabsf(l.roll) < 1 && fabsf(l.pitch) < 1) ? col::WARN : col::ALARM;
  lv_obj_set_style_bg_color(bubble, c(bc), 0);
  lv_obj_set_style_shadow_color(bubble, c(bc), 0);

  if (rollOk) setText(lRoll, LV_SYMBOL_OK " %.1f\xC2\xB0", fabsf(l.roll));
  else setText(lRoll, "%.1f\xC2\xB0 %s low", fabsf(l.roll), l.roll < 0 ? "left" : "right");
  if (pitchOk) setText(lPitch, LV_SYMBOL_OK " %.1f\xC2\xB0", fabsf(l.pitch));
  else setText(lPitch, "%.1f\xC2\xB0 front %s", fabsf(l.pitch), l.pitch > 0 ? "high" : "low");
  lv_obj_set_style_text_color(lRoll, c(rollOk ? col::CHARGE : col::TEXT), 0);
  lv_obj_set_style_text_color(lPitch, c(pitchOk ? col::CHARGE : col::TEXT), 0);

  cv::LevelAngles ang;
  ang.pitchDeg = l.pitch;
  ang.rollDeg = l.roll;
  cv::LevelAdvice adv = cv::levelAdvice(ang, l.trackMm, l.jockeyMm);
  char a1[40], a2[40];
  if (rollOk) snprintf(a1, sizeof(a1), LV_SYMBOL_OK " Wheels level");
  else snprintf(a1, sizeof(a1), "%s wheel: up %d mm", adv.raiseLeft ? "Left" : "Right", (int)(adv.wheelMm + 0.5f));
  if (pitchOk) snprintf(a2, sizeof(a2), LV_SYMBOL_OK " Jockey level");
  else snprintf(a2, sizeof(a2), "Jockey: %s %d mm", adv.jockeyMm > 0 ? "up" : "down", (int)(fabsf(adv.jockeyMm) + 0.5f));
  setText(lAdvice, "%s\n%s", a1, a2);
  lv_obj_set_style_text_color(lAdvice, c(rollOk && pitchOk ? col::CHARGE : col::TEXT), 0);

  if (!l.zeroed) setText(lStatus, LV_SYMBOL_WARNING " Not calibrated. Park on level ground and press Set level.");
  else if (!l.fwdLearned) setText(lStatus, "Calibrated. Optional: Learn front if the sensor isn't square to the van. Do the wheels first, then the jockey.");
  else setText(lStatus, "Calibrated. Do the wheels first, then the jockey.");
  setText(lDims, "Wheels %u mm apart, jockey %u mm\nfrom axle (change in dashboard)", l.trackMm, l.jockeyMm);
}

}  // namespace ui
