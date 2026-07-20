#include "timesync.h"

#include <Arduino.h>
#include <time.h>

#include "../config.h"
#include "esp_sntp.h"

namespace net {
namespace {

bool synced = false;

}  // namespace

bool clockSynced() { return synced; }

bool syncClock() {
  if (synced) return true;

  configTzTime(CLOCK_TZ, NTP_SERVER);

  struct tm parts;
  synced = getLocalTime(&parts, NTP_WAIT_MS);

  // Stopped whether the packet arrived or not. Left running, SNTP wakes on its
  // own hourly schedule to re-poll -- a request the display does not need, and
  // one that fires from a callback rather than from the poller, which is the
  // one place ADR-0003 says a request may start.
  esp_sntp_stop();

  if (synced) {
    char stamp[20];
    strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &parts);
    Serial.printf("clock synced from %s: %s %s\n", NTP_SERVER, stamp, CLOCK_TZ);
  } else {
    Serial.printf("clock sync from %s failed, header stays blank\n", NTP_SERVER);
  }
  return synced;
}

void readClock(AppModel &model) {
  if (!synced) {
    model.wallClock[0] = '\0';
    return;
  }
  const time_t now = time(nullptr);
  struct tm parts;
  localtime_r(&now, &parts);
  strftime(model.wallClock, sizeof(model.wallClock), "%H:%M", &parts);
}

}  // namespace net
