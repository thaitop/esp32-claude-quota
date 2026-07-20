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

// The screen is three bands under the title, the same three the Weekly screen
// uses: a hero card that pairs the figure with its picture, a row of three stat
// tiles, and one line of provenance.
//
// The first pass was a single 118px card with the glyph on the left and three
// stacked lines beside it, and then eighty pixels of empty page below. That
// spent the screen's whole surface on one number and left the humidity as a
// bare sentence floating on the background, which is the one thing no other
// screen does. Same bands, same pitch, same tile strip -- the set reads as one
// product, and "Feels like" and "Humidity" become figures the eye can compare
// rather than prose it has to read.
constexpr int16_t PAD = 8;
constexpr int16_t HERO_Y = 36;
constexpr int16_t HERO_W = display::WIDTH - 2 * PAD;
constexpr int16_t HERO_H = 90;
constexpr int16_t HERO_PAD = 10;

// The reading leads and the glyph answers it. Reversed from the first pass,
// which put the artwork first and pushed the temperature into the right half
// where a 36px figure has the least room; the number is what the screen is for,
// so it gets the left margin every other screen's figure starts from.
constexpr int16_t TEMP_Y = 12;   // Inter 36's line box carries its leading above
                                 // the digits, so the ink lands level with the
                                 // 10px side padding
constexpr int16_t COND_Y = 58;   // 58..79, nine clear of the card's bottom edge

constexpr int16_t GLYPH = 48;
constexpr int16_t GLYPH_X = HERO_W - HERO_PAD - GLYPH;
constexpr int16_t GLYPH_Y = (HERO_H - GLYPH) / 2;

// Same geometry as the Weekly screen's strip, to the pixel. Two rows of tiles
// that differ by a few pixels between screens read as two grids.
constexpr int16_t TILE_Y = 130;
constexpr int16_t TILE_H = 42;
constexpr int16_t TILE_GAP = 8;
constexpr int16_t TILE_W = (HERO_W - 2 * TILE_GAP) / 3;
constexpr int16_t FOOT_Y = 176;

struct Tile {
  lv_obj_t *value = nullptr;
};

lv_obj_t *hero = nullptr;
lv_obj_t *glyph = nullptr;
lv_obj_t *reading = nullptr;
lv_obj_t *condition = nullptr;
lv_obj_t *footnote = nullptr;
Tile tiles[3];  // feels like, humidity, daylight

// What was last drawn. Kept as the snapshot itself rather than as a hash of
// it: the first pass folded the fields into one int with hand-picked strides,
// and a 45 degree apparent temperature overflowed its stride into the actual
// temperature's, so two different readings compared equal and the screen kept
// showing the older one. A field-by-field comparison cannot alias.
WeatherSnapshot shown;
bool everShown = false;

// The provenance line moves on its own clock -- "4m ago" becomes "9m ago" while
// the temperature has not changed -- so it is gated on its own text rather than
// on the snapshot. Setting an identical string still marks the label dirty, and
// a repaint every second is visible as a stutter on a panel this size.
char shownFootnote[64] = {0};

void setFootnote(const char *text, uint32_t colour) {
  if (strncmp(text, shownFootnote, sizeof(shownFootnote)) == 0) return;
  snprintf(shownFootnote, sizeof(shownFootnote), "%s", text);
  lv_label_set_text(footnote, shownFootnote);
  lv_obj_set_style_text_color(footnote, theme::colour(colour), LV_PART_MAIN);
}

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

void setTile(Tile &tile, const char *text, uint32_t colour) {
  lv_label_set_text(tile.value, text);
  lv_obj_set_style_text_color(tile.value, theme::colour(colour), LV_PART_MAIN);
  // Re-centred on every write: "100%" is wider than "--", and a label aligned
  // once around the Unknown marker sits off-centre for every real figure after
  // it -- the same mistake the Crypto screen's caption made beside its pill.
  lv_obj_align(tile.value, LV_ALIGN_TOP_MID, 0, 18);
}

