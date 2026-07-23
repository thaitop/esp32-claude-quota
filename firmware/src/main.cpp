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
#include "net/config_portal.h"
#include "net/config_store.h"
#include "net/crypto.h"
#include "net/market.h"
#include "net/poller.h"
#include "net/stock.h"
#include "net/timesync.h"
#include "net/weather.h"
#include "net/wifi_portal.h"
#include "secrets.h"
#include "ui/nav.h"
#include "ui/screen_claude.h"
#include "ui/screen_crypto.h"
#include "ui/screen_setting.h"
#include "ui/screen_stock.h"
#include "ui/screen_weather.h"
#include "ui/screen_weekly.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

AppModel model;

// Copies what the radio knows into the model, so the Setting screen can report
// the link without including <WiFi.h> -- the same seam that keeps the ui layer
// out of HTTP, for the same reason.
void recordLink() {
  model.wifiAssociated = WiFi.status() == WL_CONNECTED;
  if (!model.wifiAssociated) {
    model.rssi = 0;
    model.ipAddress[0] = '\0';
    return;
  }
  model.rssi = WiFi.RSSI();
  strncpy(model.ipAddress, WiFi.localIP().toString().c_str(),
          sizeof(model.ipAddress) - 1);
  model.ipAddress[sizeof(model.ipAddress) - 1] = '\0';
}

// Only the Screen being looked at is repainted. The others keep whatever they
// last drew, which is why switching to one has to update it before it becomes
// visible -- their update functions are change-detecting, so calling them on
// the switch redraws exactly what went stale while they were hidden.
void refreshActiveScreen(uint32_t nowMs) {
  switch (ui::activeScreen()) {
    case ui::Screen::Claude:  ui::updateClaudeScreen(model); break;
    case ui::Screen::Weekly:  ui::updateWeeklyScreen(model); break;
    case ui::Screen::Weather: ui::updateWeatherScreen(model); break;
    case ui::Screen::Crypto:  ui::updateCryptoScreen(model); break;
    case ui::Screen::Stock:   ui::updateStockScreen(model); break;
    case ui::Screen::Setting: ui::updateSettingScreen(model, nowMs); break;
    default: break;
  }
}

// The panel side of Config Mode: a full-screen overlay on LVGL's top layer,
// above whatever screen was showing, carrying the address and PIN a browser
// needs. Built on entry and never torn down -- Config Mode ends in a reboot, so
// the overlay lives exactly as long as the web server does.
void showConfigOverlay(const char *ip, const char *pin) {
  lv_obj_t *ov = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(ov);
  lv_obj_set_size(ov, display::WIDTH, display::HEIGHT);
  lv_obj_set_pos(ov, 0, 0);
  lv_obj_add_style(ov, theme::bgStyle(theme::BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_remove_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(ov, LV_OBJ_FLAG_CLICKABLE);  // swallow taps meant for the screen

  lv_obj_t *heading = ui::makeLabel(ov, &font_inter_22_bold, theme::TEXT);
  lv_label_set_text(heading, "Config Mode");
  lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 26);

  lv_obj_t *hint = ui::makeLabel(ov, &font_inter_12, theme::MUTED);
  lv_label_set_text(hint, "Open this address on the same WiFi:");
  lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 70);

  char url[32];
  snprintf(url, sizeof(url), "http://%s/", ip);
  lv_obj_t *addr = ui::makeLabel(ov, &font_inter_22_bold, theme::ACCENT);
  lv_label_set_text(addr, url);
  lv_obj_align(addr, LV_ALIGN_TOP_MID, 0, 92);

  char pinText[16];
  snprintf(pinText, sizeof(pinText), "PIN  %s", pin);
  lv_obj_t *pinLabel = ui::makeLabel(ov, &font_inter_22_bold, theme::TEXT);
  lv_label_set_text(pinLabel, pinText);
  lv_obj_align(pinLabel, LV_ALIGN_TOP_MID, 0, 140);

  lv_obj_t *foot = ui::makeLabel(ov, &font_inter_12, theme::MUTED);
  lv_label_set_text(foot, "Feeds paused. Hold screen to exit without saving.");
  lv_obj_align(foot, LV_ALIGN_BOTTOM_MID, 0, -20);
}

