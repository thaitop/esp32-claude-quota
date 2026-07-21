#include "market.h"

#include <time.h>

#include "../config.h"
#include "timesync.h"

namespace net {
namespace {

// Day of week for a Gregorian date, 0 = Sunday. Sakamoto's method. Used to find
// the nth Sunday a DST rule names without building a time_t -- timegm() is not
// dependable on this newlib, and the rest of this file only ever compares
// broken-down UTC fields anyway.
int dayOfWeek(int year, int month, int day) {
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (month < 3) year -= 1;
  return (year + year / 4 - year / 100 + year / 400 + t[month - 1] + day) % 7;
}

// Day of month of the nth Sunday in a given month.
int nthSunday(int year, int month, int nth) {
  const int firstSunday = 1 + ((7 - dayOfWeek(year, month, 1)) % 7);
  return firstSunday + (nth - 1) * 7;
}

// Orders a UTC instant against a (month, day, hour) boundary in the same year:
// negative before it, zero on the hour, positive after. Minute precision is not
// carried -- the switch happens on the hour and a badge does not need to be
// right to the minute across it.
int compareUtc(const struct tm &u, int month, int day, int hour) {
  const int um = u.tm_mon + 1;
  if (um != month) return um < month ? -1 : 1;
  if (u.tm_mday != day) return u.tm_mday < day ? -1 : 1;
  if (u.tm_hour != hour) return u.tm_hour < hour ? -1 : 1;
  return 0;
}

// Whether US Eastern is on daylight time at this UTC instant. DST runs from the
// second Sunday of March at 07:00 UTC (02:00 EST) to the first Sunday of
// November at 06:00 UTC (02:00 EDT).
bool easternDst(const struct tm &u) {
  const int year = u.tm_year + 1900;
  const int startDay = nthSunday(year, 3, 2);
  const int endDay = nthSunday(year, 11, 1);
  return compareUtc(u, 3, startDay, 7) >= 0 && compareUtc(u, 11, endDay, 6) < 0;
}

}  // namespace

MarketSession marketSession() {
  if (!clockSynced()) return MarketSession::Unknown;

  // time() is UTC epoch regardless of the device's ICT zone, so this reads the
  // market's clock without disturbing the one the header shows.
  const time_t nowUtc = time(nullptr);
  struct tm utc;
  gmtime_r(&nowUtc, &utc);

  const int offset = easternDst(utc) ? -4 * 3600 : -5 * 3600;
  const time_t etEpoch = nowUtc + offset;
  struct tm et;
  gmtime_r(&etEpoch, &et);

  if (et.tm_wday == 0 || et.tm_wday == 6) return MarketSession::Closed;

  const int minute = et.tm_hour * 60 + et.tm_min;
  if (minute >= MARKET_OPEN_MINUTE && minute < MARKET_CLOSE_MINUTE) {
    return MarketSession::Open;
  }
  return MarketSession::Closed;
}

}  // namespace net
