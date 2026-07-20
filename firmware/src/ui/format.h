// Turning model values into the strings the screens show.
//
// Separated from the screens because these are the rules the design implies --
// what Unknown looks like, how a duration is abbreviated -- and they have to
// read the same on every screen. Host-compilable, so they can be exercised
// without a board.
#pragma once

#include <cstdint>
#include <cstdio>

#include "../model.h"

namespace format {

// Compact, human-scaled: "6d 8h", "1h 22m", "47m 12s", "31s". Deliberately
// drops to two units -- "6d 8h 14m 09s" is precision nobody reads.
inline void duration(int32_t seconds, char *out, size_t len) {
  if (seconds < 0) {
    snprintf(out, len, "--");
    return;
  }
  if (seconds == 0) {
    snprintf(out, len, "now");
    return;
  }

  const long days = seconds / 86400;
  const long hours = (seconds % 86400) / 3600;
  const long minutes = (seconds % 3600) / 60;
  const long secs = seconds % 60;

  if (days > 0) {
    snprintf(out, len, "%ldd %ldh", days, hours);
  } else if (hours > 0) {
    snprintf(out, len, "%ldh %02ldm", hours, minutes);
  } else if (minutes > 0) {
    snprintf(out, len, "%ldm %02lds", minutes, secs);
  } else {
    snprintf(out, len, "%lds", secs);
  }
}

// Utilization, or the Unknown marker. Never a stale figure, never a zero
// standing in for "no reading" -- see ADR-0001.
inline void utilization(const QuotaWindow &window, char *out, size_t len) {
  if (!window.trusted || window.utilization < 0) {
    snprintf(out, len, "--");
  } else {
    snprintf(out, len, "%d%%", (int)window.utilization);
  }
}

}  // namespace format
