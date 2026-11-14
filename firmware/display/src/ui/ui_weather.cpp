// Weather: compact current conditions in the status bar (visible on every
// page, including Home); tap it for the 4-day forecast.
#include "../app.h"
#include "ui.h"

LV_FONT_DECLARE(wx_icons_24);
LV_FONT_DECLARE(wx_icons_48);

namespace ui {

static lv_obj_t *wxBox, *wxIcon, *wxTemp, *wxSub, *wxOverlay;
static uint32_t wxShownUpdated = 0;
static const char *kDays[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

// Weather Icons glyphs live in the Unicode private-use area (U+F0xx).
static const char *glyph(uint16_t cp) {
  static char buf[4][4];
  static int n = 0;
  char *b = buf[n++ & 3];
  b[0] = (char)(0xE0 | (cp >> 12));
  b[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
  b[2] = (char)(0x80 | (cp & 0x3F));
  b[3] = 0;
  return b;
}

static uint32_t iconColor(uint8_t code, bool day) {
  if (code >= 95) return col::ALARM;
  if (code >= 51) return col::WATER;
  if (code <= 1) return day ? col::SOLAR : col::CLIMATE;
  return col::MUTED;
}

static void closeOverlay(lv_event_t *) {
  if (wxOverlay) lv_obj_del(wxOverlay);
  wxOverlay = nullptr;
}

static void openOverlay(lv_event_t *) {
  const cv::Weather &w = app::state.wx;
  if (!w.valid) {
    toast("No forecast yet. Set the location in the dashboard (Setup) or fit a GPS.", col::WARN);
    return;
  }
  closeOverlay(nullptr);
  wxOverlay = lv_obj_create(lv_layer_top());
  lv_obj_set_size(wxOverlay, 800, 480);
  lv_obj_set_pos(wxOverlay, 0, 0);
  lv_obj_set_style_bg_color(wxOverlay, c(col::BG), 0);
  lv_obj_set_style_bg_opa(wxOverlay, LV_OPA_80, 0);
  lv_obj_set_style_border_width(wxOverlay, 0, 0);
  lv_obj_add_event_cb(wxOverlay, closeOverlay, LV_EVENT_CLICKED, nullptr);

  char title[48];
  snprintf(title, sizeof(title), "FORECAST%s%s", w.place[0] ? "  -  " : "", w.place);
  lv_obj_t *cd = card(wxOverlay, 50, 60, 700, 360, title, col::WATER);
  lv_obj_add_flag(cd, LV_OBJ_FLAG_EVENT_BUBBLE);  // tap anywhere closes

  // Current conditions
  lv_obj_t *ic = lbl(cd, &wx_icons_48, iconColor(w.code, w.isDay), glyph(cv::wxIcon(w.code, w.isDay)));
  lv_obj_set_pos(ic, 0, 26);
  lv_obj_t *t = lbl(cd, &lv_font_montserrat_48, col::TEXT, "");
  setText(t, "%.0f\xC2\xB0", w.temp);
  lv_obj_set_pos(t, 80, 22);
  lv_obj_t *d = lbl(cd, &lv_font_montserrat_20, col::TEXT, cv::wxText(w.code));
  lv_obj_set_pos(d, 200, 30);
  lv_obj_t *wd = lbl(cd, &lv_font_montserrat_14, col::MUTED, "");
  setText(wd, "Wind %.0f km/h, gusts %.0f km/h", w.wind, w.gust);
  lv_obj_set_pos(wd, 200, 60);
  lv_obj_t *up = lbl(cd, &lv_font_montserrat_12, col::DIM, "");
  if (w.updated && app::state.ts) setText(up, "updated %u min ago  -  tap to close", (unsigned)((app::state.ts - w.updated) / 60));
  lv_obj_align(up, LV_ALIGN_TOP_RIGHT, 0, 0);

  // 4 days
  for (int i = 0; i < cv::WX_DAYS; i++) {
    const cv::DailyWx &dy = w.day[i];
    int x = i * 168;
    lv_obj_t *col_ = lv_obj_create(cd);
    lv_obj_set_size(col_, 158, 214);
    lv_obj_set_pos(col_, x, 106);
    lv_obj_set_style_bg_color(col_, c(col::TILE), 0);
    lv_obj_set_style_border_width(col_, 0, 0);
    lv_obj_set_style_radius(col_, 14, 0);
    lv_obj_set_style_pad_all(col_, 10, 0);
    lv_obj_clear_flag(col_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(col_, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_t *n = lbl(col_, &lv_font_montserrat_16, col::TEXT, i == 0 ? "Today" : (dy.wday >= 0 ? kDays[dy.wday] : "--"));
    lv_obj_align(n, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_t *di = lbl(col_, &wx_icons_48, iconColor(dy.code, true), glyph(cv::wxIcon(dy.code, true)));
    lv_obj_align(di, LV_ALIGN_TOP_MID, 0, 26);
    lv_obj_t *hl = lbl(col_, &lv_font_montserrat_20, col::TEXT, "");
    setText(hl, "%.0f\xC2\xB0 / %.0f\xC2\xB0", dy.tMax, dy.tMin);
    lv_obj_align(hl, LV_ALIGN_TOP_MID, 0, 96);
    lv_obj_t *rn = lbl(col_, &lv_font_montserrat_14, dy.rainProb >= 50 ? col::WATER : col::MUTED, "");
    setText(rn, "Rain %u%%  %.0f mm", dy.rainProb, dy.rainMm);
    lv_obj_align(rn, LV_ALIGN_TOP_MID, 0, 130);
    lv_obj_t *gs = lbl(col_, &lv_font_montserrat_14, dy.gustMax >= 50 ? col::WARN : col::MUTED, "");
    setText(gs, "Gusts %.0f km/h", dy.gustMax);
    lv_obj_align(gs, LV_ALIGN_TOP_MID, 0, 154);
  }
  wxShownUpdated = w.updated;
}

void openWeatherForTest() { openOverlay(nullptr); }

void buildWeatherWidget(lv_obj_t *bar) {
  wxBox = lv_obj_create(bar);
  lv_obj_set_size(wxBox, 196, 40);
  lv_obj_align(wxBox, LV_ALIGN_RIGHT_MID, -248, 0);
  lv_obj_set_style_bg_opa(wxBox, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(wxBox, 0, 0);
  lv_obj_set_style_pad_all(wxBox, 0, 0);
  lv_obj_clear_flag(wxBox, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(wxBox, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(wxBox, openOverlay, LV_EVENT_CLICKED, nullptr);
  wxIcon = lbl(wxBox, &wx_icons_24, col::MUTED, "");
  lv_obj_align(wxIcon, LV_ALIGN_LEFT_MID, 0, 2);
  wxTemp = lbl(wxBox, &lv_font_montserrat_20, col::TEXT, "");
  lv_obj_align(wxTemp, LV_ALIGN_LEFT_MID, 40, 2);
  wxSub = lbl(wxBox, &lv_font_montserrat_12, col::MUTED, "");
  lv_obj_align(wxSub, LV_ALIGN_LEFT_MID, 92, 2);
}

void updateWeather() {
  const cv::Weather &w = app::state.wx;
  if (!w.valid) {
    lv_obj_add_flag(wxBox, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_clear_flag(wxBox, LV_OBJ_FLAG_HIDDEN);
  setText(wxIcon, "%s", glyph(cv::wxIcon(w.code, w.isDay)));
  lv_obj_set_style_text_color(wxIcon, c(iconColor(w.code, w.isDay)), 0);
  setText(wxTemp, "%.0f\xC2\xB0", w.temp);
  setText(wxSub, LV_SYMBOL_UP "%.0f\xC2\xB0 " LV_SYMBOL_DOWN "%.0f\xC2\xB0\nRain %u%%", w.day[0].tMax, w.day[0].tMin,
          w.day[0].rainProb);
  if (wxOverlay && w.updated != wxShownUpdated) openOverlay(nullptr);  // refresh if open
}

}  // namespace ui
