// The values every screen renders and every feed produces.
//
// This header is the seam between the two halves of the firmware: nothing here
// knows about LVGL, and nothing here knows about HTTP. The net/ layer fills
// these structs in; the ui/ layer reads them. Keeping both sides ignorant of
// each other is what lets the parsers be compiled and exercised on the host.
//
// Vocabulary follows CONTEXT.md. In particular "trusted" is not "the fetch
// worked" -- a healthy 200 response can still carry values too stale to show.
// Deliberately <cstdint> and not <Arduino.h>: this header has to compile on the
// host so the simulator and the parser tests can use it.
#pragma once

#include <cstddef>
#include <cstdint>

// ---------------------------------------------------------------------------
// Feeds
// ---------------------------------------------------------------------------

enum class Feed : uint8_t {
  Bridge = 0,  // quota, via the Mac
  Weather,     // Open-Meteo, direct
  Crypto,      // CoinGecko, direct
  Stock,       // Finnhub, direct, one symbol per pass
  Count,
};

// Why the last refresh attempt ended as it did. A transport-level fact, kept
// well away from the displayed values -- see CONTEXT.md, "Fetch Outcome".
enum class FetchOutcome : uint8_t {
  Never = 0,    // no attempt yet since boot
  Ok,           // response received and parsed
  Offline,      // no WiFi association
  Unreachable,  // DNS, connect or read failed
  Rejected,     // reached the server, got a non-200
  Malformed,    // 200, but the body did not parse
};

const char *describe(FetchOutcome outcome);

struct FeedStatus {
  FetchOutcome outcome = FetchOutcome::Never;
  uint32_t lastAttemptMs = 0;
  uint32_t lastSuccessMs = 0;  // 0 means "never succeeded"
  uint16_t consecutiveFailures = 0;
};

// ---------------------------------------------------------------------------
// Quota
// ---------------------------------------------------------------------------

// Sentinels for Unknown. Zero is a real utilization and a real price, so it
// can never double as "no value".
constexpr int8_t UTILIZATION_UNKNOWN = -1;
constexpr int32_t RESET_UNKNOWN = -1;

struct QuotaWindow {
  bool trusted = false;
  int8_t utilization = UTILIZATION_UNKNOWN;  // whole percent, 0..100
  int32_t secondsToReset = RESET_UNKNOWN;    // counted down locally between polls
};

struct QuotaSnapshot {
  // Trust for the pair. The bridge reports it once: either the utilization
  // cache behind both windows is fresh enough to believe, or neither window is.
  bool trusted = false;
  QuotaWindow session;  // ~5 hours, shown as "Current"
  QuotaWindow weekly;

  uint32_t stalenessSeconds = 0;  // age of the reading, not of the fetch
  uint64_t sessionTokenTotal = 0;  // reported, never used to derive utilization
  uint32_t sessionMessageCount = 0;
  char profile[24] = {0};
};

// One recorded reading, as served by the bridge's history endpoint and plotted
// on the Weekly Usage screen.
struct Sample {
  uint32_t recordedAt = 0;  // unix seconds
  int8_t sessionUtilization = UTILIZATION_UNKNOWN;
  int8_t weeklyUtilization = UTILIZATION_UNKNOWN;
};

constexpr uint8_t HISTORY_CAPACITY = 56;  // 7 days at 3-hourly resolution

struct HistorySnapshot {
  bool trusted = false;
  uint8_t count = 0;
  Sample samples[HISTORY_CAPACITY];
};

// ---------------------------------------------------------------------------
// Ambient feeds
// ---------------------------------------------------------------------------

// The WMO code space is a hundred values deep and distinguishes light drizzle
// from moderate drizzle. At 48 pixels on a panel read from across a room those
// are the same picture, so the codes collapse into the set below and the icon
// generator draws one glyph per condition rather than one per code.
enum class WeatherCondition : uint8_t {
  Clear = 0,
  PartlyCloudy,
  Cloudy,
  Fog,
  Rain,
  Snow,
  Storm,
};

WeatherCondition conditionFor(uint8_t wmoCode);
const char *describe(WeatherCondition condition);

struct WeatherSnapshot {
  bool trusted = false;
  float temperatureC = 0.0f;
  float apparentC = 0.0f;
  uint8_t code = 0;  // WMO weather interpretation code, as Open-Meteo reports it
  bool isDay = true;
  int16_t humidityPct = -1;

  WeatherCondition condition() const { return conditionFor(code); }
};

// The coins the Crypto screen tracks, in the order its toggle presents them.
// An enum rather than three named fields: the screen shows one at a time and
// steps through them, and stepping through named fields means a switch on every
// read that a new coin can be left out of silently.
enum class Coin : uint8_t {
  BTC = 0,
  ETH,
  BNB,
  Count,
};

