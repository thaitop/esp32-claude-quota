// Claude quota monitor -- LVGL bring-up smoke test and touch calibration.
//
// Step 4 of the rewrite: prove the panel, the draw buffers and the touch
// mapping before any real UI is built on them. The screens, the feeds and the
// navbar land in later commits.
//
// It runs in two phases. First it walks four crosshairs around the display and
// records the raw controller reading at each, then prints the calibration
// constants to paste into config.h -- guessing an offset from "about 3mm" would
// have folded any scale error into it. Then it shows colour swatches and a
// dot that tracks the stylus.
//
// The swatches matter more than they look: white and black are unchanged by a
// byte swap, so a screen made only of those cannot tell a correctly wired panel
// from one rendering every colour wrong.

#include <Arduino.h>
#include <lvgl.h>

#include "config.h"
#include "display.h"
#include "model.h"

namespace {

// Where the crosshairs sit, in screen coordinates. Inset from the corners
// because the very edge of a resistive panel reads non-linearly.
constexpr int16_t TARGET_INSET_X = 20;
constexpr int16_t TARGET_INSET_Y = 20;
constexpr int16_t TARGETS[4][2] = {
    {TARGET_INSET_X, TARGET_INSET_Y},
    {display::WIDTH - 1 - TARGET_INSET_X, TARGET_INSET_Y},
    {TARGET_INSET_X, display::HEIGHT - 1 - TARGET_INSET_Y},
    {display::WIDTH - 1 - TARGET_INSET_X, display::HEIGHT - 1 - TARGET_INSET_Y},
};

enum class Phase { Calibrating, Running };

Phase phase = Phase::Calibrating;
uint8_t targetIndex = 0;
int16_t capturedX[4] = {0};
int16_t capturedY[4] = {0};

lv_obj_t *hintLabel = nullptr;
lv_obj_t *heapLabel = nullptr;
lv_obj_t *readoutLabel = nullptr;
lv_obj_t *crossH = nullptr;
lv_obj_t *crossV = nullptr;
lv_obj_t *dot = nullptr;
uint32_t touchCount = 0;

lv_obj_t *makeLabel(lv_obj_t *parent, uint32_t colour, lv_align_t align, int16_t dy) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_style_text_color(label, lv_color_hex(colour), LV_PART_MAIN);
  lv_obj_align(label, align, 0, dy);
  return label;
}

