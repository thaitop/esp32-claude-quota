#include "display.h"

#include <Arduino.h>
#include <Preferences.h>
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

// The backlight is dimmed by PWM rather than switched. 5kHz is well above what
// the eye or a phone camera picks up as flicker, and 8 bits is more resolution
// than a control that steps in tenths can ask for. Channel 0 is free: nothing
// else on this board uses LEDC.
constexpr uint8_t BACKLIGHT_CHANNEL = 0;
constexpr uint32_t BACKLIGHT_FREQ_HZ = 5000;
constexpr uint8_t BACKLIGHT_RESOLUTION = 8;
constexpr uint32_t BACKLIGHT_MAX_DUTY = (1u << BACKLIGHT_RESOLUTION) - 1;

// One namespace, one key. Written only when the level actually changes, so
// holding a button does not walk the flash.
constexpr char NVS_NAMESPACE[] = "display";
constexpr char NVS_KEY_BRIGHTNESS[] = "bright";

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
uint8_t level = BACKLIGHT_DEFAULT_PCT;
Preferences prefs;
int16_t rawX = -1;
int16_t rawY = -1;

// The one place a percentage becomes a duty cycle. Blanking is folded in here
// rather than kept as a separate path, so "off" and "10%" cannot drift apart
// into two different ways of driving the same pin.
void applyBacklight() {
  const uint32_t duty = lit ? (uint32_t)level * BACKLIGHT_MAX_DUTY / 100 : 0;
  ledcWrite(BACKLIGHT_CHANNEL, duty);
}

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
  // Dark until there is something worth looking at. TFT_eSPI drives TFT_BL
  // itself during init(), so this only holds until then -- which is why the
  // LEDC attach below comes after, and not before: attaching first and then
  // letting init() digitalWrite() the same pin hands it back to the GPIO
  // matrix, and the dimming silently stops working at every level but full.
  pinMode(BACKLIGHT_PIN, OUTPUT);
  digitalWrite(BACKLIGHT_PIN, LOW);

  tft.init();
  // Landscape with the USB socket on the right, as confirmed on the board --
  // rotation 3 is the same orientation flipped end for end.
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  prefs.begin(NVS_NAMESPACE, false);
  level = prefs.getUChar(NVS_KEY_BRIGHTNESS, BACKLIGHT_DEFAULT_PCT);
  if (level < BACKLIGHT_MIN_PCT) level = BACKLIGHT_MIN_PCT;
  if (level > BACKLIGHT_MAX_PCT) level = BACKLIGHT_MAX_PCT;

  ledcSetup(BACKLIGHT_CHANNEL, BACKLIGHT_FREQ_HZ, BACKLIGHT_RESOLUTION);
  ledcAttachPin(BACKLIGHT_PIN, BACKLIGHT_CHANNEL);
  lit = true;
  applyBacklight();

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
  lit = on;
  applyBacklight();
}

bool backlightOn() { return lit; }

void setBrightness(uint8_t percent) {
  if (percent < BACKLIGHT_MIN_PCT) percent = BACKLIGHT_MIN_PCT;
  if (percent > BACKLIGHT_MAX_PCT) percent = BACKLIGHT_MAX_PCT;
  if (percent == level) return;

  level = percent;
  applyBacklight();
  prefs.putUChar(NVS_KEY_BRIGHTNESS, level);
}

uint8_t brightness() { return level; }

uint32_t freeHeap() { return (uint32_t)ESP.getFreeHeap(); }

void lastRawTouch(int16_t &x, int16_t &y) {
  x = rawX;
  y = rawY;
}

}  // namespace display
