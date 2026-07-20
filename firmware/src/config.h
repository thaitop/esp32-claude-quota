// Tunables that are not secret and not specific to one installation's network.
//
// The split from secrets.h is deliberate: this file is committed and is the
// place to look when changing behaviour, while secrets.h stays out of the repo
// because it carries the WiFi password, the LAN address of the bridge, and the
// coordinates of wherever the display is sitting. Home coordinates are personal
// data even though nothing authenticates with them -- hence they live over
// there rather than here.
#pragma once

#include <cstdint>

// ---------------------------------------------------------------------------
// Bridge
// ---------------------------------------------------------------------------

// Paths joined onto BRIDGE_BASE_URL from secrets.h.
#define BRIDGE_PATH_QUOTA "/quota"
#define BRIDGE_PATH_HISTORY "/history?days=7"

// ---------------------------------------------------------------------------
// External feeds
// ---------------------------------------------------------------------------

// Open-Meteo. Free, no API key. The field list is kept minimal so the response
// stays a few hundred bytes -- see ADR-0003 on why heap headroom matters here.
#define WEATHER_HOST "api.open-meteo.com"
#define WEATHER_PATH_FMT                                                    \
  "/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,"       \
  "apparent_temperature,relative_humidity_2m,is_day,weather_code&timezone=" \
  "auto"

// CoinGecko simple/price. Free tier, no API key.
#define CRYPTO_HOST "api.coingecko.com"
#define CRYPTO_COIN_ID "bitcoin"
#define CRYPTO_PATH                                            \
  "/api/v3/simple/price?ids=" CRYPTO_COIN_ID                   \
  "&vs_currencies=usd&include_24hr_change=true"

// ---------------------------------------------------------------------------
// Polling
// ---------------------------------------------------------------------------

// Only ever one feed in flight at a time (ADR-0003). These are the intervals
// the round-robin scheduler honours, not guarantees.
constexpr uint32_t POLL_QUOTA_MS = 20UL * 1000;         // LAN, free, no limits
constexpr uint32_t POLL_HISTORY_MS = 10UL * 60 * 1000;  // changes slowly
constexpr uint32_t POLL_WEATHER_MS = 10UL * 60 * 1000;  // Open-Meteo: 10k/day
constexpr uint32_t POLL_CRYPTO_MS = 60UL * 1000;        // CoinGecko free tier
constexpr uint32_t POLL_RETRY_MS = 5UL * 1000;

constexpr uint32_t HTTP_TIMEOUT_MS = 6000;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;

// A reading older than this loses Trust regardless of how the fetch went.
constexpr uint32_t QUOTA_STALENESS_LIMIT_S = 30 * 60;

// ---------------------------------------------------------------------------
// Interaction
// ---------------------------------------------------------------------------

constexpr uint32_t LONG_PRESS_MS = 1000;

// Raw XPT2046 extents at the edges of the visible area, in the panel's own
// landscape orientation. Resistive panels vary unit to unit; if touches land
// consistently off-centre, run the calibration sketch and replace these.
// Signed, because extrapolating the fit out to the panel edge can land just
// past zero on a panel whose active area starts inside the raw range.
// Measured on this unit with the four-crosshair pass in the bring-up sketch,
// not estimated from how far off a touch looked -- the error turned out to be
// mostly scale rather than offset, which eyeballing would have got wrong.
constexpr int32_t TOUCH_RAW_MIN_X = 168;
constexpr int32_t TOUCH_RAW_MAX_X = 3570;
constexpr int32_t TOUCH_RAW_MIN_Y = 239;
constexpr int32_t TOUCH_RAW_MAX_Y = 3690;

// The navbar draws 40px tall but accepts touches over a taller strip: resistive
// panels and a stylus tip do not agree about where the edge is.
constexpr int16_t NAV_VISUAL_H = 44;
constexpr int16_t NAV_TOUCH_H = 56;

// ---------------------------------------------------------------------------
// Colour ramp thresholds
// ---------------------------------------------------------------------------

constexpr int8_t RAMP_WARN_PCT = 60;
constexpr int8_t RAMP_ALARM_PCT = 85;
