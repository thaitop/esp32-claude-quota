// WiFi Setup: the AP-mode escape from a device that cannot reach its network.
//
// Config Mode (config_portal) edits weather/coins/stocks over the LAN, which
// means the radio is already up -- it cannot change the WiFi it is running on.
// This is the other half: when connectWifi() fails at boot (wrong password, a
// network that moved, a fresh device flashed with placeholder creds), or the
// screen is held during boot to force it, the device stops being a client and
// becomes its own access point. A phone joins that AP, opens the page, types
// the real SSID and password, and the device saves them to NVS and reboots onto
// the new network.
//
// Deliberately not config_portal:
//   - It runs in AP mode with no internet, so it can validate nothing on the
//     wire -- there is no "check this city against Open-Meteo" step. The only
//     check is "did WPA2 later accept these", which only the next boot answers.
//   - It edits two fields, not the whole settings struct.
//   - It has no feeds to stop and no ADR-0003 TLS-overlap rule to keep: nothing
//     else touches the radio while the AP is up.
//
// Like the feeds and config_portal it lives net-side: it owns WiFi and HTTP, and
// the panel overlay that shows the AP name, password and URL is built by the
// caller in main.cpp -- the one place allowed to bridge net and ui.
#pragma once

namespace net {

// Switches the radio to AP mode, brings up a WPA2 access point named from the
// device MAC with a random password, and starts the HTTP server on
// 192.168.4.1:80. After this, active() is true; the main loop must stop the
// feeds (they cannot fetch with no uplink anyway) and call wifiPortalService()
// instead. Idempotent: a second call while active is a no-op.
void wifiPortalBegin();

// True between begin() and the reboot that ends WiFi Setup.
bool wifiPortalActive();

// Handles one HTTP client. Call every loop while active.
void wifiPortalService();

// For the caller's panel overlay. The AP SSID a phone looks for in its WiFi
// list, the WPA2 password it must type to join, and the URL to open once on it.
// All empty while inactive.
const char *wifiPortalApSsid();
const char *wifiPortalApPassword();
const char *wifiPortalUrl();

// True once the browser has saved credentials (or asked to exit without
// saving). The caller reboots on seeing this -- boot reloads the committed WiFi
// from NVS and tries to join it, exactly as applying a Config Mode change does.
bool wifiPortalShouldReboot();

}  // namespace net
