#include "bridge.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "../config.h"
#include "../secrets.h"

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

  const String url = String(BRIDGE_BASE_URL) + BRIDGE_PATH_QUOTA;
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

}  // namespace net
