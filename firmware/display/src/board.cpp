#include "board.h"

#include <LovyanGFX.hpp>
#include <esp_heap_caps.h>
#include <Wire.h>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lvgl.h>

namespace {

constexpr int W = 800, H = 480;
constexpr int PIN_SDA = 8, PIN_SCL = 9, PIN_TP_INT = 4;

// ---------------------------------------------------------------- panel
class LGFX : public lgfx::LGFX_Device {
 public:
  lgfx::Bus_RGB bus;
  lgfx::Panel_RGB panel;
  LGFX() {
    {
      auto cfg = panel.config();
      cfg.memory_width = cfg.panel_width = W;
      cfg.memory_height = cfg.panel_height = H;
      cfg.offset_x = cfg.offset_y = 0;
      panel.config(cfg);
    }
    {
      auto cfg = panel.config_detail();
      cfg.use_psram = 1;
      panel.config_detail(cfg);
    }
    {
      auto cfg = bus.config();
      cfg.panel = &panel;
      // RGB565: d0-d4 = B3..B7, d5-d10 = G2..G7, d11-d15 = R3..R7
      cfg.pin_d0 = GPIO_NUM_14;
      cfg.pin_d1 = GPIO_NUM_38;
      cfg.pin_d2 = GPIO_NUM_18;
      cfg.pin_d3 = GPIO_NUM_17;
      cfg.pin_d4 = GPIO_NUM_10;
      cfg.pin_d5 = GPIO_NUM_39;
      cfg.pin_d6 = GPIO_NUM_0;
      cfg.pin_d7 = GPIO_NUM_45;
      cfg.pin_d8 = GPIO_NUM_48;
      cfg.pin_d9 = GPIO_NUM_47;
      cfg.pin_d10 = GPIO_NUM_21;
      cfg.pin_d11 = GPIO_NUM_1;
      cfg.pin_d12 = GPIO_NUM_2;
      cfg.pin_d13 = GPIO_NUM_42;
      cfg.pin_d14 = GPIO_NUM_41;
      cfg.pin_d15 = GPIO_NUM_40;
      cfg.pin_henable = GPIO_NUM_5;
      cfg.pin_vsync = GPIO_NUM_3;
      cfg.pin_hsync = GPIO_NUM_46;
      cfg.pin_pclk = GPIO_NUM_7;
      cfg.freq_write = 16000000;
      cfg.hsync_polarity = 0;
      cfg.hsync_front_porch = 8;
      cfg.hsync_pulse_width = 4;
      cfg.hsync_back_porch = 8;
      cfg.vsync_polarity = 0;
      cfg.vsync_front_porch = 8;
      cfg.vsync_pulse_width = 4;
      cfg.vsync_back_porch = 8;
      cfg.pclk_active_neg = 1;
      cfg.de_idle_high = 0;
      cfg.pclk_idle_high = 0;
      bus.config(cfg);
    }
    panel.setBus(&bus);
    setPanel(&panel);
  }
};
LGFX gfx;

// ---------------------------------------------------------------- CH422G
// I2C "registers" are separate 7-bit addresses. EXIO1 = touch reset,
// EXIO2 = backlight, EXIO3 = LCD reset, EXIO4 = SD CS, EXIO5 = USB/CAN select.
constexpr uint8_t CH422G_SET = 0x24, CH422G_IO = 0x38;
uint8_t gExio = 0;
bool gBacklight = false;

void exioWrite() {
  Wire.beginTransmission(CH422G_IO);
  Wire.write(gExio);
  Wire.endTransmission();
}
void exioPin(int n, bool v) {
  if (v) gExio |= (1 << n);
  else gExio &= ~(1 << n);
  exioWrite();
}

// ---------------------------------------------------------------- GT911
uint8_t gTouchAddr = 0x5D;
uint32_t gLastTouchMs = 0;

bool gtRead(uint16_t reg, uint8_t *buf, uint8_t n) {
  Wire.beginTransmission(gTouchAddr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom((int)gTouchAddr, (int)n) != n) return false;
  for (int i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}
void gtWrite(uint16_t reg, uint8_t v) {
  Wire.beginTransmission(gTouchAddr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  Wire.write(v);
  Wire.endTransmission();
}

void touchReset() {
  // INT held low during reset selects address 0x5D.
  pinMode(PIN_TP_INT, OUTPUT);
  digitalWrite(PIN_TP_INT, LOW);
  exioPin(1, false);
  delay(10);
  exioPin(1, true);
  delay(60);
  pinMode(PIN_TP_INT, INPUT);
  for (uint8_t a : {(uint8_t)0x5D, (uint8_t)0x14}) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      gTouchAddr = a;
      break;
    }
  }
  uint8_t id[4] = {0};
  gtRead(0x8140, id, 4);
  Serial.printf("[touch] GT%c%c%c%c at 0x%02X\n", id[0], id[1], id[2], id[3], gTouchAddr);
}

bool touchPoint(int16_t &x, int16_t &y) {
  uint8_t st;
  if (!gtRead(0x814E, &st, 1)) return false;
  if (!(st & 0x80)) return false;  // buffer not ready
  bool pressed = false;
  if ((st & 0x0F) > 0) {
    uint8_t d[7];
    if (gtRead(0x814F, d, 7)) {
      x = d[1] | (d[2] << 8);
      y = d[3] | (d[4] << 8);
      pressed = true;
    }
  }
  gtWrite(0x814E, 0);  // release the buffer
  return pressed;
}

// ---------------------------------------------------------------- LVGL glue
lv_disp_draw_buf_t gDrawBuf;
lv_disp_drv_t gDispDrv;
lv_indev_drv_t gIndevDrv;

void flushCb(lv_disp_drv_t *drv, const lv_area_t *a, lv_color_t *px) {
  gfx.pushImage(a->x1, a->y1, a->x2 - a->x1 + 1, a->y2 - a->y1 + 1, (lgfx::rgb565_t *)&px->full);
  lv_disp_flush_ready(drv);
}

void touchCb(lv_indev_drv_t *, lv_indev_data_t *data) {
  static int16_t lx = 0, ly = 0;
  static bool down = false;
  static uint32_t lastPoll = 0;
  // GT911 updates ~100 Hz; polling faster just loads the I2C bus.
  if (millis() - lastPoll >= 15) {
    lastPoll = millis();
    int16_t x, y;
    down = touchPoint(x, y);
    if (down) {
      lx = constrain(x, 0, W - 1);
      ly = constrain(y, 0, H - 1);
      gLastTouchMs = millis();
    }
  }
  // While the screen is asleep the first touch only wakes it.
  if (down && !gBacklight) {
    board::backlight(true);
    data->state = LV_INDEV_STATE_REL;
    return;
  }
  data->point.x = lx;
  data->point.y = ly;
  data->state = down ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}

}  // namespace

