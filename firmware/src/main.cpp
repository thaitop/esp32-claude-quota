// Claude quota monitor for the ESP32-2432S028R ("Cheap Yellow Display").
//
// Step 7 of the rewrite: the Claude screen, drawn from the model. There is no
// network layer yet, so the model is seeded with the figures from the design
// and the countdowns tick locally -- which is exactly the comparison worth
// making, since it puts the real values on the real panel next to the mock.
//
// Touch: hold ~1s to blank the screen. Tapping will select navbar slots once
// the navbar exists.

#include <Arduino.h>
#include <lvgl.h>

#include "config.h"
#include "display.h"
#include "model.h"
#include "ui/nav.h"
#include "ui/screen_claude.h"
#include "ui/screen_placeholder.h"
#include "ui/theme.h"

namespace {

AppModel model;

// The figures from the design, so the board can be held next to the mock.
// Replaced by the bridge feed in a later step.
void seedModel() {
  model.wifiAssociated = true;
  model.quota.trusted = true;
  model.quota.session.trusted = true;
  model.quota.session.utilization = 50;
  model.quota.session.secondsToReset = 1 * 3600 + 22 * 60;
  model.quota.weekly.trusted = true;
  model.quota.weekly.utilization = 11;
  model.quota.weekly.secondsToReset = 6 * 86400 + 8 * 3600;
}

void handleTouch() {
  static bool wasDown = false;
  static uint32_t pressStart = 0;
  static bool longFired = false;

  lv_indev_t *indev = lv_indev_get_next(nullptr);
  if (indev == nullptr) return;
  const bool isDown = lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED;

  if (isDown && !wasDown) {
    pressStart = millis();
    longFired = false;
  }

  if (isDown && !longFired && millis() - pressStart > LONG_PRESS_MS) {
    longFired = true;
    // Only above the navbar. Holding on a slot would otherwise blank the
    // screen and then switch to that slot on release, since LVGL still emits
    // the click -- two commands from one gesture.
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    if (point.y < display::HEIGHT - NAV_TOUCH_H) {
      display::setBacklight(!display::backlightOn());
    }
  }

  if (!isDown && wasDown && !longFired && !display::backlightOn()) {
    display::setBacklight(true);
  }

  wasDown = isDown;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\nboot, heap=%u\n", (unsigned)ESP.getFreeHeap());

  if (!display::begin()) {
    Serial.println("display bring-up failed, halting");
    while (true) delay(1000);
  }

  lv_obj_t *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, theme::colour(theme::BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  seedModel();
  ui::buildShell(screen);
  ui::buildClaudeScreen(ui::page(ui::Screen::Claude));
  ui::buildPlaceholder(ui::page(ui::Screen::Weekly), "Weekly", "7-day history");
  ui::buildPlaceholder(ui::page(ui::Screen::Weather), "Weather", "Open-Meteo");
  ui::buildPlaceholder(ui::page(ui::Screen::Crypto), "Crypto", "CoinGecko");
  ui::buildPlaceholder(ui::page(ui::Screen::Setting), "Setting", "diagnostics");
  ui::updateClaudeScreen(model);

  Serial.printf("claude screen up, heap=%u\n", (unsigned)display::freeHeap());
}

void loop() {
  static uint32_t lastTick = 0;

  display::tick();
  handleTouch();

  // Count the reset timers down locally so they run smoothly between polls.
  const uint32_t now = millis();
  if (now - lastTick >= 1000) {
    lastTick = now;
    if (model.quota.session.secondsToReset > 0) model.quota.session.secondsToReset--;
    if (model.quota.weekly.secondsToReset > 0) model.quota.weekly.secondsToReset--;
    ui::updateClaudeScreen(model);
  }

  delay(5);
}