// What CoinGecko reports for one coin, and nothing more. The 24-hour price move
// in dollars is *not* here even though the screen shows it: it is exact algebra
// over the two fields below -- see format::change24hUsd -- and a derived figure
// sitting in a snapshot beside fetched ones is the sort of thing that later
// gets treated as though the server said it.
struct CoinQuote {
  bool trusted = false;
  float priceUsd = 0.0f;
  float change24hPct = 0.0f;
  float volume24hUsd = 0.0f;
};

// The id CoinGecko knows a coin by, the ticker the toggle prints, and the name
// the title carries. One table, so adding a coin cannot leave the request
// asking for one set and the screen labelling another.
const char *coinId(Coin coin);
const char *coinTicker(Coin coin);
const char *coinName(Coin coin);

struct CryptoSnapshot {
  // Per-coin, because the response can carry a price for one and nothing for
  // another -- a coin id that CoinGecko has retired comes back as a 200 with
  // that key simply absent. One screen-wide trusted flag would either condemn
  // the coins that did arrive or vouch for the one that did not.
  CoinQuote coins[(size_t)Coin::Count];

  const CoinQuote &coin(Coin which) const { return coins[(size_t)which]; }
  CoinQuote &coin(Coin which) { return coins[(size_t)which]; }
};

// ---------------------------------------------------------------------------
// Stocks
// ---------------------------------------------------------------------------

// The tickers the Stock screen tracks, in the order the list prints them. An
// enum for the same reason Coin is one: the feed steps through them one symbol
// per pass and the screen reads them by index, so a symbol added here appears
// in the request and on the row without either being edited to match.
enum class Ticker : uint8_t {
  AAPL = 0,
  NVDA,
  TSLA,
  GOOG,
  MSFT,
  Count,
};

// The symbol Finnhub knows a ticker by, which is also the label the row prints
// -- for US equities the two are the same string, so one table serves both and
// the request can never ask about one company while the row names another.
const char *stockSymbol(Ticker ticker);

// What Finnhub's /quote reports for one symbol, pared to what the row shows.
// The change is the move against the previous close as a percentage, exactly
// Finnhub's `dp` field -- not derived here, so ADR-0001's "reported, never
// synthesized" holds the same way it does for the coins.
struct StockQuote {
  bool trusted = false;
  float priceUsd = 0.0f;   // Finnhub `c`, the last trade
  float changePct = 0.0f;  // Finnhub `dp`, percent against the previous close
};

// Whether the US regular session is open right now, derived from the clock and
// never from a feed -- Finnhub's free /quote carries no session flag. Unknown
// until NTP has set the RTC, because a badge that guesses "closed" while the
// clock is blank is a claim the display cannot back. Holidays are not modelled;
// the badge reads "open" on the ten-odd exchange holidays a year (ADR-0004).
enum class MarketSession : uint8_t {
  Unknown = 0,
  Open,
  Closed,
};

struct StockSnapshot {
  // Per-symbol trust, like the coins: the feed fetches one symbol at a time, so
  // a rejected or malformed reply blanks the one row it was for and leaves the
  // four fetched moments earlier alone.
  StockQuote quotes[(size_t)Ticker::Count];
  MarketSession session = MarketSession::Unknown;

  const StockQuote &quote(Ticker which) const { return quotes[(size_t)which]; }
  StockQuote &quote(Ticker which) { return quotes[(size_t)which]; }
};

// ---------------------------------------------------------------------------
// The whole displayable world
// ---------------------------------------------------------------------------

struct AppModel {
  QuotaSnapshot quota;
  HistorySnapshot history;
  WeatherSnapshot weather;
  CryptoSnapshot crypto;
  StockSnapshot stocks;

  FeedStatus feeds[(size_t)Feed::Count];

  // What the Setting screen reports about the link. Recorded here rather than
  // read from WiFi.h where it is shown, so the ui layer stays ignorant of the
  // radio for the same reason it stays ignorant of HTTP -- and so the Setting
  // screen can be compiled and exercised on the host like every other one.
  // Local wall clock as "HH:MM", empty until NTP has set the RTC. A rendered
  // string rather than a time_t for the same reason the rest of this header
  // carries no library types: the ui layer reads it and must not have to know
  // about <time.h>, a zone or a format.
  char wallClock[6] = {0};

  bool wifiAssociated = false;
  int32_t rssi = 0;
  char ipAddress[16] = {0};   // dotted quad, empty until associated
  const char *ssid = "";      // points at the compiled-in literal
  const char *bridgeUrl = "";  // likewise; neither changes without a reflash

  FeedStatus &status(Feed feed) { return feeds[(size_t)feed]; }
  const FeedStatus &status(Feed feed) const { return feeds[(size_t)feed]; }
};
