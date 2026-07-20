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

struct WeatherSnapshot {
  bool trusted = false;
  float temperatureC = 0.0f;
  float apparentC = 0.0f;
  uint8_t code = 0;  // WMO weather interpretation code, as Open-Meteo reports it
  bool isDay = true;
  int16_t humidityPct = -1;
};

struct CryptoSnapshot {
  bool trusted = false;
  float priceUsd = 0.0f;
  float change24hPct = 0.0f;
};

// ---------------------------------------------------------------------------
// The whole displayable world
// ---------------------------------------------------------------------------

struct AppModel {
  QuotaSnapshot quota;
  HistorySnapshot history;
  WeatherSnapshot weather;
  CryptoSnapshot crypto;

  FeedStatus feeds[(size_t)Feed::Count];

  bool wifiAssociated = false;
  int32_t rssi = 0;

  FeedStatus &status(Feed feed) { return feeds[(size_t)feed]; }
  const FeedStatus &status(Feed feed) const { return feeds[(size_t)feed]; }
};