void buildTile(Tile &tile, lv_obj_t *parent, int index, const char *caption) {
  lv_obj_t *card =
      makeCard(parent, PAD + index * (TILE_W + TILE_GAP), TILE_Y, TILE_W, TILE_H);

  // Caption above figure, both centred -- the Weekly strip's arrangement, for
  // the same reason: at 42px tall there is no room to lead with the figure, and
  // three left-aligned pairs read as ragged fragments rather than as one strip.
  lv_obj_t *label = makeLabel(card, &font_inter_12, theme::MUTED);
  lv_label_set_text(label, caption);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 3);

  tile.value = makeLabel(card, &font_inter_17, theme::TEXT);
  setTile(tile, format::UNKNOWN, theme::MUTED);
}

void clearTiles() {
  for (Tile &tile : tiles) setTile(tile, format::UNKNOWN, theme::MUTED);
}

}  // namespace

void buildWeatherScreen(lv_obj_t *parent) {
  makeTitle(parent, "Weather", &icon_weather_hdr);

  hero = makeCard(parent, PAD, HERO_Y, HERO_W, HERO_H);

  reading = makeLabel(hero, &font_inter_36_bold, theme::TEXT);
  lv_label_set_text(reading, format::UNKNOWN);
  lv_obj_set_pos(reading, HERO_PAD, TEMP_Y);

  condition = makeLabel(hero, &font_inter_17, theme::MUTED);
  lv_label_set_text(condition, format::UNKNOWN);
  lv_obj_set_pos(condition, HERO_PAD, COND_Y);

  glyph = lv_image_create(hero);
  lv_image_set_src(glyph, &wx_cloudy);
  lv_obj_set_pos(glyph, GLYPH_X, GLYPH_Y);

  buildTile(tiles[0], parent, 0, "Feels like");
  buildTile(tiles[1], parent, 1, "Humidity");
  buildTile(tiles[2], parent, 2, "Daylight");

  footnote = makeLabel(parent, &font_inter_12, theme::MUTED);
  lv_label_set_text(footnote, "");
  lv_obj_set_pos(footnote, PAD + 4, FOOT_Y);

  lv_obj_update_layout(parent);
  warnIfCollapsed("weather hero", hero);
}

void updateWeatherScreen(const AppModel &model) {
  const WeatherSnapshot &weather = model.weather;
  const FeedStatus &status = model.status(Feed::Weather);

  // The provenance line is rewritten whether or not the reading moved: while
  // the Feed is broken it tracks the outage rather than the temperature, and
  // while it is healthy it ages. setFootnote() does the comparing.
  //
  // ASCII only, separator included. The generated faces carry 0x20..0x7E plus
  // the degree sign and nothing else -- see tools/mkfont_lvgl.py -- so a middle
  // dot would render as the missing-glyph box on the panel while looking
  // perfectly correct here.
  char note[64];
  if (weather.trusted) {
    char age[24];
    format::since(status.lastSuccessMs, millis(), age, sizeof(age));
    snprintf(note, sizeof(note), "Open-Meteo - updated %s", age);
    setFootnote(note, theme::MUTED);
  } else {
    format::feedNote(status, millis(), note, sizeof(note));
    setFootnote(note, theme::AMBER);
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
    clearTiles();
    return;
  }

  lv_obj_remove_flag(glyph, LV_OBJ_FLAG_HIDDEN);
  lv_image_set_src(glyph, glyphFor(weather.condition(), weather.isDay));
  lv_label_set_text(condition, describe(weather.condition()));

  format::temperature(weather.apparentC, true, text, sizeof(text));
  setTile(tiles[0], text, theme::TEXT);

  if (weather.humidityPct >= 0) {
    snprintf(text, sizeof(text), "%d%%", (int)weather.humidityPct);
    setTile(tiles[1], text, theme::TEXT);
  } else {
    setTile(tiles[1], format::UNKNOWN, theme::MUTED);
  }

  // The one tile whose figure is a state rather than a number, so it carries
  // the state in colour as well as in the word -- the amber the rest of the
  // product already uses for warm, and the session lavender for the other half
  // of the day.
  setTile(tiles[2], weather.isDay ? "Day" : "Night",
          weather.isDay ? theme::AMBER : theme::PILL_SESSION_TX);
}

}  // namespace ui
