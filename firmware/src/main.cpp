// Claude quota monitor for the ESP32-2432S028R ("Cheap Yellow Display").
//
// Polls the bridge on the Mac for the two quota windows and renders them as
// cards with a live countdown. No credential lives on the device: the bridge
// does all the reading of ~/.claude, and only percentages cross the wire.
//
// Touch: tap a navbar slot to switch screens, hold ~1s over a card to blank
// the display, tap a card to force a refresh.

#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>

#include "config.h"
#include "display.h"
#include "model.h"
#include "net/bridge.h"
#include "net/poller.h"
#include "secrets.h"
#include "ui/nav.h"
#include "ui/screen_claude.h"
#include "ui/screen_placeholder.h"
#include "ui/theme.h"

namespace {

AppModel model;

void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const uint32_t deadline = millis() + WIFI_CONNECT_TIMEOUT_MS;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
    delay(250);
  }

  model.wifiAssociated = WiFi.status() == WL_CONNECTED;
  if (model.wifiAssociated) {
    Serial.printf("wifi ok, ip=%s, bridge=%s\n", WiFi.localIP().toString().c_str(),
                  BRIDGE_BASE_URL);
  } else {
    Serial.println("wifi failed, will keep retrying");
  }
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

  if (!isDown && wasDown && !longFired) {
    if (!display::backlightOn()) {
      display::setBacklight(true);
    } else {
      lv_point_t point;
      lv_indev_get_point(indev, &point);
      if (point.y < display::HEIGHT - NAV_TOUCH_H) net::refreshAll();
    }
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

  ui::buildShell(screen);
  ui::buildClaudeScreen(ui::page(ui::Screen::Claude));
  ui::buildPlaceholder(ui::page(ui::Screen::Weekly), "Weekly", "7-day history");
  ui::buildPlaceholder(ui::page(ui::Screen::Weather), "Weather", "Open-Meteo");
  ui::buildPlaceholder(ui::page(ui::Screen::Crypto), "Crypto", "CoinGecko");
  ui::buildPlaceholder(ui::page(ui::Screen::Setting), "Setting", "diagnostics");
  ui::updateClaudeScreen(model);
  display::tick();

  connectWifi();
  net::registerFeed(Feed::Bridge, net::fetchQuota, POLL_QUOTA_MS);

  Serial.printf("ready, heap=%u\n", (unsigned)display::freeHeap());
}

void loop() {
  static uint32_t lastSecond = 0;

  display::tick();
  handleTouch();

  const uint32_t now = millis();
  const bool associated = WiFi.status() == WL_CONNECTED;
  if (associated != model.wifiAssociated) {
    model.wifiAssociated = associated;
    if (!associated) WiFi.reconnect();
    ui::updateClaudeScreen(model);
  }

  if (net::service(model, now)) {
    const FeedStatus &status = model.status(Feed::Bridge);
    Serial.printf("bridge: %s  session=%d%%  weekly=%d%%  stale=%us  heap=%u\n",
                  describe(status.outcome), (int)model.quota.session.utilization,
                  (int)model.quota.weekly.utilization,
                  (unsigned)model.quota.stalenessSeconds,
                  (unsigned)display::freeHeap());
    ui::updateClaudeScreen(model);
  }

  // Count the reset timers down locally so they run smoothly between polls.
  if (now - lastSecond >= 1000) {
    lastSecond = now;
    if (model.quota.session.secondsToReset > 0) model.quota.session.secondsToReset--;
    if (model.quota.weekly.secondsToReset > 0) model.quota.weekly.secondsToReset--;
    ui::updateClaudeScreen(model);
  }

  delay(5);
}
