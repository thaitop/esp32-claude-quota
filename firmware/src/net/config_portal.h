// Config Mode: a short-lived web server for editing the installation settings.
//
// Everything the display shows about the outside world -- where the weather is,
// which coins and which stocks -- used to need a reflash to change. This is the
// alternative: hold the Config button on the Setting screen, the device stops
// its feeds, stands up a plain HTTP server on the LAN, and shows an IP and a PIN
// on the panel. A browser on the same network edits the settings, each field
// validated against the real API as it is entered, and the device reboots onto
// the new set.
//
// The one hard rule this file keeps is ADR-0003's: never two TLS connections at
// once. Config Mode stops the feeds, so no feed can overlap a validation fetch.
// The validations themselves do reach out over TLS (to confirm a city, a coin
// id, a symbol), so they are run one at a time, between HTTP clients, never
// while a browser request is being answered -- the browser is told "validating"
// and polls for the result. See ADR-0005.
//
// Net-side, like the feeds: it holds WiFi and HTTP and a soft credential
// (Finnhub's token, to validate a symbol), none of which the ui/ layer may see.
// The panel overlay that shows the IP and PIN is built by the caller, which is
// the one place allowed to bridge net and ui -- the same seam recordLink()
// crosses.
#pragma once

#include <cstdint>

namespace net {

// Seeds the editable draft from the live settings, generates a PIN, and starts
// the HTTP server on `ip`:80. After this, active() is true; the main loop must
// stop calling service()/the feeds and call configPortalService() instead.
void configPortalBegin(const char *ip);

// True between begin() and the reboot that ends Config Mode.
bool configPortalActive();

// Handles one HTTP client, then -- if a validation is queued and no client is
// mid-request -- runs that one validation fetch. Call every loop while active.
void configPortalService();

// The PIN the browser must present, for the caller to show on the panel. Four
// digits as a string; empty when inactive.
const char *configPortalPin();

// True once the browser has asked to apply-and-exit (or to cancel). The caller
// reboots on seeing this -- boot reloads the committed settings cleanly, which
// is why applying is a reboot rather than a live swap (ADR-0005).
bool configPortalShouldReboot();

}  // namespace net
