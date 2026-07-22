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

// The old layout tried to hold the title, a Config chip, the theme toggle and
// the brightness stepper in one 320px header. Their widths sum past 340, so
// "Setting" was drawn straight over "Config" and the band read as clutter. The
// redesign spends the vertical budget instead of the horizontal one:
//
//   - Header: title + theme toggle + brightness. Nothing reaches the controls,
//     so nothing overlaps.
//   - Seven diagnostic rows, one per network/feed fact, full width.
//   - One stats line folding the three cheap counters (quota age, heap, uptime)
//     into a single row of label+value pairs -- they are numbers, not
//     sentences, and three of them fit across the display with room to spare.
//   - A full-width Config button at the foot. The action that used to be a
//     cramped header chip is now the largest, lowest, easiest target on the
//     screen, which is what a resistive panel wants.
//
// The page is 202 tall (240 - 36 navbar - 2). Rows 38..149, stats ~156, the
// button 174..200. warnIfCollapsed at boot guards the row build.
constexpr int16_t PAD = 12;
constexpr int16_t ROW_H = 16;
constexpr int16_t ROW_Y = 38;
constexpr int16_t VALUE_X = 106;

// The stats line: three label+value pairs sharing one baseline. The x's are
// spaced so the widest each value can grow to ("untrusted", "9999 KB",
// "99d 23h") clears the next label.
constexpr int16_t STATS_Y = 156;
constexpr int16_t QUOTA_LX = 12;
constexpr int16_t QUOTA_VX = 58;
constexpr int16_t HEAP_LX = 150;
constexpr int16_t HEAP_VX = 192;
constexpr int16_t UP_LX = 246;
constexpr int16_t UP_VX = 274;

// The brightness stepper keeps its old right-edge home; with the Config chip
// gone from the band, the title no longer collides with anything.
constexpr int16_t BTN_W = 32;
constexpr int16_t BTN_H = 28;
constexpr int16_t BTN_Y = 2;
constexpr int16_t MINUS_X = 198;
constexpr int16_t LEVEL_X = 234;
constexpr int16_t LEVEL_W = 44;
constexpr int16_t PLUS_X = 282;

// The Dark/Light toggle sits just left of the brightness steppers. It shows the
// Mode a tap leads to, not the one it is in: a sun while Dark (tap for Light), a
// moon while Light (tap for Dark), the way a light switch is named by where it
// takes you. buildSettingScreen warns to serial if "Setting" ever grows into it.
constexpr int16_t THEME_ICON = 16;
constexpr int16_t THEME_ICON_X = MINUS_X - 8 - THEME_ICON;

// The Config button: full width along the foot, above the navbar. Styled like a
// stepper (card fill, accent label) so it reads as a control, sized as the
// primary action a glance is looking for.
constexpr int16_t CFG_Y = 174;
constexpr int16_t CFG_H = 26;
constexpr int16_t CFG_X = 8;
constexpr int16_t CFG_W = display::WIDTH - 2 * CFG_X;

void (*configCallback)() = nullptr;

lv_obj_t *brightnessLabel = nullptr;
uint8_t shownBrightness = 0;

// Kept past build() so a Mode switch can swap the toggle's own glyph, an image
// property no shared style covers.
lv_obj_t *themeIcon = nullptr;

enum Row : uint8_t {
  RowLink = 0,
  RowAddress,
  RowBridgeUrl,
  RowBridgeFeed,
  RowWeatherFeed,
  RowCryptoFeed,
  RowStockFeed,
  RowCount,
};

const char *const LABELS[RowCount] = {
    "WiFi", "IP", "Bridge", "Bridge feed", "Weather feed", "Crypto feed",
    "Stock feed",
};

lv_obj_t *values[RowCount] = {nullptr};

// The three counters that used to be their own rows now live on one line.
lv_obj_t *quotaValue = nullptr;
lv_obj_t *heapValue = nullptr;
lv_obj_t *uptimeValue = nullptr;

// Rewriting seven labels a second is seven layout passes a second on a panel
// that redraws over SPI. Only the rows that actually changed are touched.
//
// Wide enough that no row is ever truncated -- the longest is the bridge URL.
// If one were, the comparison below would only see the truncated prefix and
// two different values sharing it would never repaint.
char shown[RowCount][56] = {{0}};

// The stats line's own change-detection caches, the same trick the rows use.
char quotaShown[16] = {0};
char heapShown[16] = {0};
char uptimeShown[16] = {0};

void setLabelIfChanged(lv_obj_t *label, char *cache, size_t cap,
                       const char *text, uint32_t colour) {
  if (strncmp(cache, text, cap - 1) == 0) return;
  strncpy(cache, text, cap - 1);
  cache[cap - 1] = '\0';
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, theme::colour(colour), LV_PART_MAIN);
}

