#include "board.h"

#include <Wire.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <lvgl.h>

namespace {
constexpr int W = board::SCREEN_W, H = board::SCREEN_H;
constexpr int PIN_SDA = 8, PIN_SCL = 9;
constexpr uint8_t IO_ADDR = 0x24;
constexpr uint8_t REG_MODE = 0x02, REG_OUTPUT = 0x03;
// 0x05 = backlight PWM, INVERTED (0x00 brightest, 0xFF off). Never written at
// boot (see begin()); only once the screen first dims.
constexpr uint8_t REG_PWM = 0x05;
// How far "dimmed" goes. DISPLAY_DIM_PWM=0 switches the backlight fully off
// instead (no PWM writes at all), in case PWM brings back the stripe band.
#ifndef DISPLAY_DIM_PWM
#define DISPLAY_DIM_PWM 1
#endif
constexpr uint8_t PWM_DIM = 0xF0, PWM_FULL = 0x00;
constexpr int IO_BACKLIGHT = 2;  // IO3 = LCD reset: left high, never pulsed

esp_lcd_panel_handle_t gPanel = nullptr;
lv_disp_draw_buf_t gDrawBuf;
lv_disp_drv_t gDispDrv;
lv_indev_drv_t gIndevDrv;
uint8_t gExio = 0xff;
bool gBacklight = false;
bool gPwmUsed = false;      // PWM register written since boot
bool gSwallowTouch = false; // current touch woke the screen: don't pass it to LVGL
uint32_t gLastTouchMs = 0;
uint8_t gTouchAddr = 0;

bool exioWrite(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(IO_ADDR);
  Wire.write(reg);
  Wire.write(value);
  uint8_t err = Wire.endTransmission();
  if (err) Serial.printf("[board] IO extension write 0x%02X failed: %u\n", reg, err);
  return err == 0;
}

void exioPin(int pin, bool on) {
  if (on) gExio |= 1U << pin;
  else gExio &= ~(1U << pin);
  exioWrite(REG_OUTPUT, gExio);
}

bool gtRead(uint16_t reg, uint8_t *buf, uint8_t len) {
  if (!gTouchAddr) return false;
  Wire.beginTransmission(gTouchAddr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xff);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(gTouchAddr, len) != len) return false;
  for (uint8_t i = 0; i < len; ++i) buf[i] = Wire.read();
  return true;
}

void gtWrite(uint16_t reg, uint8_t value) {
  Wire.beginTransmission(gTouchAddr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xff);
  Wire.write(value);
  Wire.endTransmission();
}

bool touchPoint(int16_t &x, int16_t &y) {
  uint8_t status = 0;
  if (!gtRead(0x814e, &status, 1) || !(status & 0x80)) return false;
  bool pressed = (status & 0x0f) > 0;
  if (pressed) {
    uint8_t point[7];
    if (gtRead(0x8150, point, sizeof(point))) {
      x = point[0] | (point[1] << 8);
      y = point[2] | (point[3] << 8);
      if (x >= W || y >= H) pressed = false;
    } else {
      pressed = false;
    }
  }
  gtWrite(0x814e, 0);
  return pressed;
}

void flushCb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *pixels) {
  static bool first = true;
  esp_err_t err = esp_lcd_panel_draw_bitmap(gPanel, area->x1, area->y1,
                                           area->x2 + 1, area->y2 + 1, pixels);
  if (first || err != ESP_OK) {
    Serial.printf("[board] LVGL flush: %s (%d)\n", err == ESP_OK ? "OK" : "FAILED", err);
    first = false;
  }
  lv_disp_flush_ready(drv);
}

void touchCb(lv_indev_drv_t *, lv_indev_data_t *data) {
  static int16_t lastX = 0, lastY = 0;
  int16_t x = 0, y = 0;
  bool down = touchPoint(x, y);
  if (down) {
    lastX = x;
    lastY = y;
    gLastTouchMs = millis();
  }
  if (down && !gBacklight) {
    board::backlight(true);
    gSwallowTouch = true;
  }
  if (gSwallowTouch) {
    if (!down) gSwallowTouch = false;  // finger lifted: the next touch is a real one
    down = false;
  }
  data->point.x = lastX;
  data->point.y = lastY;
  data->state = down ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}
}  // namespace

#ifndef DISPLAY_PCLK_MHZ
#define DISPLAY_PCLK_MHZ 30  // ~33 Hz refresh. 21 MHz (~23 Hz) stopped tearing but flickered badly
#endif

