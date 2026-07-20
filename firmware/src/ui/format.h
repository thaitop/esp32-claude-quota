// Turning model values into the strings the screens show.
//
// Separated from the screens because these are the rules the design implies --
// what Unknown looks like, how a duration is abbreviated -- and they have to
// read the same on every screen. Host-compilable, so they can be exercised
// without a board.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>

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

// The Unknown marker, spelled once so no screen invents its own.
constexpr const char *UNKNOWN = "--";

// "30.2°". The degree sign is U+00B0, which the generated faces carry --
// see tools/mkfont_lvgl.py. LVGL wants it UTF-8 encoded, hence the escape pair.
inline void temperature(float celsius, bool trusted, char *out, size_t len) {
  if (!trusted) {
    snprintf(out, len, "%s", UNKNOWN);
    return;
  }
  snprintf(out, len, "%.1f\xc2\xb0", (double)celsius);
}

// "$104,231", "$18.42", "$0.4832". The number of decimals follows the
// magnitude: four of them on a six-figure price is noise, none of them on a
// sub-dollar coin is a flat zero.
inline void price(float usd, bool trusted, char *out, size_t len) {
  if (!trusted) {
    snprintf(out, len, "%s", UNKNOWN);
    return;
  }

  if (usd < 10.0f) {
    snprintf(out, len, "$%.4f", (double)usd);
    return;
  }
  if (usd < 1000.0f) {
    snprintf(out, len, "$%.2f", (double)usd);
    return;
  }

  // Thousands separators by hand: the ESP32's newlocale support does not
  // include a grouping locale, so "%'d" prints a literal apostrophe.
  char digits[16];
  snprintf(digits, sizeof(digits), "%ld", (long)(usd + 0.5f));

  if (len == 0) return;

  const size_t count = strlen(digits);
  size_t written = 0;
  if (written + 1 < len) out[written++] = '$';
  for (size_t i = 0; i < count && written + 1 < len; i++) {
    if (i > 0 && (count - i) % 3 == 0) out[written++] = ',';
    if (written + 1 < len) out[written++] = digits[i];
  }
  // Always inside the buffer: the loop stops one short of the end, so this
  // slot exists even when nothing else fitted.
  out[written] = '\0';
}

// "+1.42%", "-0.83%". Always signed: the direction is the point, and an
// unsigned figure beside a colour is one fact told twice and one not at all.
inline void signedPercent(float pct, bool trusted, char *out, size_t len) {
  if (!trusted) {
    snprintf(out, len, "%s", UNKNOWN);
    return;
  }
  snprintf(out, len, "%+.2f%%", (double)pct);
}

// How long ago a millis() stamp was, for the Setting screen. Zero means the
// thing never happened, which is a different fact from "happened a long time
// ago" and is spelled differently -- see the Setting screen's feed rows.
inline void since(uint32_t stampMs, uint32_t nowMs, char *out, size_t len) {
  if (stampMs == 0) {
    snprintf(out, len, "never");
    return;
  }
  const int32_t seconds = (int32_t)((nowMs - stampMs) / 1000);
  if (seconds <= 0) {
    // duration() spells zero "now", and "now ago" is not a thing.
    snprintf(out, len, "just now");
    return;
  }
  char span[16];
  duration(seconds, span, sizeof(span));
  snprintf(out, len, "%s ago", span);
}

// Why a Screen is showing Unknown, in the words that separate "not set up yet"
// from "broken now".
//
// A Feed that has never once succeeded is a different problem from one that
// worked this morning and has since stopped: the first means the coordinates
// or the URL are wrong, the second means something is down. Rendering both as
// a bare `--` tells the reader to check the wrong thing.
inline void feedNote(const FeedStatus &status, uint32_t nowMs, char *out, size_t len) {
  if (status.outcome == FetchOutcome::Never) {
    snprintf(out, len, "waiting for the first fetch");
    return;
  }
  if (status.lastSuccessMs == 0) {
    snprintf(out, len, "%s -- has never succeeded", describe(status.outcome));
    return;
  }
  char age[24];
  since(status.lastSuccessMs, nowMs, age, sizeof(age));
  snprintf(out, len, "%s -- last good reading %s", describe(status.outcome), age);
}

}  // namespace format