void setRow(Row row, const char *text, uint32_t colour) {
  setLabelIfChanged(values[row], shown[row], sizeof(shown[row]), text, colour);
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
  lv_obj_add_style(button, theme::borderStyle(theme::CARD_EDGE), LV_PART_MAIN);
  lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
  lv_obj_set_style_border_opa(button, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(button, onStepPressed, LV_EVENT_CLICKED,
                      (void *)(intptr_t)delta);

  lv_obj_t *label = makeLabel(button, &font_inter_22_bold, theme::ACCENT);
  lv_label_set_text(label, glyph);
  lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
}

// The glyph a tap leads to: the sun that Light gives you while Dark, the moon
// that Dark gives you while Light. Recoloured ACCENT to read as a control, the
// same tint the steppers wear.
const lv_image_dsc_t *themeGlyphFor(theme::Mode mode) {
  return mode == theme::Mode::Dark ? &glyph_brightness : &glyph_moon;
}

void paintThemeIcon() {
  lv_image_set_src(themeIcon, themeGlyphFor(theme::mode()));
  lv_obj_set_style_image_recolor(themeIcon, theme::colour(theme::ACCENT),
                                 LV_PART_MAIN);
  lv_obj_set_style_image_recolor_opa(themeIcon, LV_OPA_COVER, LV_PART_MAIN);
}

// A tap flips the Mode, which persists it, rewrites the shared styles in place
// and bumps the generation every screen watches. The glyph and its ACCENT tint
// are the one thing generation() cannot reach -- not gated on a model value --
// so the handler repaints them itself, now showing the Mode the next tap leaves.
void onThemePressed(lv_event_t *) {
  theme::toggle();
  paintThemeIcon();
}

// The glyph is a slim target for a resistive panel at the top edge, so a taller
// invisible strip behind it catches the press -- the same trick the navbar and
// the steppers use.
void buildThemeToggle(lv_obj_t *parent) {
  themeIcon = lv_image_create(parent);
  lv_obj_set_pos(themeIcon, THEME_ICON_X, BTN_Y + (BTN_H - THEME_ICON) / 2);
  paintThemeIcon();

  lv_obj_t *hit = lv_obj_create(parent);
  lv_obj_remove_style_all(hit);
  lv_obj_set_size(hit, THEME_ICON + 12, BTN_H);
  lv_obj_set_pos(hit, THEME_ICON_X - 6, BTN_Y);
  lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(hit, onThemePressed, LV_EVENT_CLICKED, nullptr);
}

void onConfigPressed(lv_event_t *) {
  if (configCallback != nullptr) configCallback();
}

// The one action on a screen full of readouts: a full-width button along the
// foot, styled like the brightness steppers so it reads as a control. Its own
// event rather than a card gesture, because it does not sit over the cards.
void buildConfigButton(lv_obj_t *parent) {
  lv_obj_t *button = makePanel(parent, CFG_W, CFG_H, 8, theme::CARD);
  lv_obj_set_pos(button, CFG_X, CFG_Y);
  lv_obj_add_style(button, theme::borderStyle(theme::CARD_EDGE), LV_PART_MAIN);
  lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
  lv_obj_set_style_border_opa(button, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(button, onConfigPressed, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *label = makeLabel(button, &font_inter_15, theme::ACCENT);
  lv_label_set_text(label, "Config Mode");
  lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
}

// One label+value pair on the stats line. The static caption is drawn once; the
// value it introduces is returned so the update pass can rewrite it.
lv_obj_t *makeStat(lv_obj_t *parent, const char *caption, int16_t labelX,
                   int16_t valueX) {
  lv_obj_t *label = makeLabel(parent, &font_inter_12, theme::MUTED);
  lv_label_set_text(label, caption);
  lv_obj_set_pos(label, labelX, STATS_Y);

  lv_obj_t *value = makeLabel(parent, &font_inter_12, theme::TEXT);
  lv_label_set_text(value, format::UNKNOWN);
  lv_obj_set_pos(value, valueX, STATS_Y);
  return value;
}

}  // namespace

void buildSettingScreen(lv_obj_t *parent, void (*onConfig)()) {
  configCallback = onConfig;

  lv_obj_t *title = makeTitle(parent, "Setting", &icon_setting_hdr);

  buildThemeToggle(parent);

  // The title and the theme toggle share the band; if the title ever reaches
  // into the toggle the tripwire says so, the same way warnIfCollapsed guards
  // the rows below.
  lv_obj_update_layout(parent);
  if (lv_obj_get_x(title) + lv_obj_get_width(title) > THEME_ICON_X) {
    Serial.printf("setting title runs to %d, theme toggle starts at %d\n",
                  (int)(lv_obj_get_x(title) + lv_obj_get_width(title)),
                  (int)THEME_ICON_X);
  }

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

  quotaValue = makeStat(parent, "Quota", QUOTA_LX, QUOTA_VX);
  heapValue = makeStat(parent, "Heap", HEAP_LX, HEAP_VX);
  uptimeValue = makeStat(parent, "Up", UP_LX, UP_VX);

  buildConfigButton(parent);

  lv_obj_update_layout(parent);
  warnIfCollapsed("setting rows", values[RowLink]);
}

void updateSettingScreen(const AppModel &model, uint32_t nowMs) {
  char text[56];

  // A Mode switch moved the health colours on every row (green/amber/red);
  // forget the caches so each label repaints in the new Mode.
  static uint8_t shownGen = 0;
  if (theme::generation() != shownGen) {
    shownGen = theme::generation();
    memset(shown, 0, sizeof(shown));
    quotaShown[0] = heapShown[0] = uptimeShown[0] = '\0';
  }

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
  // feed rows above and wrong here, which is exactly the distinction that
  // separates a dead bridge from a dead Claude Usage app.
  if (model.quota.trusted) {
    char age[16];
    format::duration((int32_t)model.quota.stalenessSeconds, age, sizeof(age));
    setLabelIfChanged(quotaValue, quotaShown, sizeof(quotaShown), age,
                      theme::GREEN);
  } else {
    setLabelIfChanged(quotaValue, quotaShown, sizeof(quotaShown), "untrusted",
                      theme::AMBER);
  }

  snprintf(text, sizeof(text), "%u KB", (unsigned)(display::freeHeap() / 1024));
  setLabelIfChanged(heapValue, heapShown, sizeof(heapShown), text, theme::TEXT);

  char span[16];
  format::duration((int32_t)(nowMs / 1000), span, sizeof(span));
  setLabelIfChanged(uptimeValue, uptimeShown, sizeof(uptimeShown), span,
                    theme::TEXT);
}

}  // namespace ui