namespace board {

void begin() {
  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  Wire.beginTransmission(CH422G_SET);
  Wire.write(0x01);  // IO0-7 as outputs
  Wire.endTransmission();
  gExio = 0;
  exioWrite();
  delay(10);
  exioPin(3, true);  // LCD out of reset
  touchReset();

  gfx.init();
  gfx.fillScreen(TFT_BLACK);
  backlight(true);

  lv_init();
  // Two partial buffers in internal RAM (fast) - 800 x 40 lines each.
  const size_t n = W * 40;
  auto *b1 = (lv_color_t *)heap_caps_malloc(n * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  auto *b2 = (lv_color_t *)heap_caps_malloc(n * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  lv_disp_draw_buf_init(&gDrawBuf, b1, b2, n);
  lv_disp_drv_init(&gDispDrv);
  gDispDrv.hor_res = W;
  gDispDrv.ver_res = H;
  gDispDrv.flush_cb = flushCb;
  gDispDrv.draw_buf = &gDrawBuf;
  lv_disp_drv_register(&gDispDrv);

  lv_indev_drv_init(&gIndevDrv);
  gIndevDrv.type = LV_INDEV_TYPE_POINTER;
  gIndevDrv.read_cb = touchCb;
  lv_indev_drv_register(&gIndevDrv);
  gLastTouchMs = millis();
}

void backlight(bool on) {
  gBacklight = on;
  exioPin(2, on);
  if (on) gLastTouchMs = millis();
}

bool backlightOn() { return gBacklight; }
uint32_t lastTouchMs() { return gLastTouchMs; }

}  // namespace board
