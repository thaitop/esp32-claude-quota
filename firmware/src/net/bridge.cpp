#include "bridge.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "../config.h"
#include "../secrets.h"
#include "config_store.h"
#include "poller.h"

namespace net {
namespace {

// Reads a value that the bridge sends as null when it has nothing
// trustworthy. Null becomes the Unknown sentinel rather than zero: zero is a
// real utilization and a real countdown.
int8_t readUtilization(JsonVariantConst value) {
  if (value.isNull()) return UTILIZATION_UNKNOWN;
  const int raw = value.as<int>();
  if (raw < 0 || raw > 100) return UTILIZATION_UNKNOWN;
  return (int8_t)raw;
}

int32_t readSeconds(JsonVariantConst value) {
  if (value.isNull()) return RESET_UNKNOWN;
  return value.as<int32_t>();
}

void clearQuota(QuotaSnapshot &quota) {
  quota.trusted = false;
  quota.session = QuotaWindow{};
  quota.weekly = QuotaWindow{};
}

// History runs on its own timer inside the Bridge Feed's slot. Kept here
// rather than in the poller so there is still exactly one request in flight at
// a time: this only ever runs after the quota fetch has already finished.
uint32_t historyDueAtMs = 0;
uint16_t historyFailures = 0;
uint32_t historyRefreshSeen = 0;

}  // namespace

FetchOutcome fetchQuota(AppModel &model) {
  if (WiFi.status() != WL_CONNECTED) {
    clearQuota(model.quota);
    return FetchOutcome::Offline;
  }

  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setReuse(false);

  const String url = String(configBridgeUrl()) + BRIDGE_PATH_QUOTA;
  if (!http.begin(url)) {
    clearQuota(model.quota);
    return FetchOutcome::Unreachable;
  }

  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    http.end();
    clearQuota(model.quota);
    return status < 0 ? FetchOutcome::Unreachable : FetchOutcome::Rejected;
  }

  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, http.getStream());
  http.end();

  if (error) {
    clearQuota(model.quota);
    return FetchOutcome::Malformed;
  }

  QuotaSnapshot &quota = model.quota;
  quota.trusted = doc["trusted"] | false;
  quota.stalenessSeconds = doc["stalenessSeconds"] | 0;
  quota.sessionTokenTotal = doc["sessionTokenTotal"] | 0ULL;
  quota.sessionMessageCount = doc["sessionMessageCount"] | 0UL;

  const char *profile = doc["profile"] | "";
  strncpy(quota.profile, profile, sizeof(quota.profile) - 1);
  quota.profile[sizeof(quota.profile) - 1] = '\0';

  quota.session.utilization = readUtilization(doc["session"]["utilization"]);
  quota.session.secondsToReset = readSeconds(doc["session"]["secondsToReset"]);
  quota.weekly.utilization = readUtilization(doc["weekly"]["utilization"]);
  quota.weekly.secondsToReset = readSeconds(doc["weekly"]["secondsToReset"]);

  // A window is only shown if the bridge vouched for the reading and the
  // number survived parsing. Either failure alone is enough to show Unknown.
  quota.session.trusted = quota.trusted && quota.session.utilization >= 0;
  quota.weekly.trusted = quota.trusted && quota.weekly.utilization >= 0;

  // Also refuse a reading the bridge considers fresh but which is older than
  // this firmware is willing to believe.
  if (quota.stalenessSeconds > QUOTA_STALENESS_LIMIT_S) {
    quota.trusted = false;
    quota.session.trusted = false;
    quota.weekly.trusted = false;
  }

  return FetchOutcome::Ok;
}

FetchOutcome fetchHistory(AppModel &model) {
  if (WiFi.status() != WL_CONNECTED) return FetchOutcome::Offline;

  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setReuse(false);

  const String url = String(configBridgeUrl()) + BRIDGE_PATH_HISTORY;
  if (!http.begin(url)) return FetchOutcome::Unreachable;

  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    http.end();
    return status < 0 ? FetchOutcome::Unreachable : FetchOutcome::Rejected;
  }

  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, http.getStream());
  http.end();

  if (error) return FetchOutcome::Malformed;

  JsonArrayConst samples = doc["samples"];
  if (samples.isNull()) return FetchOutcome::Malformed;

  // Built into a local and copied over only once it has parsed. A partial
  // write into the model would leave the chart drawing half of the new week
  // and half of the old one, which reads as a real trend.
  HistorySnapshot fetched;
  for (JsonObjectConst sample : samples) {
    if (fetched.count >= HISTORY_CAPACITY) break;
    Sample &slot = fetched.samples[fetched.count];
    slot.recordedAt = sample["recordedAt"] | 0UL;
    slot.sessionUtilization = readUtilization(sample["session"]);
    slot.weeklyUtilization = readUtilization(sample["weekly"]);
    fetched.count++;
  }

  // An empty history is a successful fetch of a bridge that has not recorded
  // anything yet -- the first days after setup. The Weekly screen says so
  // rather than showing an empty plot, so trust follows the count.
  fetched.trusted = fetched.count > 0;
  model.history = fetched;
  return FetchOutcome::Ok;
}

FetchOutcome fetchBridge(AppModel &model) {
  const FetchOutcome quotaOutcome = fetchQuota(model);
  if (quotaOutcome != FetchOutcome::Ok) return quotaOutcome;

  const uint32_t now = millis();

  // A manual refresh reaches the history too, even though the poller cannot
  // see its timer. Tapping the Weekly screen and watching the chart sit there
  // for ten minutes is the wait the gesture exists to skip.
  const uint32_t generation = refreshGeneration();
  const bool forced = generation != historyRefreshSeen;
  historyRefreshSeen = generation;

  if (!forced && (int32_t)(now - historyDueAtMs) < 0) return quotaOutcome;

  const FetchOutcome historyOutcome = fetchHistory(model);
  if (historyOutcome == FetchOutcome::Ok) {
    historyFailures = 0;
    historyDueAtMs = now + POLL_HISTORY_MS;
  } else {
    // The Samples already held stay held and stay trusted. A dropped poll is
    // not evidence that the week that already happened did not happen, and
    // blanking a chart on one unreachable fetch is the failure the Weekly
    // screen is specifically required not to have.
    if (historyFailures < 5) historyFailures++;
    historyDueAtMs = now + (POLL_RETRY_MS << historyFailures);
  }

  return quotaOutcome;
}

}  // namespace net
