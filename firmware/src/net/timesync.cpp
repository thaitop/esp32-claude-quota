#include "timesync.h"

#include <Arduino.h>
#include <cstdlib>
#include <cstring>
#include <time.h>

#include "../config.h"
#include "config_store.h"
#include "esp_sntp.h"

namespace net {
namespace {

bool synced = false;

// Turn CLOCK_TZ into the POSIX TZ string libc's tzset() wants.
//
// A human writes the offset the way they say it -- "UTC+7", "UTC-5",
// "UTC+5:30", plain "UTC". POSIX spells the same offset backwards (east is
// negative) and names the zone first, so "UTC+7" must become "<+0700>-7". This
// does that conversion so secrets.h never has to.
//
// Anything without a "UTC"/"GMT" prefix is assumed to already be a POSIX string
// (e.g. "EST5EDT,M3.2.0,M11.1.0" for a DST zone this shorthand cannot express)
// and is copied through untouched.
void buildPosixTz(const char *in, char *out, size_t n) {
  const bool isOffset = strncasecmp(in, "UTC", 3) == 0 || strncasecmp(in, "GMT", 3) == 0;
  if (!isOffset) {
    snprintf(out, n, "%s", in);
    return;
  }

  // Read the signed offset that follows the prefix. No sign at all (bare "UTC")
  // means zero.
  const char *p = in + 3;
  int sign = 1;
  if (*p == '+' || *p == '-') {
    sign = (*p == '-') ? -1 : 1;
    p++;
  }
  const int hours = atoi(p);
  int mins = 0;
  if (const char *colon = strchr(p, ':')) mins = atoi(colon + 1);

  const int east = sign * (hours * 60 + mins);  // minutes east of UTC
  const int west = -east;                       // POSIX offset (west-positive)
  const int am = abs(east);
  const int wm = abs(west);

  char name[10];
  snprintf(name, sizeof(name), "<%c%02d%02d>", east < 0 ? '-' : '+', am / 60, am % 60);
  if (wm % 60)
    snprintf(out, n, "%s%c%d:%02d", name, west < 0 ? '-' : '+', wm / 60, wm % 60);
  else
    snprintf(out, n, "%s%c%d", name, west < 0 ? '-' : '+', wm / 60);
}

}  // namespace

bool clockSynced() { return synced; }

bool syncClock() {
  if (synced) return true;

  char tz[24];
  buildPosixTz(configClockTz(), tz, sizeof(tz));
  configTzTime(tz, NTP_SERVER);

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
    Serial.printf("clock synced from %s: %s %s (%s)\n", NTP_SERVER, stamp,
                  configClockTz(), tz);
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