// Stops the feeds by taking the loop down the Config Mode path, stands up the
// web server, and shows the overlay. recordLink() first so the IP on screen is
// current -- Config Mode is reached by a tap, which can happen long after boot.
void enterConfigMode() {
  recordLink();
  if (!model.wifiAssociated) return;  // no LAN, no server; the tap is a no-op
  net::configPortalBegin(model.ipAddress);
  showConfigOverlay(model.ipAddress, net::configPortalPin());
}

// The panel side of WiFi Setup, the AP-mode sibling of showConfigOverlay. Shows
// the AP name and password a phone joins and the URL to open once on it. Built
// on entry and never torn down -- WiFi Setup ends in a reboot, same as Config
// Mode.
void showWifiSetupOverlay(const char *ssid, const char *pass, const char *url) {
  lv_obj_t *ov = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(ov);
  lv_obj_set_size(ov, display::WIDTH, display::HEIGHT);
  lv_obj_set_pos(ov, 0, 0);
  lv_obj_add_style(ov, theme::bgStyle(theme::BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_remove_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(ov, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t *heading = ui::makeLabel(ov, &font_inter_22_bold, theme::TEXT);
  lv_label_set_text(heading, "WiFi Setup");
  lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 20);

  lv_obj_t *hint = ui::makeLabel(ov, &font_inter_12, theme::MUTED);
  lv_label_set_text(hint, "Join this WiFi from your phone:");
  lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 58);

  lv_obj_t *net = ui::makeLabel(ov, &font_inter_22_bold, theme::ACCENT);
  lv_label_set_text(net, ssid);
  lv_obj_align(net, LV_ALIGN_TOP_MID, 0, 78);

  char passText[24];
  snprintf(passText, sizeof(passText), "PASS  %s", pass);
  lv_obj_t *passLabel = ui::makeLabel(ov, &font_inter_22_bold, theme::TEXT);
  lv_label_set_text(passLabel, passText);
  lv_obj_align(passLabel, LV_ALIGN_TOP_MID, 0, 118);

  lv_obj_t *urlHint = ui::makeLabel(ov, &font_inter_12, theme::MUTED);
  lv_label_set_text(urlHint, "then open:");
  lv_obj_align(urlHint, LV_ALIGN_TOP_MID, 0, 158);

  lv_obj_t *urlLabel = ui::makeLabel(ov, &font_inter_22_bold, theme::ACCENT);
  lv_label_set_text(urlLabel, url);
  lv_obj_align(urlLabel, LV_ALIGN_TOP_MID, 0, 176);

  lv_obj_t *foot = ui::makeLabel(ov, &font_inter_12, theme::MUTED);
  lv_label_set_text(foot, "Hold screen to reboot without changing WiFi.");
  lv_obj_align(foot, LV_ALIGN_BOTTOM_MID, 0, -20);
}

// Stands up the AP-mode WiFi provisioning server and shows its overlay. Reached
// when connectWifi() fails at boot, or when the screen is held during boot to
// force a change even though the saved network still works.
void enterWifiSetup() {
  net::wifiPortalBegin();
  showWifiSetupOverlay(net::wifiPortalApSsid(), net::wifiPortalApPassword(),
                       net::wifiPortalUrl());
}

// Whether the screen is being held as the device boots, the on-device way to
// force WiFi Setup when the saved network still connects (moving house, say).
// Samples touch across a short window, requiring a continuous press so a single
// settling glitch does not trip it. display::tick() drives LVGL's input read.
bool screenHeldAtBoot() {
  const uint32_t deadline = millis() + 700;
  while (millis() < deadline) {
    display::tick();
    lv_indev_t *indev = lv_indev_get_next(nullptr);
    if (indev == nullptr || lv_indev_get_state(indev) != LV_INDEV_STATE_PRESSED)
      return false;
    delay(20);
  }
  return true;
}

// Joins the network NVS holds (or the secrets.h defaults, on a fresh device).
// Returns whether it associated inside the timeout; the caller drops into WiFi
// Setup (AP mode) when it did not, so a device whose saved network is gone can
// be pointed at a new one without a reflash.
bool connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(net::configWifiSsid(), net::configWifiPass());

  const uint32_t deadline = millis() + WIFI_CONNECT_TIMEOUT_MS;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
    delay(250);
  }

  recordLink();
  if (model.wifiAssociated) {
    Serial.printf("wifi ok, ip=%s, bridge=%s\n", model.ipAddress, model.bridgeUrl);
  } else {
    Serial.println("wifi failed");
  }
  return model.wifiAssociated;
}

