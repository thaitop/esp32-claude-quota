#include "screen_setting.h"

#include <Arduino.h>

#include "../display.h"
#include "fonts/ui_fonts.h"
#include "format.h"
#include "theme.h"
#include "ui_icons.h"
#include "widgets.h"

namespace ui {
namespace {

// Nine rows between the title and the navbar, in the smallest face the design
// already uses. The first pass set them in Inter 14 on an 18px pitch, which
// put the last row's bottom edge at 202 against a page 194 tall -- the uptime
// row was drawn underneath the navbar, where it is neither visible nor
// obviously missing. Inter 12 sets a 15px line, so a 16px pitch clears it.
//
//   36 + 9 * 16 = 180, against the 194 the page has.
//
// Nine rows is not negotiable: each one answers a different question about a
// stuck display, and the screen exists to stop the next one meaning a cable.
constexpr int16_t PAD = 12;
constexpr int16_t ROW_H = 16;
constexpr int16_t ROW_Y = 36;
constexpr int16_t VALUE_X = 106;

enum Row : uint8_t {
  RowLink = 0,
  RowAddress,
  RowBridgeUrl,
  RowBridgeFeed,
  RowWeatherFeed,
  RowCryptoFeed,
  RowQuotaAge,
  RowHeap,
  RowUptime,
  RowCount,
};

const char *const LABELS[RowCount] = {
    "WiFi", "IP", "Bridge", "Bridge feed", "Weather feed",
    "Crypto feed", "Quota age", "Free heap", "Uptime",
};

lv_obj_t *values[RowCount] = {nullptr};

// Rewriting nine labels a second is nine layout passes a second on a panel
// that redraws over SPI. Only the rows that actually changed are touched.
//
// Wide enough that no row is ever truncated -- the longest is the bridge URL.
// If one were, the comparison below would only see the truncated prefix and
// two different values sharing it would never repaint.
char shown[RowCount][56] = {{0}};

void setRow(Row row, const char *text, uint32_t colour) {
  if (strncmp(shown[row], text, sizeof(shown[row]) - 1) == 0) return;
  strncpy(shown[row], text, sizeof(shown[row]) - 1);
  shown[row][sizeof(shown[row]) - 1] = '\0';
  lv_label_set_text(values[row], text);
  lv_obj_set_style_text_color(values[row], theme::colour(colour), LV_PART_MAIN);
}

// A feed that has never succeeded is a different fact from one that worked and
// then stopped: the first is "not set up yet", the second is "broken now", and
// they call for different things being checked.
void setFeedRow(Row row, const FeedStatus &status, uint32_t nowMs) {
  char text[56];

  if (status.outcome == FetchOutcome::Never) {
    setRow(row, "not tried yet", theme::MUTED);
    return;
  }

  if (status.lastSuccessMs == 0) {
    snprintf(text, sizeof(text), "%s, never ok", describe(status.outcome));
    setRow(row, text, theme::RED);
    return;
  }

  char age[24];
  format::since(status.lastSuccessMs, nowMs, age, sizeof(age));
  snprintf(text, sizeof(text), "%s, %s", describe(status.outcome), age);
  setRow(row, text,
         status.outcome == FetchOutcome::Ok ? theme::GREEN : theme::AMBER);
}

}  // namespace

void buildSettingScreen(lv_obj_t *parent) {
  makeTitle(parent, "Setting", &icon_setting);

  for (uint8_t i = 0; i < RowCount; i++) {
    lv_obj_t *label = makeLabel(parent, &font_inter_12, theme::MUTED);
    lv_label_set_text(label, LABELS[i]);
    lv_obj_set_pos(label, PAD, ROW_Y + i * ROW_H);

    values[i] = makeLabel(parent, &font_inter_12, theme::TEXT);
    lv_label_set_text(values[i], format::UNKNOWN);
    lv_obj_set_pos(values[i], VALUE_X, ROW_Y + i * ROW_H);
  }

  lv_obj_update_layout(parent);
  warnIfCollapsed("setting rows", values[RowLink]);
}

void updateSettingScreen(const AppModel &model, uint32_t nowMs) {
  char text[56];

  if (model.wifiAssociated) {
    snprintf(text, sizeof(text), "%s  %d dBm", model.ssid, (int)model.rssi);
    setRow(RowLink, text, theme::GREEN);
    setRow(RowAddress, model.ipAddress, theme::TEXT);
  } else {
    setRow(RowLink, "not associated", theme::RED);
    setRow(RowAddress, format::UNKNOWN, theme::MUTED);
  }

  setRow(RowBridgeUrl, model.bridgeUrl, theme::TEXT);

  setFeedRow(RowBridgeFeed, model.status(Feed::Bridge), nowMs);
  setFeedRow(RowWeatherFeed, model.status(Feed::Weather), nowMs);
  setFeedRow(RowCryptoFeed, model.status(Feed::Crypto), nowMs);

  // Staleness is the age of the reading, not of the fetch: a healthy bridge
  // serving a cache the Claude Usage app stopped refreshing looks fine on the
  // feed row above and wrong here, which is exactly the distinction that
  // separates a dead bridge from a dead Claude Usage app.
  if (model.quota.trusted) {
    char age[16];
    format::duration((int32_t)model.quota.stalenessSeconds, age, sizeof(age));
    setRow(RowQuotaAge, age, theme::GREEN);
  } else {
    setRow(RowQuotaAge, "not trusted", theme::AMBER);
  }

  snprintf(text, sizeof(text), "%u KB", (unsigned)(display::freeHeap() / 1024));
  setRow(RowHeap, text, theme::TEXT);

  char span[16];
  format::duration((int32_t)(nowMs / 1000), span, sizeof(span));
  setRow(RowUptime, span, theme::TEXT);
}

}  // namespace ui
