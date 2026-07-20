#include "screen_weather.h"

#include <Arduino.h>

#include "../display.h"
#include "fonts/ui_fonts.h"
#include "format.h"
#include "theme.h"
#include "ui_icons.h"
#include "widgets.h"

namespace ui {
namespace {

constexpr int16_t PAD = 12;
constexpr int16_t CARD_Y = 42;
constexpr int16_t CARD_H = 118;
constexpr int16_t GLYPH = 48;
// The glyph is inset from the card's left edge by the same amount the text is
// from the glyph, so the pair sits as one block rather than as an icon with a
// caption drifting away from it.
constexpr int16_t GLYPH_X = 22;
constexpr int16_t TEXT_X = GLYPH_X + GLYPH + 22;

lv_obj_t *card = nullptr;
lv_obj_t *glyph = nullptr;
lv_obj_t *reading = nullptr;
lv_obj_t *condition = nullptr;
lv_obj_t *feels = nullptr;
lv_obj_t *footnote = nullptr;

// What was last drawn. Kept as the snapshot itself rather than as a hash of
// it: the first pass folded the fields into one int with hand-picked strides,
// and a 45 degree apparent temperature overflowed its stride into the actual
// temperature's, so two different readings compared equal and the screen kept
// showing the older one. A field-by-field comparison cannot alias.
WeatherSnapshot shown;
bool everShown = false;

// Day and night differ only where the sky itself is visible. A cloudy night
// and a cloudy day are the same picture, and drawing two would be two glyphs
// in flash to say one thing.
const lv_image_dsc_t *glyphFor(WeatherCondition condition, bool isDay) {
  switch (condition) {
    case WeatherCondition::Clear:
      return isDay ? &wx_clear_day : &wx_clear_night;
    case WeatherCondition::PartlyCloudy:
      return isDay ? &wx_partly_day : &wx_partly_night;
    case WeatherCondition::Cloudy: return &wx_cloudy;
    case WeatherCondition::Fog:    return &wx_fog;
    case WeatherCondition::Rain:   return &wx_rain;
    case WeatherCondition::Snow:   return &wx_snow;
    case WeatherCondition::Storm:  return &wx_storm;
  }
  return &wx_cloudy;
}

// Compared at the resolution the screen draws -- a tenth of a degree. Comparing
// the raw floats would repaint on noise a reader cannot see.
int tenths(float celsius) { return (int)(celsius * 10.0f); }

bool sameAsShown(const WeatherSnapshot &weather) {
  if (!everShown) return false;
  if (weather.trusted != shown.trusted) return false;
  if (!weather.trusted) return true;  // one Unknown looks like any other
  return tenths(weather.temperatureC) == tenths(shown.temperatureC) &&
         tenths(weather.apparentC) == tenths(shown.apparentC) &&
         weather.code == shown.code && weather.isDay == shown.isDay &&
         weather.humidityPct == shown.humidityPct;
}

}  // namespace

void buildWeatherScreen(lv_obj_t *parent) {
  makeTitle(parent, "Weather");

  card = makeCard(parent, PAD, CARD_Y, display::WIDTH - 2 * PAD, CARD_H);

  glyph = lv_image_create(card);
  lv_image_set_src(glyph, &wx_cloudy);
  lv_obj_set_pos(glyph, GLYPH_X, (CARD_H - GLYPH) / 2 - 8);

  reading = makeLabel(card, &font_inter_36, theme::TEXT);
  lv_label_set_text(reading, format::UNKNOWN);
  lv_obj_set_pos(reading, TEXT_X, 14);

  condition = makeLabel(card, &font_inter_17, theme::MUTED);
  lv_label_set_text(condition, format::UNKNOWN);
  lv_obj_set_pos(condition, TEXT_X, 62);

  feels = makeLabel(card, &font_inter_14, theme::MUTED);
  lv_label_set_text(feels, "");
  lv_obj_set_pos(feels, TEXT_X, 86);

  footnote = makeLabel(parent, &font_inter_14, theme::MUTED);
  lv_label_set_text(footnote, "");
  lv_obj_set_pos(footnote, PAD + 10, CARD_Y + CARD_H + 10);

  lv_obj_update_layout(parent);
  warnIfCollapsed("weather card", card);
}

void updateWeatherScreen(const AppModel &model) {
  const WeatherSnapshot &weather = model.weather;
  const FeedStatus &status = model.status(Feed::Weather);

  // While the Feed is broken the footnote tracks it rather than the reading,
  // so it has to be rewritten even though the reading has not moved: "4m ago"
  // becomes "9m ago" while the temperature stays absent.
  if (!weather.trusted) {
    char note[56];
    format::feedNote(status, millis(), note, sizeof(note));
    lv_label_set_text(footnote, note);
    lv_obj_set_style_text_color(footnote, theme::colour(theme::AMBER), LV_PART_MAIN);
  }

  if (sameAsShown(weather)) return;
  shown = weather;
  everShown = true;

  char text[32];
  format::temperature(weather.temperatureC, weather.trusted, text, sizeof(text));
  lv_label_set_text(reading, text);

  if (!weather.trusted) {
    // No glyph rather than a stale one: a sun left over from an hour ago is a
    // claim about the sky, and the screen has nothing to base it on.
    lv_obj_add_flag(glyph, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(condition, format::UNKNOWN);
    lv_label_set_text(feels, "");
    return;
  }

  lv_obj_remove_flag(glyph, LV_OBJ_FLAG_HIDDEN);
  lv_image_set_src(glyph, glyphFor(weather.condition(), weather.isDay));
  lv_label_set_text(condition, describe(weather.condition()));

  char span[16];
  format::temperature(weather.apparentC, true, span, sizeof(span));
  snprintf(text, sizeof(text), "Feels like %s", span);
  lv_label_set_text(feels, text);

  lv_obj_set_style_text_color(footnote, theme::colour(theme::MUTED), LV_PART_MAIN);
  if (weather.humidityPct >= 0) {
    snprintf(text, sizeof(text), "Humidity %d%%", (int)weather.humidityPct);
    lv_label_set_text(footnote, text);
  } else {
    lv_label_set_text(footnote, "");
  }
}

}  // namespace ui
