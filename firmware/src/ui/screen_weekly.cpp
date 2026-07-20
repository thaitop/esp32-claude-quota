#include "screen_weekly.h"

#include <Arduino.h>

#include "../display.h"
#include "fonts/ui_fonts.h"
#include "format.h"
#include "theme.h"
#include "ui_icons.h"
#include "widgets.h"

namespace ui {
namespace {

// The screen is three bands under the title: a hero card that pairs the figure
// with the curve, a row of three stat tiles, and one line of provenance.
//
// The plot used to be the whole page -- a bare chart in a box with a caption,
// which showed the shape of the week and none of the numbers in it. Reading a
// peak off a 70px-tall curve is guesswork; the tiles state the three figures
// the eye was trying to measure, and the curve goes back to doing the one job
// it is good at.
constexpr int16_t PAD = 8;
constexpr int16_t HERO_Y = 36;
constexpr int16_t HERO_W = display::WIDTH - 2 * PAD;
constexpr int16_t HERO_H = 90;

// The figure's column. "100%" at Inter 30 Bold measures 74px, so 86 of column
// clears it with room to breathe and leaves the curve the remaining two
// thirds -- the split the design uses, at this width.
constexpr int16_t HERO_PAD = 10;
constexpr int16_t PCT_Y = 8;
constexpr int16_t PILL_Y = 52;
constexpr int16_t PILL_H = 22;
constexpr int16_t PILL_PAD = 12;

constexpr int16_t CHART_X = 96;
constexpr int16_t CHART_Y = 10;
constexpr int16_t CHART_W = HERO_W - CHART_X - HERO_PAD;
constexpr int16_t CHART_H = 70;
// The plot's own inset, kept as a constant because the peak marker is placed by
// hand and has to land on the same grid LVGL draws the series onto.
constexpr int16_t CHART_PAD = 4;
constexpr int16_t DOT = 7;

constexpr int16_t TILE_Y = 130;
constexpr int16_t TILE_H = 42;
constexpr int16_t TILE_GAP = 8;
constexpr int16_t TILE_W = (HERO_W - 2 * TILE_GAP) / 3;
constexpr int16_t FOOT_Y = 176;

// A single Sample is a dot, not a trend. Below this the screen says so rather
// than drawing a nearly empty plot that looks like a broken chart.
constexpr uint8_t MIN_PLOTTABLE = 2;

struct Tile {
  lv_obj_t *value = nullptr;
};

lv_obj_t *hero = nullptr;
lv_obj_t *chart = nullptr;
lv_chart_series_t *series = nullptr;
lv_obj_t *peakDot = nullptr;
lv_obj_t *current = nullptr;
lv_obj_t *pill = nullptr;
lv_obj_t *pillLabel = nullptr;
lv_obj_t *footer = nullptr;
lv_obj_t *empty = nullptr;
Tile tiles[3];  // average, peak, lowest

// Last rendered state. The main loop calls update() every second for the
// countdown on the other screen, and repainting a 198px chart at 1Hz over SPI
// is visible as a stutter even when nothing changed.
uint8_t shownCount = 0xFF;
uint32_t shownStamp = 0;
int8_t shownCurrent = -2;

// The three figures the tiles state, over the Samples the bridge vouched for.
// Untrusted readings are skipped rather than counted as zero: a dropped poll is
// not a quiet week, and averaging it in as one is the same mistake ADR-0001
// forbids at the source.
struct Stats {
  bool any = false;
  int16_t average = 0;
  int8_t peak = 0;
  int8_t lowest = 0;
  uint8_t peakIndex = 0;
};

Stats summarise(const HistorySnapshot &history) {
  Stats stats;
  int32_t sum = 0;
  uint8_t counted = 0;

  for (uint8_t i = 0; i < history.count; i++) {
    const int8_t value = history.samples[i].weeklyUtilization;
    if (value < 0) continue;

    if (!stats.any) {
      stats.any = true;
      stats.peak = stats.lowest = value;
      stats.peakIndex = i;
    }
    if (value > stats.peak) {
      stats.peak = value;
      stats.peakIndex = i;
    }
    if (value < stats.lowest) stats.lowest = value;
    sum += value;
    counted++;
  }

  if (counted > 0) stats.average = (int16_t)((sum + counted / 2) / counted);
  return stats;
}

void buildChart(lv_obj_t *parent) {
  chart = lv_chart_create(parent);
  lv_obj_set_size(chart, CHART_W, CHART_H);
  lv_obj_set_pos(chart, CHART_X, CHART_Y);
  lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
  lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
  // Three lines, not five: at 70px tall a five-way split puts the grid 17px
  // apart and the curve spends most of its length inside a hatch pattern.
  lv_chart_set_div_line_count(chart, 3, 0);

  // Transparent now that the card behind it carries the surface. A chart with
  // its own fill and border inside a card reads as a box in a box.
  lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(chart, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(chart, CHART_PAD, LV_PART_MAIN);

  // The parts are not the obvious way round, and getting them backwards fails
  // silently in both directions at once: MAIN's line styles are the division
  // grid, ITEMS is the series line itself, and INDICATOR is the point markers.
  // Styling the series through ITEMS and the grid through MAIN -- rather than
  // the reverse -- is the difference between a readable curve over a faint
  // grid and an invisible curve under a heavy one.
  lv_obj_set_style_line_color(chart, theme::colour(theme::TRACK), LV_PART_MAIN);
  lv_obj_set_style_line_width(chart, 1, LV_PART_MAIN);
  // Dashed, so the grid reads as a scale behind the curve rather than as three
  // more lines competing with it.
  lv_obj_set_style_line_dash_width(chart, 3, LV_PART_MAIN);
  lv_obj_set_style_line_dash_gap(chart, 4, LV_PART_MAIN);
  lv_obj_set_style_line_width(chart, 3, LV_PART_ITEMS);
  lv_obj_set_style_line_rounded(chart, true, LV_PART_ITEMS);

  // No markers on the points. At 56 of them across 198 pixels the dots merge
  // into a thick band and the line stops being readable as a line. The one dot
  // that earns its place is the peak, drawn separately below.
  lv_obj_set_style_width(chart, 0, LV_PART_INDICATOR);
  lv_obj_set_style_height(chart, 0, LV_PART_INDICATOR);

  series = lv_chart_add_series(chart, theme::colour(theme::GREEN),
                               LV_CHART_AXIS_PRIMARY_Y);

  peakDot = makePanel(parent, DOT, DOT, LV_RADIUS_CIRCLE, theme::TEXT);
  lv_obj_add_flag(peakDot, LV_OBJ_FLAG_HIDDEN);
}

// Puts the peak marker on the same point LVGL drew. The chart insets its plot
// area by pad_all and spreads count points across what is left, first point on
// the left edge and last on the right, so both axes are a straight remap of
// that span -- which is why CHART_PAD is a constant rather than a literal.
void placePeak(uint8_t index, uint8_t count, int8_t value) {
  if (count < MIN_PLOTTABLE) {
    lv_obj_add_flag(peakDot, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  const int32_t innerW = CHART_W - 2 * CHART_PAD;
  const int32_t innerH = CHART_H - 2 * CHART_PAD;
  const int32_t x = CHART_X + CHART_PAD + (int32_t)index * (innerW - 1) / (count - 1);
  const int32_t y = CHART_Y + CHART_PAD + (100 - value) * (innerH - 1) / 100;

  lv_obj_set_pos(peakDot, (int16_t)(x - DOT / 2), (int16_t)(y - DOT / 2));
  lv_obj_remove_flag(peakDot, LV_OBJ_FLAG_HIDDEN);
}

void buildTile(Tile &tile, lv_obj_t *parent, int index, const char *caption) {
  lv_obj_t *card =
      makeCard(parent, PAD + index * (TILE_W + TILE_GAP), TILE_Y, TILE_W, TILE_H);

  // Caption above figure, both centred. The Claude screen's cards lead with the
  // figure because they have room for it; at 42px tall these do not, and a
  // left-aligned label with the number beside it would leave the row reading as
  // three ragged fragments rather than one strip.
  lv_obj_t *label = makeLabel(card, &font_inter_12, theme::MUTED);
  lv_label_set_text(label, caption);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 3);

  tile.value = makeLabel(card, &font_inter_17, theme::TEXT);
  lv_label_set_text(tile.value, format::UNKNOWN);
  lv_obj_align(tile.value, LV_ALIGN_TOP_MID, 0, 18);
}

void setTile(Tile &tile, bool known, int16_t value, uint32_t colour) {
  char text[8];
  if (known) {
    snprintf(text, sizeof(text), "%d%%", (int)value);
  } else {
    snprintf(text, sizeof(text), "%s", format::UNKNOWN);
  }
  lv_label_set_text(tile.value, text);
  lv_obj_set_style_text_color(tile.value, theme::colour(known ? colour : theme::MUTED),
                              LV_PART_MAIN);
  lv_obj_align(tile.value, LV_ALIGN_TOP_MID, 0, 18);
}

}  // namespace

void buildWeeklyScreen(lv_obj_t *parent) {
  makeTitle(parent, "Weekly", &icon_weekly_hdr);

  hero = makeCard(parent, PAD, HERO_Y, HERO_W, HERO_H);

  current = makeLabel(hero, &font_inter_30_bold, theme::TEXT);
  lv_label_set_text(current, format::UNKNOWN);
  lv_obj_set_pos(current, HERO_PAD, PCT_Y);

  // Sized from the measured label rather than with LV_SIZE_CONTENT, for the
  // reason the Claude screen's pills document: content sizing resolves to zero
  // width here and the pill clips its own text without reporting anything.
  pill = makePanel(hero, 10, PILL_H, PILL_H / 2, theme::PILL_WEEKLY_BG);
  pillLabel = makeLabel(pill, &font_inter_15, theme::PILL_WEEKLY_TX);
  lv_label_set_text(pillLabel, "Weekly");
  lv_obj_update_layout(pillLabel);
  lv_obj_set_width(pill, lv_obj_get_width(pillLabel) + 2 * PILL_PAD);
  lv_obj_set_pos(pill, HERO_PAD, PILL_Y);
  lv_obj_center(pillLabel);

  buildChart(hero);

  buildTile(tiles[0], parent, 0, "Average");
  buildTile(tiles[1], parent, 1, "Peak");
  buildTile(tiles[2], parent, 2, "Lowest");

  footer = makeLabel(parent, &font_inter_12, theme::MUTED);
  lv_label_set_text(footer, "");
  lv_obj_set_pos(footer, PAD + 4, FOOT_Y);

  // Sits on top of the chart rather than beside it: the two states are
  // alternatives, and a plot with a caption apologising for it underneath
  // reads as a plot that failed rather than as a week that has not happened.
  empty = makeLabel(hero, &font_inter_14, theme::MUTED);
  lv_label_set_text(empty, "Not enough history yet");
  lv_obj_align(empty, LV_ALIGN_TOP_LEFT, CHART_X, CHART_Y + CHART_H / 2 - 9);

  lv_obj_update_layout(parent);
  warnIfCollapsed("weekly chart", chart);
  warnIfCollapsed("weekly pill", pill);
  warnIfOverflowing("weekly", parent);
}

void updateWeeklyScreen(const AppModel &model) {
  const HistorySnapshot &history = model.history;
  const int8_t weekly =
      model.quota.weekly.trusted ? model.quota.weekly.utilization : -1;

  // The newest Sample's timestamp stands in for "has the series changed".
  // Comparing 56 values every second to save a redraw that happens twice an
  // hour is the wrong trade.
  const uint32_t stamp = history.count > 0 ? history.samples[history.count - 1].recordedAt : 0;

  if (weekly != shownCurrent) {
    shownCurrent = weekly;

    char text[8];
    format::utilization(model.quota.weekly, text, sizeof(text));
    lv_label_set_text(current, text);

    uint32_t from, to;
    theme::ramp(weekly, from, to);
    lv_obj_set_style_text_color(current, theme::colour(from), LV_PART_MAIN);
    lv_chart_set_series_color(chart, series, theme::colour(from));
    lv_obj_set_style_bg_color(peakDot, theme::colour(from), LV_PART_MAIN);
  }

  if (history.count == shownCount && stamp == shownStamp) return;
  shownCount = history.count;
  shownStamp = stamp;

  const Stats stats = summarise(history);
  const bool plottable = history.trusted && history.count >= MIN_PLOTTABLE && stats.any;

  if (!plottable) {
    lv_obj_add_flag(chart, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(peakDot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(empty, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(empty, history.trusted ? "Not enough history yet"
                                             : "No history from the bridge");
    for (Tile &tile : tiles) setTile(tile, false, 0, theme::MUTED);
    lv_label_set_text(footer, "");
    return;
  }

  lv_obj_remove_flag(chart, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(empty, LV_OBJ_FLAG_HIDDEN);

  lv_chart_set_point_count(chart, history.count);
  for (uint8_t i = 0; i < history.count; i++) {
    const int8_t value = history.samples[i].weeklyUtilization;
    // A Sample the bridge could not vouch for leaves a gap rather than a zero.
    // LVGL draws straight through the gap, so a dropped poll costs a slightly
    // straighter curve and never a cliff down to the axis.
    lv_chart_set_value_by_id(chart, series, i,
                             value < 0 ? LV_CHART_POINT_NONE : value);
  }
  lv_chart_refresh(chart);
  placePeak(stats.peakIndex, history.count, stats.peak);

  // The peak is the figure that decides whether the week was close to the
  // ceiling, so it wears the Ramp colour for its own value rather than for the
  // current reading. The low is always the comfortable end, hence green.
  uint32_t peakFrom, peakTo;
  theme::ramp((int8_t)stats.peak, peakFrom, peakTo);
  setTile(tiles[0], true, stats.average, theme::TEXT);
  setTile(tiles[1], true, stats.peak, peakFrom);
  setTile(tiles[2], true, stats.lowest, theme::GREEN);

  // ASCII only. The separator here used to be a middle dot, and the generated
  // faces carry 0x20..0x7E plus the degree sign and nothing else -- see
  // tools/mkfont_lvgl.py -- so U+00B7 rendered as the missing-glyph box on the
  // panel while looking perfectly correct in the source.
  char text[48];
  snprintf(text, sizeof(text), "Last 7 days - %u samples, 3h apart",
           (unsigned)history.count);
  lv_label_set_text(footer, text);
}

}  // namespace ui
