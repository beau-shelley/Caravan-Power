# Waveshare ESP32-S3-Touch-LCD-7B — Handoff for a new project

Everything learned getting the "Canopy Power" panel running well on this exact board.
Written so a fresh Claude Code session can start a new caravan power project without
re-discovering any of it. **Read sections 0, 1 and 9 first** — they prevent the costliest mistakes.

Source of truth for all code below: `C:\Users\beaus\OneDrive\canopy-panel` (git repo,
branch `ready-for-release`, commit `b6745d0`, pushed to
`https://github.com/BeauShelley-534/canopy-panel`). Where this document and that repo
disagree, the repo wins — it is the tested artifact.

---

## 0. Instructions to the new Claude session

- **Copy, don't re-derive.** The board-support layer (`lib/board_bsp/*`, `include/pins.h`,
  `include/lv_conf.h`, `src/sensors/i2c_bus.*`, `platformio.ini`, `partitions.csv`) is verified on
  hardware. Copy those files from the repo above into the new project as the starting point.
- **Do not trust generic ESP32-S3 / Waveshare docs for this board.** Several widely-repeated
  assumptions are wrong for this revision (section 2). The vendor *example code* is right; the
  vendor *docs* are not.
- **Verify display changes on the physical screen, not only in serial logs.** Two display
  "fixes" in the previous project looked perfect in serial metrics and were wrong or harmful.
  Ask the user for a photo, or have them confirm visually, before calling any display change done.
- **Change one display-affecting thing at a time**, flash, and look at the screen between changes.
- **Always `esptool verify-flash` after flashing** (section 8). PlatformIO's own "Hash of data
  verified" only confirms the write.
- The user has one physical board per project. Don't iterate structural display changes on it
  blind — see the failed attempts in section 6.

---

## 1. Hardware summary

| Item | Value |
|---|---|
| Board | Waveshare ESP32-S3-Touch-LCD-7B |
| MCU | ESP32-S3, 16MB flash (quad), 8MB OPI PSRAM, 240MHz |
| Display | 7", 1024x600, 16-bit RGB565 parallel, IPS. No vendor init sequence needed |
| Touch | GT911 capacitive, I2C |
| USB ports | **Two.** UART/CH343 port shows up as **COM7** ("USB-Enhanced-SERIAL CH343") and carries `Serial` logs + flashing. The native-USB port shows as COM6 (VID:PID `303A:1001`) and gives **no log output** with this build config. Plugged into the wrong one = silent board |
| IO expander | **CH32V003 MCU @ I2C 0x24** running Waveshare's "IO_EXTENSION" firmware — NOT a CH422G/TCA9554 (section 2) |
| Other onboard | microSD (SD_MMC 1-bit), CAN (TWAI), RS485, RTC interrupt line |

**There are NO free native GPIOs.** Every usable GPIO is consumed by the RGB bus, shared I2C,
SD_MMC, CAN and RS485. GPIO26-37 are PSRAM/flash, 43/44 are the USB-serial console. Anything extra
(relays, sensors, PIR) must hang off the shared I2C bus. The previous project added:

- MCP23017 @ 0x20 — 8 relay outputs (GPA0-7, active-low via opto board) + PIR input (GPB0)
- ADS1115 @ 0x4A — battery voltage, tank sender, pressure transducer, total-current shunt

The onboard IO expander has three spare pins: IO0, IO6, IO7 (`FREE_0/6/7`) — unused, available.

---

## 2. Things that are wrong in the docs / that cost hours