lv_obj_t *makeBox(lv_obj_t *parent, int16_t w, int16_t h, uint32_t colour) {
  lv_obj_t *box = lv_obj_create(parent);
  lv_obj_remove_style_all(box);
  lv_obj_set_size(box, w, h);
  lv_obj_set_style_bg_color(box, lv_color_hex(colour), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(box, LV_OPA_COVER, LV_PART_MAIN);
  return box;
}

void placeCrosshair() {
  const int16_t x = TARGETS[targetIndex][0];
  const int16_t y = TARGETS[targetIndex][1];
  lv_obj_set_pos(crossH, x - 12, y);
  lv_obj_set_pos(crossV, x, y - 12);
  lv_label_set_text_fmt(hintLabel, "tap the cross  (%u of 4)", targetIndex + 1);
}

// Fit screen coordinates to raw readings with a straight line through the two
// inset positions, then extrapolate out to the panel edges. Averaging the two
// samples on each edge cancels most of the tilt in a hand-held stylus.
void reportCalibration() {
  const long rawLeft = (capturedX[0] + capturedX[2]) / 2;
  const long rawRight = (capturedX[1] + capturedX[3]) / 2;
  const long rawTop = (capturedY[0] + capturedY[1]) / 2;
  const long rawBottom = (capturedY[2] + capturedY[3]) / 2;

  const long spanX = TARGETS[1][0] - TARGETS[0][0];
  const long spanY = TARGETS[2][1] - TARGETS[0][1];
  if (spanX == 0 || spanY == 0) return;

  const long minX = rawLeft - (rawRight - rawLeft) * TARGETS[0][0] / spanX;
  const long maxX =
      minX + (rawRight - rawLeft) * (display::WIDTH - 1) / spanX;
  const long minY = rawTop - (rawBottom - rawTop) * TARGETS[0][1] / spanY;
  const long maxY =
      minY + (rawBottom - rawTop) * (display::HEIGHT - 1) / spanY;

  Serial.println("\n--- paste into firmware/src/config.h ---");
  Serial.printf("constexpr int32_t TOUCH_RAW_MIN_X = %ld;\n", minX);
  Serial.printf("constexpr int32_t TOUCH_RAW_MAX_X = %ld;\n", maxX);
  Serial.printf("constexpr int32_t TOUCH_RAW_MIN_Y = %ld;\n", minY);
  Serial.printf("constexpr int32_t TOUCH_RAW_MAX_Y = %ld;\n", maxY);
  Serial.println("----------------------------------------");
  Serial.printf("(currently compiled in: X %ld..%ld  Y %ld..%ld)\n",
                (long)TOUCH_RAW_MIN_X, (long)TOUCH_RAW_MAX_X, (long)TOUCH_RAW_MIN_Y,
                (long)TOUCH_RAW_MAX_Y);
}

void enterRunningPhase() {
  phase = Phase::Running;
  lv_obj_add_flag(crossH, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(crossV, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(hintLabel, "calibration printed to serial");

  lv_obj_t *screen = lv_screen_active();

  // Pure primaries at full saturation. Under a byte swap red renders as a dark
  // blue, green as a dark cyan-grey, and blue as near-black -- unmistakable.
  struct Swatch {
    uint32_t colour;
    const char *name;
  };
  static const Swatch swatches[] = {
      {0xFF0000, "R"}, {0x00FF00, "G"}, {0x0000FF, "B"}, {0xF5822E, "accent"},
  };

  int16_t x = 24;
  for (const Swatch &s : swatches) {
    lv_obj_t *box = makeBox(screen, 52, 34, s.colour);
    lv_obj_set_pos(box, x, 104);
    lv_obj_t *caption = lv_label_create(screen);
    lv_label_set_text(caption, s.name);
    lv_obj_set_style_text_color(caption, lv_color_hex(0x9A9AA0), LV_PART_MAIN);
    lv_obj_set_pos(caption, x, 142);
    x += 72;
  }
}

void buildScreen() {
  lv_obj_t *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

  hintLabel = makeLabel(screen, 0xFFFFFF, LV_ALIGN_TOP_MID, 12);
  heapLabel = makeLabel(screen, 0x9A9AA0, LV_ALIGN_TOP_MID, 36);
  readoutLabel = makeLabel(screen, 0x9A9AA0, LV_ALIGN_BOTTOM_MID, -12);
  lv_label_set_text(readoutLabel, "");

  crossH = makeBox(screen, 25, 3, 0xF5822E);
  crossV = makeBox(screen, 3, 25, 0xF5822E);

  dot = makeBox(screen, 16, 16, 0xF5822E);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);

  placeCrosshair();
}

// Fires once per touch, on release, with the raw reading taken at press time.
void onTouchReleased(int16_t rx, int16_t ry, lv_point_t mapped) {
  touchCount++;

  if (phase == Phase::Calibrating) {
    capturedX[targetIndex] = rx;
    capturedY[targetIndex] = ry;
    Serial.printf("target %u at (%d,%d) -> raw (%d,%d)\n", targetIndex + 1,
                  TARGETS[targetIndex][0], TARGETS[targetIndex][1], rx, ry);

    targetIndex++;
    if (targetIndex >= 4) {
      reportCalibration();
      enterRunningPhase();
    } else {
      placeCrosshair();
    }
    return;
  }

  lv_label_set_text_fmt(readoutLabel, "raw %d,%d  ->  %d,%d   (%u)", rx, ry,
                        (int)mapped.x, (int)mapped.y, touchCount);
}

void pollTouch() {
  static bool wasPressed = false;
  static int16_t pressRawX = 0;
  static int16_t pressRawY = 0;
  static lv_point_t pressPoint = {0, 0};

  lv_indev_t *indev = lv_indev_get_next(nullptr);
  if (indev == nullptr) return;

  const bool pressed = lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED;

  if (pressed) {
    lv_indev_get_point(indev, &pressPoint);
    display::lastRawTouch(pressRawX, pressRawY);
    if (phase == Phase::Running) {
      lv_obj_remove_flag(dot, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_pos(dot, pressPoint.x - 8, pressPoint.y - 8);
    }
  } else if (wasPressed) {
    lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
    onTouchReleased(pressRawX, pressRawY, pressPoint);
  }

  wasPressed = pressed;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\nboot, heap=%u, AppModel=%u bytes\n", (unsigned)ESP.getFreeHeap(),
                (unsigned)sizeof(AppModel));

  if (!display::begin()) {
    Serial.println("display bring-up failed, halting");
    while (true) delay(1000);
  }

  buildScreen();
  Serial.println("tap each crosshair once, starting top-left");
}

void loop() {
  static uint32_t lastHeapUpdate = 0;

  display::tick();
  pollTouch();

  const uint32_t now = millis();
  if (now - lastHeapUpdate >= 1000) {
    lastHeapUpdate = now;
    lv_label_set_text_fmt(heapLabel, "heap %u   up %us", (unsigned)display::freeHeap(),
                          (unsigned)(now / 1000));
  }

  delay(5);
}