namespace board {
void begin() {
  Serial.printf("[board] PSRAM: %u bytes, free internal heap: %u bytes\n", ESP.getPsramSize(), ESP.getFreeHeap());
  Wire.begin(PIN_SDA, PIN_SCL, 400000);

  // Same start-up as Waveshare's 06_LCD demo: CH32 pins to outputs (all high,
  // so backlight on, LCD out of reset and EXIO5 = CAN: GPIO19/20 go to the
  // CAN transceiver, not the native USB port) and nothing else. Pulsing LCD reset
  // (IO3) and writing the backlight PWM register here left a faint band of
  // vertical stripes across the panel; confirmed on hardware by A/B flash.
  bool ioOk = exioWrite(REG_MODE, 0xff);
  exioWrite(REG_OUTPUT, gExio);
  gBacklight = true;
  Serial.printf("[board] CH32 IO extension: %s\n", ioOk ? "OK" : "FAILED");

  esp_lcd_rgb_panel_config_t cfg = {};
  cfg.clk_src = LCD_CLK_SRC_DEFAULT;
  // Wi-Fi on this board (forecast, clock, cloud) shares the PSRAM bus with the
  // frame buffer: with it running the panel tore and flickered (2026-11-19).
  // Wi-Fi/lwIP buffers now stay in internal RAM (platformio.ini; TLS stays in
  // PSRAM: in internal RAM every HTTPS fetch failed),
  // bigger bounce buffers ride out bus stalls, and the DMA restarts at each
  // vsync so a missed refill can't leave the picture shifted.
  cfg.timings.pclk_hz = DISPLAY_PCLK_MHZ * 1000 * 1000;
  cfg.timings.h_res = W;
  cfg.timings.v_res = H;
  cfg.timings.hsync_pulse_width = 162;
  cfg.timings.hsync_back_porch = 152;
  cfg.timings.hsync_front_porch = 48;
  cfg.timings.vsync_pulse_width = 45;
  cfg.timings.vsync_back_porch = 13;
  cfg.timings.vsync_front_porch = 3;
  cfg.timings.flags.pclk_active_neg = 1;
  cfg.data_width = 16;
  cfg.bits_per_pixel = 16;
  cfg.num_fbs = 2;
  cfg.bounce_buffer_size_px = W * 30;  // 2 x 60 KB internal RAM (must divide 600 lines); 15 lines still tore with the cloud link up
  cfg.sram_trans_align = 4;
  cfg.psram_trans_align = 64;
  cfg.hsync_gpio_num = 46;
  cfg.vsync_gpio_num = 3;
  cfg.de_gpio_num = 5;
  cfg.pclk_gpio_num = 7;
  cfg.disp_gpio_num = -1;
  const int dataPins[16] = {14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40};
  for (int i = 0; i < 16; ++i) cfg.data_gpio_nums[i] = dataPins[i];
  cfg.flags.fb_in_psram = 1;
  esp_err_t err = esp_lcd_new_rgb_panel(&cfg, &gPanel);
  if (err == ESP_OK) err = esp_lcd_panel_init(gPanel);
  if (err != ESP_OK) {
    Serial.printf("[board] ERROR: RGB panel initialization failed: %d\n", err);
    while (true) delay(1000);
  }
  Serial.printf("[board] RGB panel initialized: %dx%d, %dMHz\n", W, H, DISPLAY_PCLK_MHZ);

  lv_init();
  // LVGL draw buffers in PSRAM: internal RAM goes to the panel's bounce
  // buffers instead (drawing in internal RAM didn't stop the tearing, and
  // internal RAM can't hold both - 939617a crash-looped).
  size_t n = W * 60;
  auto *b1 = static_cast<lv_color_t *>(heap_caps_malloc(n * sizeof(lv_color_t), MALLOC_CAP_SPIRAM));
  auto *b2 = static_cast<lv_color_t *>(heap_caps_malloc(n * sizeof(lv_color_t), MALLOC_CAP_SPIRAM));
  if (!b1 || !b2) {
    Serial.println("[board] ERROR: draw buffer allocation failed");
    while (true) delay(1000);
  }
  Serial.printf("[board] draw buffers %u lines, internal RAM left %u (largest %u)\n", (unsigned)(n / W),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  lv_disp_draw_buf_init(&gDrawBuf, b1, b2, n);
  lv_disp_drv_init(&gDispDrv);
  gDispDrv.hor_res = W;
  gDispDrv.ver_res = H;
  gDispDrv.flush_cb = flushCb;
  gDispDrv.draw_buf = &gDrawBuf;
  lv_disp_drv_register(&gDispDrv);

  for (uint8_t addr : {(uint8_t)0x5d, (uint8_t)0x14}) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      gTouchAddr = addr;
      break;
    }
  }
  Serial.printf("[touch] GT911 %s at 0x%02X\n", gTouchAddr ? "found" : "not found", gTouchAddr);
  lv_indev_drv_init(&gIndevDrv);
  gIndevDrv.type = LV_INDEV_TYPE_POINTER;
  gIndevDrv.read_cb = touchCb;
  lv_indev_drv_register(&gIndevDrv);
  gLastTouchMs = millis();
}

void backlight(bool on) {
  if (on == gBacklight) return;
  gBacklight = on;
#if DISPLAY_DIM_PWM
  if (!on) {
    exioWrite(REG_PWM, PWM_DIM);
    gPwmUsed = true;
  } else if (gPwmUsed) {
    exioWrite(REG_PWM, PWM_FULL);
  }
#else
  exioPin(IO_BACKLIGHT, on);
#endif
  if (on) gLastTouchMs = millis();
}
bool backlightOn() { return gBacklight; }
uint32_t lastTouchMs() { return gLastTouchMs; }
}  // namespace board