### 2.1 The IO expander is a CH32V003, not a CH422G
It answers at 0x24 (the address a CH422G would use), so I2C scans and generic-expander writes all
ACK — but the register map is completely different. Symptom of getting this wrong: backlight
flickers on briefly then dies; single-register writes work inconsistently. This burned ~2 hours.
Confirmed protocol (cross-checked against ESPHome's merged `waveshare_io_ch32v003` driver,
esphome/esphome PR #10071, and issue esphome/feature-requests#3202):

| Reg | Meaning |
|---|---|
| `0x02` | direction (1 = output) |
| `0x03` | output register: bit1 = touch reset, bit2 = **backlight enable**, bit3 = LCD reset |
| `0x04` | input register (read) |
| `0x05` | **PWM backlight, INVERTED**: `0x00` = brightest, high = darker, `0xFF` = fully off |
| `0x06` | ADC (battery monitor) |
| `0x07` | RTC interrupt status |

Do **not** use the `ESP32_IO_Expander` Arduino library's CH422G class — it triggers an
"i2c driver_ng conflict" abort on ESP32 Arduino core 3.x. Plain `Wire` writes are simpler and reliable.

### 2.2 GT911 touch data layout differs from the usual description
Raw-byte capture on real hardware showed `REG_POINT0` (0x8150) has **no leading track-ID byte**.
X is `point[0] | point[1]<<8`, Y is `point[2] | point[3]<<8`. The "track_id-first" layout produces
values up to 64000+. Also: **cache the last pressed coordinate and report it on release** — otherwise
release events report (0,0) and any LVGL widget that reads position at release time misbehaves
(dropdown selection always chose the last option; root-caused via live serial).

### 2.3 Pixel clock: 30MHz, not 16MHz
The original Arduino_GFX build ran 16MHz and had a persistent subtle shimmer that survived every
software test (PCLK tweaks, bounce size, task isolation, backlight sweep, even a static fill with
no app code running). It was misdiagnosed as electrical ripple. An **A/B flash of Waveshare's own
unmodified demo** (`examples/Arduino/examples/06_LCD` in
github.com/waveshareteam/ESP32-S3-Touch-LCD-7B) showed it clean on the same unit — the difference
was 30MHz. Lesson: before concluding "it must be hardware", flash the vendor's reference firmware.
Note `pins.h` still contains an unused `LCD_PIXEL_CLOCK_HZ = 30.85MHz` constant; the verified
driver uses the literal `30 * 1000 * 1000`. Ignore the constant.

### 2.4 `bounce_buffer_size_px` is required, and has a hard ceiling
Left at 0 in the Arduino_GFX build: continuous left-to-right tearing. `LCD_H_RES * 10` (10240)
fixes it. **`*15` and `*20` blank the display entirely** — `*10` sits at the edge of a cliff.
Do not raise it.

### 2.5 CPU frequency scaling destroys the display (the big one)
The previous project's biggest bug: `setCpuFrequencyMhz(80)` on a dim/sleep power state. It caused
(a) `lv_timer_handler()` to go from ~44ms to ~287ms — a 6.5x slowdown from a 3x clock cut, because
code AND rodata run from PSRAM (`SPIRAM_FETCH_INSTRUCTIONS`/`SPIRAM_RODATA`) and LVGL's draw buffer
is in PSRAM, so every fetch/pixel write pays a far worse cache-miss penalty; and (b) the clock
*transition* desyncs the RGB DMA scan-out, leaving the framebuffer visibly offset (logo clipped at
top, reappearing at bottom) until reboot.
**Pin the CPU at 240MHz in every power state.** Dim the backlight instead — the backlight dwarfs
any CPU saving. It fired exactly 30s after boot (default dim timeout) and, with no PIR wired,
never recovered — which is why it looked like a leak for several sessions.

### 2.6 Flash writes corrupt the display
ESP32-S3 flash and PSRAM share one SPI bus. Any flash write (NVS / LittleFS save) starves the RGB
panel's DMA, which reads its framebuffer from PSRAM. Mitigations in use:
1. `CONFIG_SPIRAM_XIP_FROM_PSRAM=y` + `SPIRAM_FETCH_INSTRUCTIONS` + `SPIRAM_RODATA` (section 4) —
   keeps the CPU running during a write. Documented as not guaranteed to fully eliminate it.
2. `blankScreen()` / `unblankScreen()` around every config save, with a **400ms** settle delay
   after unblanking (150ms was not enough under load).
3. `WiFi.persistent(false)` — the ESP-IDF WiFi driver otherwise writes its own state to NVS during
   active phone traffic.
4. Remove avoidable flash-write sources (e.g. don't persist on every toggle).

### 2.7 No bus-wide thread safety in `Wire`
GT911, the IO expander, MCP23017 and ADS1115 share one bus and are touched from several FreeRTOS
tasks. Every I2C transaction must hold a shared mutex (`sensors::I2cBusGuard`, section 5). The
touch poll must use a **bounded wait** (1 tick) and skip that poll on contention — a blocking wait
there stalls the whole LVGL render pipeline for the duration of an ADC burst and feels like input lag.

---

## 3. Display driver (verified, committed)

Raw ESP-IDF `esp_lcd_new_rgb_panel`, **not** Arduino_GFX. Config copied verbatim from Waveshare's
working demo. Do not "improve" these values without a hardware A/B.

```cpp
esp_lcd_rgb_panel_config_t panelConfig = {
    .clk_src = LCD_CLK_SRC_DEFAULT,
    .timings = {
        .pclk_hz = 30 * 1000 * 1000,          // 30MHz — see 2.3
        .h_res = 1024, .v_res = 600,
        .hsync_pulse_width = 162, .hsync_back_porch = 152, .hsync_front_porch = 48,
        .vsync_pulse_width = 45,  .vsync_back_porch = 13,  .vsync_front_porch = 3,
        .flags = { .pclk_active_neg = 1 },
    },
    .data_width = 16,
    .bits_per_pixel = 16,
    .num_fbs = 2,                              // see 6.1 — does NOT give real double buffering here
    .bounce_buffer_size_px = 1024 * 10,        // do not raise — see 2.4
    .sram_trans_align = 4,
    .psram_trans_align = 64,
    .hsync_gpio_num = 46, .vsync_gpio_num = 3, .de_gpio_num = 5, .pclk_gpio_num = 7,
    .disp_gpio_num = -1,
    .data_gpio_nums = {                         // D0..D15 = B3-B7, G2-G7, R3-R7
        14, 38, 18, 17, 10,   39, 0, 45, 48, 47, 21,   1, 2, 42, 41, 40,
    },
    .flags = { .fb_in_psram = 1 },
};
ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panelConfig, &panelHandle));
ESP_ERROR_CHECK(esp_lcd_panel_init(panelHandle));
```

LVGL side: LVGL renders into two **60-line partial** PSRAM buffers; `flushCb` forwards each
dirty rect to `esp_lcd_panel_draw_bitmap()` and calls `lv_disp_flush_ready()`.

```cpp
constexpr int DRAW_BUF_LINES = 60;
size_t bufPixels = 1024 * DRAW_BUF_LINES;
lv_color_t* buf1 = (lv_color_t*)heap_caps_malloc(bufPixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
lv_color_t* buf2 = (lv_color_t*)heap_caps_malloc(bufPixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
lv_disp_draw_buf_init(&drawBuf, buf1, buf2, bufPixels);

lv_disp_drv_init(&dispDrv);
dispDrv.hor_res = 1024;  dispDrv.ver_res = 600;
dispDrv.flush_cb = flushCb;
dispDrv.draw_buf = &drawBuf;
lv_disp_drv_register(&dispDrv);

void flushCb(lv_disp_drv_t* drv, const lv_area_t* a, lv_color_t* c) {
  esp_lcd_panel_draw_bitmap(panelHandle, a->x1, a->y1, a->x2 + 1, a->y2 + 1, (const uint16_t*)c);
  lv_disp_flush_ready(drv);
}
```

**Boot order matters** (mirrors Waveshare's own init): `Serial` → `Wire.begin(8, 9, 400000)` →
`i2cBusInit()` → IO-expander `begin()` (resets + backlight) → *then* `bspDisplay().begin()` →
`bspTouch().begin()` → `ui::build()` → tasks. The IO expander must come up before the RGB panel and
GT911.

---

## 4. `platformio.ini` (working)

```ini
[env:your-project]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip
board = esp32-s3-devkitc-1
; stock devkitc-1 profile assumes the 8MB no-PSRAM N8 variant - override or uploadfs overflows
board_upload.flash_size = 16MB
board_upload.maximum_size = 16777216
; plain arduino - an "espidf, arduino" hybrid was tried and abandoned
framework = arduino
board_build.mcu = esp32s3
board_build.f_cpu = 240000000L
board_build.psram_type = opi
board_build.arduino.memory_type = qio_opi
board_build.partitions = partitions.csv
board_build.filesystem = littlefs
monitor_speed = 115200
upload_speed = 921600

; XIP from PSRAM - keeps the CPU running during flash writes (see 2.6).
; pioarduino-specific: recompiles the Arduino libs with these sdkconfig overrides.
; (FMB_MASTER line: see the note below this block)
custom_sdkconfig =
    CONFIG_SPIRAM_XIP_FROM_PSRAM=y
    CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y
    CONFIG_FMB_MASTER_MAX_API_BLOCKING_TIME_MS=12000
    CONFIG_SPIRAM_RODATA=y

build_flags =
    -DBOARD_HAS_PSRAM
    -DCORE_DEBUG_LEVEL=3
    -DLV_CONF_INCLUDE_SIMPLE
    -DLV_LVGL_H_INCLUDE_SIMPLE
    -Iinclude
    -Isrc
    -std=gnu++17
build_unflags = -std=gnu++11

lib_deps =
    lvgl/lvgl @ ^8.3.11
    ESP32Async/ESPAsyncWebServer @ ^3.6.0
    ESP32Async/AsyncTCP @ ^3.3.2
    bblanchon/ArduinoJson @ ^7.2.1
    adafruit/Adafruit ADS1X15 @ ^2.5.0
    adafruit/Adafruit BusIO @ ^1.16.1
    adafruit/Adafruit MCP23017 Arduino Library @ ^2.3.2
```

Notes:
- `GFX Library for Arduino` was in the old `lib_deps` but is no longer used by the display driver.
  Drop it in a new project.
- `FMB_MASTER_MAX_API_BLOCKING_TIME_MS=12000`: `custom_sdkconfig` recomputes the whole sdkconfig and
  landed on a combination that fails esp-modbus's own `static_assert`. Any consistent values work;
  the project never calls that component. If you later *use* esp-modbus, revisit both timeout values.
- `partitions.csv` (single app, no OTA; nvs/littlefs offsets stable if OTA is added later):
  ```
  nvs,      data, nvs,     0x9000,   0x6000,
  phy_init, data, phy,     0xF000,   0x1000,
  app0,     app,  factory, 0x10000,  0x600000,
  littlefs, data, spiffs,  0x610000, 0x9F0000,
  ```
- **Generated / gitignored — do not copy:** `sdkconfig.defaults`, `sdkconfig.<env>`, `managed_components/`,
  `.dummy/`. **Tracked but auto-generated by the build:** root `CMakeLists.txt`, `src/CMakeLists.txt`,
  `dependencies.lock`. They appear on the first build; don't hand-write them.

---

## 5. Board-support code to copy

Copy these files verbatim from the repo (all verified on hardware):

| File | Purpose |
|---|---|
| `include/pins.h` | Full pin map. The `board::` namespace is the board's fixed wiring; `ext::` is this project's added I2C parts — replace for a new project |
| `include/lv_conf.h` | LVGL 8.3 config |
| `lib/board_bsp/bsp_display.{h,cpp}` | RGB panel + LVGL registration |
| `lib/board_bsp/bsp_ioexpander.{h,cpp}` | CH32V003 driver: backlight, resets |
| `lib/board_bsp/bsp_touch.{h,cpp}` | GT911 + LVGL indev |
| `src/sensors/i2c_bus.{h,cpp}` | Shared I2C mutex |

### IO-expander driver (core logic)

```cpp
constexpr uint8_t REG_MODE = 0x02, REG_OUTPUT = 0x03, REG_PWM = 0x05;   // addr 0x24

bool begin() {
  writeReg(REG_MODE, 0xFF);                 // all outputs
  lastOutputValue_ = 0xFF;  writeReg(REG_OUTPUT, 0xFF);   // everything high first
  setPin(LCD_RST /*bit3*/, false);  delay(10);            // pulse LCD reset
  setPin(LCD_RST, true);            delay(120);           // settle
  setBacklightPercent(100);
}
void setPin(uint8_t pin, bool high) {       // read-modify-write on a cached byte
  if (high) lastOutputValue_ |= (1 << pin); else lastOutputValue_ &= ~(1 << pin);
  writeReg(REG_OUTPUT, lastOutputValue_);
}
void setBacklightPercent(uint8_t pct) {     // 0 = off, 100 = brightest
  uint8_t inverted = 100 - min<uint8_t>(pct, 100);
  writeReg(REG_PWM, (uint8_t)(inverted * (255.0f / 100.0f)));   // register is INVERTED
}
// Every writeReg() takes sensors::I2cBusGuard first.
```
`backlightOn()` is `setPin(BACKLIGHT /*bit2*/, true)`. `percent = 0` maps to `0xFF` (full off) — intended
for a SLEEP state; Waveshare's own library caps near `0xF7` to avoid full shutdown, so use a small
non-zero floor if you never want the screen fully dark.

### Shared I2C mutex

```cpp
// i2c_bus.cpp
static SemaphoreHandle_t i2cMutex;
void i2cBusInit() { i2cMutex = xSemaphoreCreateMutex(); }
I2cBusGuard::I2cBusGuard()                      { xSemaphoreTake(i2cMutex, portMAX_DELAY); }
I2cBusGuard::I2cBusGuard(TickType_t timeout)    { locked_ = xSemaphoreTake(i2cMutex, timeout) == pdTRUE; }
I2cBusGuard::~I2cBusGuard()                     { if (locked_) xSemaphoreGive(i2cMutex); }
```
Hold it across the *whole logical transaction* (a write-then-read register access, not just one
`beginTransmission`/`endTransmission` pair).

### Touch (GT911) core logic

```cpp
// probe 0x5D then fallback 0x14. Status reg 0x814E, point0 reg 0x8150.
sensors::I2cBusGuard g(1);                  // BOUNDED — skip this poll if the bus is busy
if (!g.locked()) return false;
readReg16(addr, 0x814E, &status, 1);
if (!(status & 0x80)) return false;         // buffer not ready
if ((status & 0x0F) > 0) {
  readReg16(addr, 0x8150, point, 7);
  x = point[0] | (point[1] << 8);           // NO track-id byte — see 2.2
  y = point[2] | (point[3] << 8);
}
writeReg16(addr, 0x814E, 0x00);             // clear buffer-ready flag
// In the indev read callback: reject x>=1024 || y>=600; on release, report the CACHED last x/y.
```

### Pin map highlights (`board::`)

```
I2C: SDA=8 SCL=9 @400kHz        Touch INT=4 (addr 0x5D, backup 0x14)
RGB: DE=5 VSYNC=3 HSYNC=46 PCLK=7
SD_MMC 1-bit: CLK=12 CMD=11 D0=13           CAN: TX=20 RX=19 (shared with USB, IO-ext bit5 selects)
RS485: RX=15 TX=16
IO-ext @0x24 bits: 0 free, 1 touch-rst, 2 backlight, 3 lcd-rst, 4 sd-cs, 5 usb/can-sel, 6 free, 7 free
```

---

## 6. LVGL / rendering rules learned the hard way

1. **Dirty-check every periodic style write.** LVGL 8's `lv_obj_set_style_*()` does **not** compare
   against the current value — it always runs `lv_obj_refresh_style()`, which invalidates the object.
   A style set on a 200ms tick without a guard dirties that widget 5x/second forever. One such call
   cost 87% of all render time. Compare with `lv_obj_get_style_*()` first.
2. **Throttle the UI loop to the panel.** At 30MHz pclk the panel scans out ~33fps. Run
   `lv_timer_handler()` on a ~30ms period; flushing faster only risks mid-scan updates.
   ```cpp
   for (;;) { lv_timer_handler(); vTaskDelay(pdMS_TO_TICKS(30)); }
   ```
3. **Make decorative/interactive-by-default widgets non-clickable** (charts, arcs used as gauges):
   `lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE)`. A draggable chart caused a redraw storm.
4. **Do not re-run LVGL's native theme init on a theme switch** — the full-screen redraw reliably
   corrupted the panel. Instead give each screen an `applyAccent(lv_color_t)` and call it.
5. **No `LV_SIZE_CONTENT` on anything whose content changes after first layout** — use explicit
   `lv_obj_set_pos()/set_size()` with pixel constants. Three separate hardware-only bugs came from this
   (one clipped the battery/WiFi readout off-screen; invisible in an HTML mockup).
6. **Never block `uiTask` with a single long `vTaskDelay`** during startup animations — it stops
   `lv_timer_handler()` and silently completes any in-flight animation with zero frames rendered. Loop
   `lv_timer_handler()` + `vTaskDelay(10)` instead. Start boot animations from the top of `uiTask`,
   not from `build()`.
7. **Use `lv_anim_path_linear`** for steady sweeps; `ease_out` rushes to ~90% instantly.
8. **`LV_MEM_SIZE` is a static internal-SRAM array**, not a heap malloc — oversizing it shrinks the whole
   app's heap. 112KB is verified (real peak 80KB, hit by the 20-icon picker overlay). **Never size it from
   `lv_mem_monitor().max_used`** — it's a naive running sum blind to TLSF overhead and fragmentation; a
   64KB attempt based on it crashed the board. Use `used_pct` / `total_size - free_size`, measured while
   driving the real widget churn (keyboard, dropdown, overlays), not on an idle screen.
9. **LVGL's `monitor_cb` frame time is wall-clock**, so it cannot distinguish "this redraw did more
   work" from "this task was preempted". To diagnose slowness, time inside `lv_timer_handler()`
   separately from the loop's real period (see section 7).

### 6.1 Open problem: `num_fbs = 2` is NOT real double buffering
Verified by reading `esp_lcd_panel_rgb.c`: with a non-zero `bounce_buffer_size_px`, the buffer-swap
path (guarded by `if (!bb_size)`) never runs, and `cur_fb_index` is only reassigned when the pointer
passed to `draw_bitmap()` is one of the panel's own framebuffers — which never happens because
LVGL's draw buffers are separate PSRAM allocations. So `cur_fb_index` is permanently 0, `fbs[1]`
(1.2MB PSRAM) is wasted, and LVGL CPU-copies directly into the buffer the DMA is scanning live.

**Symptom (confirmed by photo):** torn lines and colour-fringed pixels on localized areas while the rest
of the frame is intact, triggered by WiFi connected + rapid UI updates. Not a vertical wrap. It is
occasional, not constant. The header comments in `bsp_display.h` claiming "true double buffering"
are wrong for this configuration.

**Two fix attempts, both reverted:**
- *IRAM-safe + drift detector + auto-resync* (`CONFIG_LCD_RGB_ISR_IRAM_SAFE=y`, vsync/frame-complete
  phase measurement, `esp_lcd_panel_init()` re-call as a hard resync). The detector never fired
  (`resyncs=0`) while tearing was visible — wrong theory for this symptom. Useful learning: a healthy
  panel does **not** read zero phase between vsync and frame-complete; the bounce refill runs ahead by a
  fixed ~554-584µs (~11-12 lines), so any detector must calibrate that baseline, not compare to zero.
- *Real double buffering* — **crash-looped on boot** (`LoadProhibited` on Core 1, watchdog resets,
  never reached a usable state). The attempt:
  ```cpp
  .bounce_buffer_size_px = 0,                                  // was 1024*10
  // ... after esp_lcd_panel_init():
  void *fb0, *fb1;
  esp_lcd_rgb_panel_get_frame_buffer(panelHandle, 2, &fb0, &fb1);
  lv_disp_draw_buf_init(&drawBuf, fb0, fb1, 1024 * 600);       // LVGL renders INTO the panel's own buffers
  dispDrv.full_refresh = 1;
  ```
  Root cause of the crash was **not diagnosed**. Untested candidates: LVGL colour-format/byte-layout
  assumptions against the raw framebuffer, a missing LVGL direct-mode config step, or full-frame
  rendering cost/memory interaction. An earlier session also found `full_refresh` raised CPU use sharply.

**Recommendation:** treat this as unsolved. Do not retry on the only board. If it matters for the new
project, validate the LVGL-direct-framebuffer wiring somewhere with a safe fallback first (second
board, or native LVGL simulator), and build it incrementally. Note the two patch files describing these
attempts were saved to a temporary scratchpad that has since been wiped — they no longer exist; the code
above is the reconstruction.

---

## 7. Diagnostics that were worth keeping

Two kinds of boot/runtime log lines are cheap, passive, and have repeatedly paid for themselves.
Keep passive logging; remove anything that changes behaviour.

```
[HEAP-DIAG] after <subsystem>  free=…        — per-subsystem heap checkpoints in setup()
[STACK-DIAG] <task>(<size>B): <n> B free      — uxTaskGetStackHighWaterMark (reports bytes on this port)
[RESET-DIAG] esp_reset_reason() = …           — POWERON vs PANIC vs BROWNOUT vs WDT
[PERF-DIAG] t=… fps=… frameMs=… heapFree=… psramFree=… i2cStall=…
[UI-DIAG]   t=… loops=… handlerAvg=…ms handlerMax=…ms periodAvg=…ms flushes=… flushAvg=…ms flushPx=… lvFrame=…ms
```

How to read `[UI-DIAG]` (this is what finally cracked the CPU-scaling bug):
- `handlerAvg` = time actually inside `lv_timer_handler()`. `periodAvg` = real loop cadence.
- `periodAvg` tracking `handlerAvg` → the render itself is slow. `periodAvg` ≫ `handlerAvg` → the task is
  being starved.
- Healthy baseline on this board: `handlerAvg` ~4-6ms, `periodAvg` ~34.5ms (30ms delay + work),
  ~57 loops/2s, `flushAvg` ~1.3ms, internal heap ~37KB free (16%) after all tasks start, PSRAM ~3.57MB free.
- A **deterministic** step at the same timestamp on every boot points at a fixed timer, not a leak.
- Do not clamp displayed load at 100% — a clamped "frame load" read 100% for months while the real
  figure was 757%. Show raw `frameMs`.

Reading serial without resetting a running board (the CH343 auto-reset toggles on port open):
```python
import serial
s = serial.Serial(); s.port = 'COM7'; s.baudrate = 115200; s.timeout = 1
s.dtr = False; s.rts = False        # set BEFORE open() so attaching doesn't reset the board
s.open()
```
Use `grep -a` on captured logs — a crash dump makes grep treat the file as binary and silently hide matches.
A leftover capture script holds COM7 and makes the next upload fail with "Access is denied".

---

## 8. Build / flash workflow on this machine

- **Project path must contain no spaces.** `...\Desktop\Claude Code\...` hard-fails the ESP-IDF/CMake layer
  ("Detected a whitespace character in project paths"). The old project lives at
  `C:\Users\beaus\OneDrive\canopy-panel`.
- **Use PowerShell, not Git Bash** — pioarduino's toolchain installer refuses under MSYS/MinGW.
- **Set `$env:PYTHONIOENCODING = "utf-8"`** — esptool 5.x prints Unicode progress bars that kill PlatformIO's
  log thread under cp1252, silently hanging the build with no output.
- Don't pipe a *backgrounded* `pio` through `Select-Object -Last N` (buffers everything; looks hung).
- `pio` isn't on PATH; use the full path.

```powershell
$env:PYTHONIOENCODING = "utf-8"
cd C:\path\to\project
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e <env> -t upload --upload-port COM7
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" -m esptool --port COM7 verify-flash 0x10000 .pio\build\<env>\firmware.bin
```

Build timing/quirks:
- Normal incremental build ≈ 40-60s. **Any change to `custom_sdkconfig` forces a full framework rebuild ≈ 6-7 min.**
- A repo under OneDrive intermittently fails the component-manager step with
  `PermissionError: [WinError 5] Access is denied` on `managed_components\...` (OneDrive briefly locking
  fresh files). Fix: `Remove-Item -Recurse -Force managed_components` and rebuild.
- That same rebuild trigger can delete or modify tracked `CMakeLists.txt` / `dependencies.lock`.
  Restore with `git checkout -- CMakeLists.txt dependencies.lock`.
- Consider keeping the new project **outside OneDrive** (e.g. `C:\dev\...`) to avoid the locking entirely.
- Web UI files live in `data/` and are flashed with `pio run -t uploadfs`.

---

## 9. Architecture and tuning that worked

**Tasks** (FreeRTOS, pinned):

| Task | Core | Prio | Stack | Notes |
|---|---|---|---|---|
| sensor | 0 | 5 | 2048 | ADC/relay loop, measured peak ~850B |
| power | 0 | 2 | 2048 | PIR/backlight fade, measured peak ~730B |
| net | 0 | 2 | 8192 | WiFi/web/WS; peak not yet exercised by a real phone at sizing time |
| ui | 1 | 4 | 16384 | LVGL only — nothing else on core 1 |

Right-size stacks from `uxTaskGetStackHighWaterMark()` **after** exercising real paths; don't shrink
`net`/`ui` from an idle reading.

**Memory** (internal heap ≈ 229KB total): WiFi driver base footprint ≈ 53KB, web server/AsyncTCP ≈ 18.7KB —
together over a third of internal heap before any client connects. 16% free after boot is workable but
thin; this is why `LV_MEM_SIZE` and task stacks were tuned. Run WiFi **AP-only** until STA credentials
exist (`WIFI_AP_STA` only once needed).

**WiFi / phone** (if the new project has a phone dashboard):
- AP at 192.168.4.1, WebSocket state push ~300ms, plain HTTP (no TLS → PWA "install" banner won't
  appear; manual Add-to-Home-Screen works).
- WebSocket dead-client leak: a phone dropping WiFi without a clean close left the server queueing to a
  dead connection forever, pinning heap near zero. Fix: `setCloseClientOnQueueFull()` plus a reaper called
  every net tick.
- SoftAP DHCP bug with a self-heal reboot — suppress the "disconnected" toast just before that reboot.
- Cache NVS-backed strings (e.g. system name); an uncached `Preferences` read on every ~3Hz broadcast was
  a flash-access source.
- POST bodies arrive in TCP chunks — use a chunk-accumulating JSON handler, not `deserializeJson` per chunk.
- Do **not** use a cache-first service worker for the app shell — it served stale old/new mixed files after
  firmware updates. Ship a self-unregistering kill-switch `sw.js` if one ever shipped.
- Mobile browsers suspend `setTimeout` in backgrounded tabs — drive WS reconnect from `visibilitychange`
  and `online` events, not just a retry timer, and make `connectWebSocket()` idempotent.

**Power manager:** dim the backlight, never the CPU (2.5). Wake on touch/PIR/phone command via
`notifyActivity()` (safe from any task).

**Boot "ceremony" animations:** start from the top of `uiTask`, hold a 400ms resting frame first, use
`lv_anim_path_linear`, and keep a pump loop (6) — otherwise they appear to jump to the end.

**Process habits that paid off:**
- Add a targeted diagnostic → flash → capture live serial in the background while the user reproduces →
  *then* conclude. Code-only theorising was wrong repeatedly (dropdown bug, heap sizing, fps decay).
- When a hardware conclusion is reached by elimination ("must be ripple/hardware"), get an A/B against
  vendor reference firmware before accepting it.
- Mock UI changes in a standalone HTML/CSS/JS 1024x600 mockup before touching C++ — but the mockup drifts
  from firmware; don't assume they match.
- A "tearing" or "flashing red" report may not be corruption (it was once a correct low-battery alert on a
  bench unit with no battery wired). Ask what's actually on screen first.

---

## 10. What the previous project contains (for context only)

`src/`: `config/` (NVS-backed settings, calibration), `control/` (relay driver, fuse/protection logic,
compressor controller, shared `SystemState`), `sensors/` (ADS1115 manager, battery SoC, tank, pressure,
shunt, PIR, MCP23017 wrapper, I2C mutex), `power/` (dim/sleep), `net/` (WiFi AP, web server, WebSocket
sync, an unfinished protection-board link), `logging/` (SD CSV + ring buffer), `ui/` (Home, Energy, Tank,
Air Compressor, Settings, diagnostics overlay, themes incl. a branded "12 Volt Direct" theme).
Also `data/` (phone PWA), `android/` (native Android app + APK), `mockup/ui_mockup.html`, `tools/`.
These are application code — reuse selectively, the BSP layer above is the transferable part.

Known unfinished items there: RS485 Modbus relay driver (research only — Waveshare Modbus RTU Relay (E),
8ch ≤30A, default 9600 baud, address/baud set via config registers `0x4000`/`0x2000`; use the board's
onboard RS485 on GPIO15/16, 3-wire incl. ground; bench-test with a USB-RS485 adapter first), physical buzzer
for alerts (IO-ext `FREE_0`/`FREE_6` available), pressure sensor not wired, no pressure-calibration UI,
`net`/`ui` stacks not safely shrunk, an untracked debris folder `canopy-panel/canopy-panel/` at the repo root.

---

## 11. Suggested first steps for the new project

1. Create the project at a **no-spaces path** (ideally outside OneDrive).
2. Copy the BSP files (section 5), `platformio.ini` (section 4), `partitions.csv`.
3. Boot a minimal `main.cpp`: `Serial` → `Wire.begin` → `i2cBusInit` → IO-ext `begin` → `bspDisplay().begin`
   → `bspTouch().begin` → one LVGL label → `uiTask` with the 30ms loop. **Confirm clean display + touch before
   adding anything.** Expect COM7 for logs/flash.
4. Add `[UI-DIAG]` early so any later regression is measurable against the section 7 baseline.
5. Decide the power policy up front: backlight dimming only, CPU pinned at 240MHz.
6. Wrap every flash write (config save) in blank/unblank with a 400ms settle, and set `WiFi.persistent(false)`.
7. Only then add relays/sensors (MCP23017 @0x20, ADS1115 @0x4A) — all through the I2C mutex.
8. Look at the screen after every display-affecting change.