// The band the card gestures own: below the title, above the navbar. Anything
// outside it belongs to a widget that handles its own taps.
bool overCards(const lv_point_t &point) {
  return point.y >= HEADER_TOUCH_H && point.y < display::HEIGHT - NAV_TOUCH_H;
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
    // Only over the cards. Holding on a navbar slot would otherwise blank the
    // screen and then switch to that slot on release, since LVGL still emits
    // the click -- two commands from one gesture. The same is true of the
    // brightness steppers in the title band.
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    if (overCards(point)) {
      display::setBacklight(!display::backlightOn());
    }
  }

  if (!isDown && wasDown && !longFired) {
    if (!display::backlightOn()) {
      display::setBacklight(true);
    } else {
      lv_point_t point;
      lv_indev_get_point(indev, &point);
      if (overCards(point)) net::refreshAll();
    }
  }

  wasDown = isDown;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\nboot, heap=%u\n", (unsigned)ESP.getFreeHeap());

  // Before any feed reads a location, a coin id or a symbol, and before the
  // clock sync reads the timezone: load the installation settings from NVS,
  // falling back to the config.h/secrets.h defaults on a fresh device.
  net::configBegin();

  if (!display::begin()) {
    Serial.println("display bring-up failed, halting");
    while (true) delay(1000);
  }

  // Before any widget is built, so the helpers attach styles already in the
  // persisted Mode; the shared BG style then carries a later Mode switch to the
  // root without a rebuild, same as every card and label.
  theme::init();

  lv_obj_t *screen = lv_screen_active();
  lv_obj_add_style(screen, theme::bgStyle(theme::BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  // The SSID now comes from NVS (WiFi Setup can change it), the bridge URL is
  // still compiled in. Both are pointers into storage that outlives setup(), so
  // the model can carry them rather than the Setting screen reaching for them.
  model.ssid = net::configWifiSsid();
  model.bridgeUrl = BRIDGE_BASE_URL;

  ui::buildShell(screen);
  ui::buildClaudeScreen(ui::page(ui::Screen::Claude));
  ui::buildWeeklyScreen(ui::page(ui::Screen::Weekly));
  ui::buildWeatherScreen(ui::page(ui::Screen::Weather));
  ui::buildCryptoScreen(ui::page(ui::Screen::Crypto));
  ui::buildStockScreen(ui::page(ui::Screen::Stock));
  ui::buildSettingScreen(ui::page(ui::Screen::Setting), enterConfigMode);
  ui::updateClaudeScreen(model);
  display::tick();

  lv_obj_update_layout(screen);
  ui::warnIfOverflowing("claude", ui::page(ui::Screen::Claude));
  ui::warnIfOverflowing("weekly", ui::page(ui::Screen::Weekly));
  ui::warnIfOverflowing("weather", ui::page(ui::Screen::Weather));
  ui::warnIfOverflowing("crypto", ui::page(ui::Screen::Crypto));
  ui::warnIfOverflowing("stock", ui::page(ui::Screen::Stock));
  ui::warnIfOverflowing("setting", ui::page(ui::Screen::Setting));

  // Two doors into WiFi Setup, both before the feeds are even registered. The
  // screen held as the device boots forces it even when the saved network works
  // (moving the display to a new place); otherwise a failed association falls
  // into it, so a network that is gone or a wrong password is fixable without a
  // reflash. Either way the loop below runs the AP portal instead of the feeds
  // until the browser saves and the device reboots -- there is nothing to fetch
  // with no uplink, so setup() returns here rather than syncing the clock or
  // registering feeds that could not run.
  if (screenHeldAtBoot()) {
    Serial.println("screen held at boot: entering WiFi Setup");
    enterWifiSetup();
    return;
  }
  if (!connectWifi()) {
    Serial.println("no wifi association: entering WiFi Setup");
    enterWifiSetup();
    return;
  }

  // Before the feeds, and blocking, because it is one packet and the header is
  // already on screen with a hole where the time goes.
  net::syncClock();
  net::readClock(model);
  model.stocks.session = net::marketSession();

  // Registration order does not set priority -- the poller picks whichever
  // Feed is most overdue -- but it does set which one goes first at boot,
  // since they all come due at once and the quota is what the display is for.
  net::registerFeed(Feed::Bridge, net::fetchBridge, POLL_QUOTA_MS);
  net::registerFeed(Feed::Weather, net::fetchWeather, POLL_WEATHER_MS);
  net::registerFeed(Feed::Crypto, net::fetchCrypto, POLL_CRYPTO_MS);
  net::registerFeed(Feed::Stock, net::fetchStock, POLL_STOCK_MS);

  // What the five screens cost LVGL's own pool, once they are all built.
  //
  // The figure that matters is the largest contiguous block rather than the
  // total free: layers are allocated whole, and the Claude screen's gradient
  // bar wants 9088 bytes of them on every repaint. When that block drops below
  // what a layer needs, LVGL retries forever instead of failing, and the main
  // loop starves -- see the note over LV_MEM_SIZE in lv_conf.h. Anything that
  // grows a screen should read this line off the device afterwards.
  lv_mem_monitor_t mem;
  lv_mem_monitor(&mem);
  Serial.printf("lv pool: used=%u free=%u frag=%u%% biggest=%u\n",
                (unsigned)(mem.total_size - mem.free_size), (unsigned)mem.free_size,
                (unsigned)mem.frag_pct, (unsigned)mem.free_biggest_size);

  Serial.printf("ready, heap=%u\n", (unsigned)display::freeHeap());
}

void loop() {
  static uint32_t lastSecond = 0;
  static ui::Screen lastScreen = ui::Screen::Count;

  display::tick();

  // WiFi Setup owns the loop the same way Config Mode does, but simpler: there
  // are no feeds to stop (there is no uplink to fetch over) and no validation
  // TLS to keep clear of, so it only pumps the AP web server. It ends on a
  // reboot -- the browser saving new credentials, or a held press for the
  // present-user who opened it by mistake or changed their mind. The reboot
  // reloads the committed WiFi and tries to join it.
  if (net::wifiPortalActive()) {
    net::wifiPortalService();

    static bool apWasDown = false;
    static uint32_t apPressStart = 0;
    lv_indev_t *indev = lv_indev_get_next(nullptr);
    const bool down =
        indev != nullptr && lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED;
    if (down && !apWasDown) apPressStart = millis();
    const bool heldToExit = down && millis() - apPressStart > LONG_PRESS_MS;
    apWasDown = down;

    if (heldToExit || net::wifiPortalShouldReboot()) {
      Serial.println(heldToExit ? "wifi setup cancelled on device, rebooting"
                                : "wifi saved, rebooting");
      delay(200);  // let the final HTTP response flush before the reset
      ESP.restart();
    }
    delay(5);
    return;
  }

  // Config Mode owns the loop while it is up: the feeds are stopped so no TLS
  // fetch can overlap the web server's validation fetches (ADR-0003/0005), and
  // the touch gestures are suppressed because the overlay is the only thing on
  // screen. It ends the one way it can -- a reboot onto the freshly saved
  // settings, once the browser asks for it.
  if (net::configPortalActive()) {
    net::configPortalService();

    // The on-device escape hatch: a held press anywhere leaves Config Mode
    // without saving, so a mistaken tap into it does not strand the device
    // waiting for a browser. Held rather than tapped so a stray touch cannot
    // drop out of a session someone is in the middle of. The reboot discards
    // the draft, exactly as the browser's Cancel does.
    static bool cfgWasDown = false;
    static uint32_t cfgPressStart = 0;
    lv_indev_t *indev = lv_indev_get_next(nullptr);
    const bool down =
        indev != nullptr && lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED;
    if (down && !cfgWasDown) cfgPressStart = millis();
    const bool heldToExit = down && millis() - cfgPressStart > LONG_PRESS_MS;
    cfgWasDown = down;

    if (heldToExit || net::configPortalShouldReboot()) {
      Serial.println(heldToExit ? "config cancelled on device, rebooting"
                                : "config saved, rebooting");
      delay(200);  // let any final HTTP response flush before the reset
      ESP.restart();
    }
    delay(5);
    return;
  }

  handleTouch();

  const uint32_t now = millis();

  // Association is a cheap read and worth checking every pass. The address and
  // the signal strength are not -- localIP().toString() builds a String -- so
  // they are refreshed on the one-second tick below instead.
  const bool associated = WiFi.status() == WL_CONNECTED;
  if (associated != model.wifiAssociated) {
    recordLink();
    if (!associated) WiFi.reconnect();
    // The boot sync runs whether or not the radio made it up in twenty
    // seconds, so an association arriving later is the second and last chance
    // the clock gets. A no-op once synced.
    if (associated && !net::clockSynced()) {
      net::syncClock();
      net::readClock(model);
    }
    refreshActiveScreen(now);
  }

  // A Screen that has just come into view is showing whatever it drew before
  // it was hidden. Switching has to feel like hardware, so this happens on the
  // same pass as the tap rather than on the next one-second tick.
  if (ui::activeScreen() != lastScreen) {
    lastScreen = ui::activeScreen();
    refreshActiveScreen(now);
  }

  if (net::service(model, now)) {
    Serial.printf(
        "feeds: bridge=%s weather=%s crypto=%s stock=%s | session=%d%% "
        "weekly=%d%% stale=%us | history=%u | %.1fC | BTC $%.2f | "
        "AAPL $%.2f | heap=%u\n",
        describe(model.status(Feed::Bridge).outcome),
        describe(model.status(Feed::Weather).outcome),
        describe(model.status(Feed::Crypto).outcome),
        describe(model.status(Feed::Stock).outcome),
        (int)model.quota.session.utilization, (int)model.quota.weekly.utilization,
        (unsigned)model.quota.stalenessSeconds, (unsigned)model.history.count,
        (double)model.weather.temperatureC,
        (double)model.crypto.coin(Coin::BTC).priceUsd,
        (double)model.stocks.quote(Ticker::AAPL).priceUsd,
        (unsigned)display::freeHeap());
    refreshActiveScreen(now);
  }

  // Count the reset timers down locally so they run smoothly between polls.
  if (now - lastSecond >= 1000) {
    lastSecond = now;
    recordLink();
    net::readClock(model);
    model.stocks.session = net::marketSession();
    if (model.quota.session.secondsToReset > 0) model.quota.session.secondsToReset--;
    if (model.quota.weekly.secondsToReset > 0) model.quota.weekly.secondsToReset--;
    refreshActiveScreen(now);
  }

  delay(5);
}
