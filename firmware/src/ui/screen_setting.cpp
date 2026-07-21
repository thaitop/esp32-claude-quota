#include "screen_setting.h"

#include <Arduino.h>

#include "../config.h"
#include "../display.h"
#include "fonts/ui_fonts.h"
#include "format.h"
#include "theme.h"
#include "ui_icons.h"
#include "widgets.h"

namespace ui {
namespace {

// Ten rows between the title and the navbar, in the smallest face the design
// already uses. The first pass set them in Inter 14 on an 18px pitch, which
// put the last row's bottom edge at 202 against a page 194 tall -- the uptime
// row was drawn underneath the navbar, where it is neither visible nor
// obviously missing. Inter 12 sets a 15px line, so a 16px pitch clears it.
//
// The stock feed made it ten, which needed the two pixels back that a nine-row
// start could spare:
//
//   34 + 9 * 16 = 178, +15 line = 193, against the 194 the page has.
//
// Every row is not negotiable: each one answers a different question about a
// stuck display, and the screen exists to stop the next one meaning a cable.
constexpr int16_t PAD = 12;
constexpr int16_t ROW_H = 16;
constexpr int16_t ROW_Y = 34;
constexpr int16_t VALUE_X = 106;

// The brightness control rides in the header rather than joining the rows
// below it. Nine rows already reach 180 of the page's 194, so a tenth would
// have to displace one -- and every one of them is a diagnostic that exists
// because a stuck display once meant checking a cable. The header's right half
// is empty next to a five-letter title, and a control that is a control wants
// to look unlike the readouts underneath it anyway.
// A sun opens the group, because "- 70% +" on its own names no quantity. It is
// the same sun the Weather screen draws for a clear day, tinted to sit with
// the labels rather than shout over them.
constexpr int16_t GLYPH_SIZE = 16;
constexpr int16_t GLYPH_X = 170;
constexpr int16_t BTN_W = 32;
constexpr int16_t BTN_H = 28;
constexpr int16_t BTN_Y = 2;
constexpr int16_t MINUS_X = 198;
constexpr int16_t LEVEL_X = 234;
constexpr int16_t LEVEL_W = 44;
constexpr int16_t PLUS_X = 282;

lv_obj_t *brightnessLabel = nullptr;
uint8_t shownBrightness = 0;

enum Row : uint8_t {
  RowLink = 0,
  RowAddress,
  RowBridgeUrl,
  RowBridgeFeed,
  RowWeatherFeed,
  RowCryptoFeed,
  RowStockFeed,
  RowQuotaAge,
  RowHeap,
  RowUptime,
  RowCount,
};

const char *const LABELS[RowCount] = {
    "WiFi", "IP", "Bridge", "Bridge feed", "Weather feed",
    "Crypto feed", "Stock feed", "Quota age", "Free heap", "Uptime",
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

// Zero is not a value the label can show, so it doubles as "nothing drawn
// yet" and the first update always paints.
void showBrightness() {
  const uint8_t level = display::brightness();
  if (level == shownBrightness) return;
  shownBrightness = level;

  char text[8];
  snprintf(text, sizeof(text), "%u%%", (unsigned)level);
  lv_label_set_text(brightnessLabel, text);
}

void onStepPressed(lv_event_t *event) {
  const int delta = (int)(intptr_t)lv_event_get_user_data(event);

  // Stepped in int and clamped before it narrows: 10 - 10 in uint8_t is 0,
  // which the display layer would clamp back up to the floor, and the button
  // at the bottom of its range would look like it had done nothing twice.
  int next = (int)display::brightness() + delta * (int)BACKLIGHT_STEP_PCT;
  if (next < BACKLIGHT_MIN_PCT) next = BACKLIGHT_MIN_PCT;
  if (next > BACKLIGHT_MAX_PCT) next = BACKLIGHT_MAX_PCT;

  display::setBrightness((uint8_t)next);
  showBrightness();
}

// A tap has to land somewhere it can be felt. The panel is the target itself
// rather than a taller strip behind it, the way the navbar does it -- these
// sit at the top of the display where a resistive panel's error is smallest,
// and there is nothing adjacent for a stray press to trigger.
void makeStepButton(lv_obj_t *parent, int16_t x, const char *glyph, int delta) {
  lv_obj_t *button = makePanel(parent, BTN_W, BTN_H, 8, theme::CARD);
  lv_obj_set_pos(button, x, BTN_Y);
  lv_obj_set_style_border_color(button, theme::colour(theme::CARD_EDGE),
                                LV_PART_MAIN);
  lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
  lv_obj_set_style_border_opa(button, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(button, onStepPressed, LV_EVENT_CLICKED,
                      (void *)(intptr_t)delta);

  lv_obj_t *label = makeLabel(button, &font_inter_22_bold, theme::ACCENT);
  lv_label_set_text(label, glyph);
  lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
}

}  // namespace

void buildSettingScreen(lv_obj_t *parent) {
  makeTitle(parent, "Setting", &icon_setting_hdr);

  lv_obj_t *sun = lv_image_create(parent);
  lv_image_set_src(sun, &glyph_brightness);
  lv_obj_set_pos(sun, GLYPH_X, BTN_Y + (BTN_H - GLYPH_SIZE) / 2);
  lv_obj_set_style_image_recolor(sun, theme::colour(theme::MUTED), LV_PART_MAIN);
  lv_obj_set_style_image_recolor_opa(sun, LV_OPA_COVER, LV_PART_MAIN);

  makeStepButton(parent, MINUS_X, "-", -1);
  makeStepButton(parent, PLUS_X, "+", +1);

  brightnessLabel = makeLabel(parent, &font_inter_15, theme::TEXT);
  lv_obj_set_size(brightnessLabel, LEVEL_W, BTN_H);
  lv_obj_set_style_text_align(brightnessLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_pos(brightnessLabel, LEVEL_X, BTN_Y + (BTN_H - 18) / 2);
  shownBrightness = 0;
  showBrightness();

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

  // Change-detecting like every other readout here, so the one-second tick
  // costs nothing while the level is not being changed.
  showBrightness();

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
  setFeedRow(RowStockFeed, model.status(Feed::Stock), nowMs);

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
