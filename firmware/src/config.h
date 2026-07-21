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
  WEATHER_TZ

// CoinGecko simple/price. Free tier, no API key.
#define CRYPTO_HOST "api.coingecko.com"

// The ids CoinGecko knows the tracked coins by. The tickers and the display
// names that go with them live in model.cpp's table, which reads these back --
// so a request and a title can never disagree about which coin is on screen.
#define CRYPTO_ID_BTC "bitcoin"
#define CRYPTO_ID_ETH "ethereum"
#define CRYPTO_ID_BNB "binancecoin"

// All three coins in one request. Three separate ones would be three round
// trips against a free tier that rate-limits by the minute, and only one feed
// is ever in flight at a time (ADR-0003) -- so they would also arrive up to two
// poll intervals apart and the screen would compare coins read at different
// moments.
#define CRYPTO_PATH                                                      \
  "/api/v3/simple/price?ids=" CRYPTO_ID_BTC "," CRYPTO_ID_ETH ","        \
  CRYPTO_ID_BNB "&vs_currencies=usd&include_24hr_change=true"            \
  "&include_24hr_vol=true"

// Finnhub /quote. Free tier: 60 requests/minute, one symbol per request, and
// no historical candles -- which is why the Stock screen shows a price and a
// day's change but no chart. The token is a soft credential (read-only, public
// market data) and so lives in secrets.h with the rest, fetched directly by the
// device rather than through the bridge -- see ADR-0004 on why that bends
// ADR-0002 by degree rather than in kind.
#define FINNHUB_HOST "finnhub.io"

// One symbol at a time, the token appended. `%s` twice: symbol then token.
#define FINNHUB_PATH_FMT "/api/v1/quote?symbol=%s&token=%s"

// US regular session in Eastern wall-clock minutes past midnight, for the
// market-open badge. Regular hours only; pre- and post-market are "closed"
// here, which matches what the price is doing when nobody is trading the open
// auction. DST is handled where the badge is computed (net/market.cpp); these
// are the same numbers year round.
constexpr int MARKET_OPEN_MINUTE = 9 * 60 + 30;   // 09:30 ET
constexpr int MARKET_CLOSE_MINUTE = 16 * 60;      // 16:00 ET

// ---------------------------------------------------------------------------
// Clock
// ---------------------------------------------------------------------------

#define NTP_SERVER "time.google.com"

// CLOCK_TZ (the POSIX TZ string) lives in secrets.h alongside WEATHER_TZ and the
// coordinates, since it describes where the display sits, not repo-wide
// behaviour. timesync.cpp reads it from there.

// How long the one boot-time sync waits for the first packet. The header shows
// minutes, so a clock that never arrives costs a blank corner and nothing else
// -- which is why this blocks once at boot rather than becoming a fourth feed.
constexpr uint32_t NTP_WAIT_MS = 5000;

// ---------------------------------------------------------------------------
// Polling
// ---------------------------------------------------------------------------

// Only ever one feed in flight at a time (ADR-0003). These are the intervals
// the round-robin scheduler honours, not guarantees.
constexpr uint32_t POLL_QUOTA_MS = 20UL * 1000;         // LAN, free, no limits
constexpr uint32_t POLL_HISTORY_MS = 10UL * 60 * 1000;  // changes slowly
constexpr uint32_t POLL_WEATHER_MS = 5UL * 60 * 1000;   // Open-Meteo: 10k/day
constexpr uint32_t POLL_CRYPTO_MS = 60UL * 1000;        // CoinGecko free tier
// One symbol per pass, five symbols: at 12s each the whole list refreshes in a
// minute, the same freshness the coins get. Well inside Finnhub's 60/min.
constexpr uint32_t POLL_STOCK_MS = 12UL * 1000;
constexpr uint32_t POLL_RETRY_MS = 5UL * 1000;

constexpr uint32_t HTTP_TIMEOUT_MS = 6000;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;

// A reading older than this loses Trust regardless of how the fetch went.
constexpr uint32_t QUOTA_STALENESS_LIMIT_S = 30 * 60;

// The length of each quota window, used only to place the pace marker on the
// progress bar (elapsed = windowLen - secondsToReset). These are Claude's fixed
// rate-limit windows, not something the bridge reports -- the cache carries the
// absolute reset time, not a duration, so the geometry has to live here. Marker
// placement is pure time arithmetic and never touches the reported utilization
// (ADR-0001): a wrong constant slides a hint, it cannot invent a percentage.
constexpr int32_t QUOTA_WINDOW_SESSION_S = 5 * 60 * 60;       // ~5h rolling
constexpr int32_t QUOTA_WINDOW_WEEKLY_S = 7 * 24 * 60 * 60;   // 7 days

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

// The navbar draws 36px tall but accepts touches over a taller strip: resistive
// panels and a stylus tip do not agree about where the edge is.
constexpr int16_t NAV_VISUAL_H = 36;
constexpr int16_t NAV_TOUCH_H = 56;

// The title band across the top of every screen, which the two global gestures
// -- hold to blank, tap to refresh -- do not apply to.
//
// Both gestures are aimed at the cards: "tap a card to refresh it" is a card
// affordance, and the band above them carries a heading and, on the Setting
// screen, the brightness stepper. Without this, every tap on a stepper button
// also queued a full refresh of all three feeds, whose fetches block the loop
// -- so the second tap on the button did not register until the network was
// done, and dimming the display felt like it was fighting the device. Nothing
// in this band is a card, so nothing in it should mean "refresh".
constexpr int16_t HEADER_TOUCH_H = 34;

// ---------------------------------------------------------------------------
// Backlight
// ---------------------------------------------------------------------------

// Percentages, because that is what the Setting screen shows and what gets
// stored -- the duty cycle is derived at the one place that talks to LEDC.
//
// The floor is 10 rather than 0: at 0 the panel is indistinguishable from a
// device that has crashed, and the display already has a way to go dark that
// says so on purpose (hold anywhere above the navbar). A control that can
// silently produce the same result as a fault is a control that will one day
// be mistaken for one.
constexpr uint8_t BACKLIGHT_MIN_PCT = 10;
constexpr uint8_t BACKLIGHT_MAX_PCT = 100;
constexpr uint8_t BACKLIGHT_STEP_PCT = 10;
constexpr uint8_t BACKLIGHT_DEFAULT_PCT = 100;

// ---------------------------------------------------------------------------
// Colour ramp thresholds
// ---------------------------------------------------------------------------

constexpr int8_t RAMP_WARN_PCT = 60;
constexpr int8_t RAMP_ALARM_PCT = 85;
