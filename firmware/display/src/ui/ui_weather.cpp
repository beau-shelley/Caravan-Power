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

// Current conditions + one column per day, built into `box` (w x h content
// area). Used by the tap-for-forecast overlay and the Climate page card.
// `bubble` lets taps on the children reach the overlay (tap anywhere closes).
void buildForecast(lv_obj_t *box, int w, int h, const char *note, bool bubble) {
  const cv::Weather &wx = app::state.wx;
  lv_obj_clean(box);
  if (!wx.valid) {
    lv_obj_t *m = lbl(box, LARGE ? &lv_font_montserrat_16 : &lv_font_montserrat_14, col::MUTED,
                      "No forecast yet.\nSet the location: More > Settings > Weather location,\n"
                      "or from the phone level page once the van is level.");
    lv_obj_set_style_text_align(m, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(m);
    return;
  }
  constexpr int top = 26;  // below the card caption
  lv_obj_t *ic = lbl(box, &wx_icons_48, iconColor(wx.code, wx.isDay), glyph(cv::wxIcon(wx.code, wx.isDay)));
  lv_obj_set_pos(ic, 0, top);
  lv_obj_t *t = lbl(box, &lv_font_montserrat_48, col::TEXT, "");
  setText(t, "%d\xC2\xB0", (int)lroundf(wx.temp));
  lv_obj_set_style_text_color(t, c(tempColor(cv::TEMP_OUTSIDE, wx.temp)), 0);
  lv_obj_set_pos(t, 80, top - 4);
  lv_obj_t *d = lbl(box, &lv_font_montserrat_20, col::TEXT, cv::wxText(wx.code));
  lv_obj_set_pos(d, 200, top + 4);
  lv_obj_t *wd = lbl(box, &lv_font_montserrat_14, col::MUTED, "");
  setText(wd, "Wind %.0f km/h, gusts %.0f km/h", wx.wind, wx.gust);
  lv_obj_set_pos(wd, 200, top + 34);
  lv_obj_t *up = lbl(box, &lv_font_montserrat_12, col::DIM, "");
  if (wx.updated && app::state.ts)
    setText(up, "updated %u min ago%s", (unsigned)((app::state.ts - wx.updated) / 60), note ? note : "");
  lv_obj_align(up, LV_ALIGN_TOP_RIGHT, 0, 0);

  const int dayTop = top + 80, dayGap = 10;
  const int dayW = (w - (cv::WX_DAYS - 1) * dayGap) / cv::WX_DAYS, dayH = h - dayTop;
  // Full block is 194 px tall; on short boxes (e.g. the Climate card on an
  // 800x480 panel) tighten the spacing and drop the lines that don't fit.
  const int avail = dayH - 20;
  const bool tight = avail < 194;
  const int yIcon = tight ? 16 : 26, yHl = tight ? 84 : 96, yRn = tight ? 112 : 132, yGs = tight ? 134 : 158;
  const int blockH = tight ? (avail >= 154 ? 154 : avail >= 132 ? 132 : 106) : 194;
  const int y0 = (avail - blockH) / 2 > 0 ? (avail - blockH) / 2 : 0;  // centre the block
  for (int i = 0; i < cv::WX_DAYS; i++) {
    const cv::DailyWx &dy = wx.day[i];
    lv_obj_t *col_ = lv_obj_create(box);
    lv_obj_set_size(col_, dayW, dayH);
    lv_obj_set_pos(col_, i * (dayW + dayGap), dayTop);
    lv_obj_set_style_bg_color(col_, c(col::TILE), 0);
    lv_obj_set_style_border_width(col_, 0, 0);
    lv_obj_set_style_radius(col_, 14, 0);
    lv_obj_set_style_pad_all(col_, 10, 0);
    lv_obj_clear_flag(col_, LV_OBJ_FLAG_SCROLLABLE);
    if (bubble) lv_obj_add_flag(col_, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_t *n = lbl(col_, &lv_font_montserrat_16, col::TEXT, i == 0 ? "Today" : (dy.wday >= 0 ? kDays[dy.wday] : "--"));
    lv_obj_align(n, LV_ALIGN_TOP_MID, 0, y0);
    lv_obj_t *di = lbl(col_, &wx_icons_48, iconColor(dy.code, true), glyph(cv::wxIcon(dy.code, true)));
    lv_obj_align(di, LV_ALIGN_TOP_MID, 0, y0 + yIcon);
    lv_obj_t *hl = lbl(col_, LARGE ? &lv_font_montserrat_24 : &lv_font_montserrat_20, col::TEXT, "");
    lv_label_set_recolor(hl, true);
    setText(hl, "#%06x %d\xC2\xB0# / #%06x %d\xC2\xB0#", (unsigned)tempColor(cv::TEMP_OUTSIDE, dy.tMax),
            (int)lroundf(dy.tMax), (unsigned)tempColor(cv::TEMP_OUTSIDE, dy.tMin), (int)lroundf(dy.tMin));
    lv_obj_align(hl, LV_ALIGN_TOP_MID, 0, y0 + yHl);
    if (blockH < 132) continue;
    lv_obj_t *rn = lbl(col_, LARGE ? &lv_font_montserrat_16 : &lv_font_montserrat_14, dy.rainProb >= 50 ? col::WATER : col::MUTED, "");
    setText(rn, "Rain %u%%  %.0f mm", dy.rainProb, dy.rainMm);
    lv_obj_align(rn, LV_ALIGN_TOP_MID, 0, y0 + yRn);
    if (blockH < 154) continue;
    lv_obj_t *gs = lbl(col_, LARGE ? &lv_font_montserrat_16 : &lv_font_montserrat_14, dy.gustMax >= 50 ? col::WARN : col::MUTED, "");
    setText(gs, "Gusts %.0f km/h", dy.gustMax);
    lv_obj_align(gs, LV_ALIGN_TOP_MID, 0, y0 + yGs);
  }
}

static void openOverlay(lv_event_t *) {
  const cv::Weather &w = app::state.wx;
  if (!w.valid) {
    toast("No forecast yet. Set the location: Settings > Weather location.", col::WARN);
    return;
  }
  closeOverlay(nullptr);
  wxOverlay = lv_obj_create(lv_layer_top());
  lv_obj_set_size(wxOverlay, SCR_W, SCR_H);
  lv_obj_set_pos(wxOverlay, 0, 0);
  lv_obj_set_style_bg_color(wxOverlay, c(col::BG), 0);
  lv_obj_set_style_bg_opa(wxOverlay, LV_OPA_80, 0);
  lv_obj_set_style_border_width(wxOverlay, 0, 0);
  lv_obj_add_event_cb(wxOverlay, closeOverlay, LV_EVENT_CLICKED, nullptr);

  char title[48];
  snprintf(title, sizeof(title), "FORECAST%s%s", w.place[0] ? "  -  " : "", w.place);
  constexpr int cw = SCR_W - (LARGE ? 120 : 100), ch = LARGE ? 400 : 360;
  lv_obj_t *cd = card(wxOverlay, (SCR_W - cw) / 2, (SCR_H - ch) / 2, cw, ch, title, col::WATER);
  lv_obj_add_flag(cd, LV_OBJ_FLAG_EVENT_BUBBLE);  // tap anywhere closes
  lv_obj_t *box = panel(cd, 0, 0, innerOf(cw), innerOf(ch) + 4);
  lv_obj_add_flag(box, LV_OBJ_FLAG_EVENT_BUBBLE);
  buildForecast(box, innerOf(cw), innerOf(ch) + 4, "  -  tap to close", true);
  wxShownUpdated = w.updated;
}

void openWeatherForTest() { openOverlay(nullptr); }

void buildWeatherWidget(lv_obj_t *bar) {
  wxBox = lv_obj_create(bar);
  lv_obj_set_size(wxBox, WX_W, BAR_H - 4);
  lv_obj_align(wxBox, LV_ALIGN_RIGHT_MID, -WX_RIGHT_OFS, 0);
  lv_obj_set_style_bg_opa(wxBox, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(wxBox, 0, 0);
  lv_obj_set_style_pad_all(wxBox, 0, 0);
  lv_obj_clear_flag(wxBox, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(wxBox, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(wxBox, openOverlay, LV_EVENT_CLICKED, nullptr);
  wxIcon = lbl(wxBox, &wx_icons_24, col::MUTED, "");
  lv_obj_align(wxIcon, LV_ALIGN_LEFT_MID, 0, 2);
  wxTemp = lbl(wxBox, LARGE ? &lv_font_montserrat_24 : &lv_font_montserrat_20, col::TEXT, "");
  lv_obj_align(wxTemp, LV_ALIGN_LEFT_MID, 40, 2);
  wxSub = lbl(wxBox, LARGE ? &lv_font_montserrat_14 : &lv_font_montserrat_12, col::MUTED, "");
  lv_obj_align(wxSub, LV_ALIGN_LEFT_MID, LARGE ? 100 : 92, 2);
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
  setText(wxTemp, "%d\xC2\xB0", (int)lroundf(w.temp));
  lv_obj_set_style_text_color(wxTemp, c(tempColor(cv::TEMP_OUTSIDE, w.temp)), 0);
  lv_label_set_recolor(wxSub, true);
  setText(wxSub, LV_SYMBOL_UP "#%06x %d\xC2\xB0# " LV_SYMBOL_DOWN "#%06x %d\xC2\xB0#\nRain %u%%",
          (unsigned)tempColor(cv::TEMP_OUTSIDE, w.day[0].tMax), (int)lroundf(w.day[0].tMax),
          (unsigned)tempColor(cv::TEMP_OUTSIDE, w.day[0].tMin), (int)lroundf(w.day[0].tMin), w.day[0].rainProb);
  if (wxOverlay && w.updated != wxShownUpdated) openOverlay(nullptr);  // refresh if open
}

}  // namespace ui
