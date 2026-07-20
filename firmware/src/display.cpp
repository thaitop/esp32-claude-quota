#include "display.h"

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <lvgl.h>

#include "config.h"

namespace display {
namespace {

// The touch controller sits on its own SPI bus on this board, separate from the
// panel, so it gets an explicit SPIClass rather than TFT_eSPI's TOUCH_CS.
constexpr uint8_t TOUCH_CLK = 25;
constexpr uint8_t TOUCH_MISO = 39;
constexpr uint8_t TOUCH_MOSI = 32;
constexpr uint8_t TOUCH_CS = 33;
constexpr uint8_t TOUCH_IRQ = 36;
constexpr uint8_t BACKLIGHT_PIN = 21;

// One tenth of the display, double buffered: 320 x 24 x 2 bytes each. Sized in
// ADR-0003 against the 45KB a TLS handshake peaks at. Growing these is how you
// get allocation failures that surface as reboots hours later.
constexpr uint32_t BUF_LINES = HEIGHT / 10;
constexpr uint32_t BUF_PIXELS = WIDTH * BUF_LINES;
constexpr uint32_t BUF_BYTES = BUF_PIXELS * sizeof(uint16_t);

TFT_eSPI tft;
SPIClass touchSpi(HSPI);
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);

lv_display_t *lvDisplay = nullptr;
lv_indev_t *lvTouch = nullptr;
uint8_t *bufA = nullptr;
uint8_t *bufB = nullptr;
bool lit = true;
int16_t rawX = -1;
int16_t rawY = -1;

void flush(lv_display_t *disp, const lv_area_t *area, uint8_t *pixels) {
  const uint32_t w = area->x2 - area->x1 + 1;
  const uint32_t h = area->y2 - area->y1 + 1;

  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  // The final argument swaps bytes: LVGL renders RGB565 little-endian, the
  // panel wants it big-endian. Getting this wrong does not fail loudly, it
  // just renders in wrong-but-plausible colours.
  tft.pushColors((uint16_t *)pixels, w * h, true);
  tft.endWrite();

  lv_display_flush_ready(disp);
}

void readTouch(lv_indev_t *indev, lv_indev_data_t *data) {
  (void)indev;

  if (!touch.touched()) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }

  const TS_Point p = touch.getPoint();
  rawX = p.x;
  rawY = p.y;

  const long x = map(p.x, TOUCH_RAW_MIN_X, TOUCH_RAW_MAX_X, 0, WIDTH - 1);
  const long y = map(p.y, TOUCH_RAW_MIN_Y, TOUCH_RAW_MAX_Y, 0, HEIGHT - 1);

  data->point.x = (int16_t)constrain(x, 0, WIDTH - 1);
  data->point.y = (int16_t)constrain(y, 0, HEIGHT - 1);
  data->state = LV_INDEV_STATE_PRESSED;
}

uint32_t millisForLvgl() { return (uint32_t)millis(); }

}  // namespace

bool begin() {
  pinMode(BACKLIGHT_PIN, OUTPUT);
  setBacklight(true);

  tft.init();
  // Landscape with the USB socket on the right, as confirmed on the board --
  // rotation 3 is the same orientation flipped end for end.
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  touchSpi.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  touch.begin(touchSpi);
  touch.setRotation(1);

  lv_init();
  lv_tick_set_cb(millisForLvgl);

  bufA = (uint8_t *)heap_caps_malloc(BUF_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
  bufB = (uint8_t *)heap_caps_malloc(BUF_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
  if (bufA == nullptr || bufB == nullptr) {
    Serial.printf("draw buffer alloc failed (%u bytes each, %u free)\n", BUF_BYTES,
                  freeHeap());
    return false;
  }

  lvDisplay = lv_display_create(WIDTH, HEIGHT);
  if (lvDisplay == nullptr) return false;
  lv_display_set_flush_cb(lvDisplay, flush);
  lv_display_set_buffers(lvDisplay, bufA, bufB, BUF_BYTES,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

  lvTouch = lv_indev_create();
  if (lvTouch == nullptr) return false;
  lv_indev_set_type(lvTouch, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(lvTouch, readTouch);
  lv_indev_set_display(lvTouch, lvDisplay);

  Serial.printf("lvgl up: %dx%d, 2 x %u byte buffers, %u heap free\n", WIDTH, HEIGHT,
                BUF_BYTES, freeHeap());
  return true;
}

void tick() { lv_timer_handler(); }

void setBacklight(bool on) {
  digitalWrite(BACKLIGHT_PIN, on ? HIGH : LOW);
  lit = on;
}

bool backlightOn() { return lit; }

uint32_t freeHeap() { return (uint32_t)ESP.getFreeHeap(); }

void lastRawTouch(int16_t &x, int16_t &y) {
  x = rawX;
  y = rawY;
}

}  // namespace display
