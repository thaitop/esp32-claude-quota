#include "screen_weekly.h"

#include <Arduino.h>

#include "../display.h"
#include "fonts/ui_fonts.h"
#include "format.h"
#include "theme.h"
#include "widgets.h"

namespace ui {
namespace {

// The plot gets everything between the title and the footer. Measured against
// the faces rather than divided out of the page: Inter 27 sets a 33px line and
// Inter 12 an 15px one, and the chart takes what is left.
constexpr int16_t PAD = 12;
constexpr int16_t CHART_X = PAD;
constexpr int16_t CHART_Y = 44;
constexpr int16_t CHART_W = display::WIDTH - 2 * PAD;
constexpr int16_t CHART_H = 112;
constexpr int16_t FOOT_Y = CHART_Y + CHART_H + 10;

// A single Sample is a dot, not a trend. Below this the screen says so rather
// than drawing a nearly empty plot that looks like a broken chart.
constexpr uint8_t MIN_PLOTTABLE = 2;

lv_obj_t *chart = nullptr;
lv_chart_series_t *series = nullptr;
lv_obj_t *current = nullptr;
lv_obj_t *footer = nullptr;
lv_obj_t *empty = nullptr;

// Last rendered state. The main loop calls update() every second for the
// countdown on the other screen, and repainting a 296px chart at 1Hz over SPI
// is visible as a stutter even when nothing changed.
uint8_t shownCount = 0xFF;
uint32_t shownStamp = 0;
int8_t shownCurrent = -2;

void buildChart(lv_obj_t *parent) {
  chart = lv_chart_create(parent);
  lv_obj_set_size(chart, CHART_W, CHART_H);
  lv_obj_set_pos(chart, CHART_X, CHART_Y);
  lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
  lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
  lv_chart_set_div_line_count(chart, 5, 0);

  lv_obj_set_style_bg_color(chart, theme::colour(theme::CARD), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(chart, 10, LV_PART_MAIN);
  lv_obj_set_style_border_color(chart, theme::colour(theme::CARD_EDGE), LV_PART_MAIN);
  lv_obj_set_style_border_width(chart, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(chart, 8, LV_PART_MAIN);

  // The parts are not the obvious way round, and getting them backwards fails
  // silently in both directions at once: MAIN's line styles are the division
  // grid, ITEMS is the series line itself, and INDICATOR is the point markers.
  // Styling the series through ITEMS and the grid through MAIN -- rather than
  // the reverse -- is the difference between a readable curve over a faint
  // grid and an invisible curve under a heavy one.
  lv_obj_set_style_line_color(chart, theme::colour(theme::TRACK), LV_PART_MAIN);
  lv_obj_set_style_line_width(chart, 1, LV_PART_MAIN);
  lv_obj_set_style_line_width(chart, 3, LV_PART_ITEMS);

  // No markers on the points. At 56 of them across 296 pixels the dots merge
  // into a thick band and the line stops being readable as a line.
  lv_obj_set_style_width(chart, 0, LV_PART_INDICATOR);
  lv_obj_set_style_height(chart, 0, LV_PART_INDICATOR);

  series = lv_chart_add_series(chart, theme::colour(theme::GREEN),
                               LV_CHART_AXIS_PRIMARY_Y);
}

}  // namespace

void buildWeeklyScreen(lv_obj_t *parent) {
  makeTitle(parent, "Weekly");

  current = makeLabel(parent, &font_inter_27, theme::TEXT);
  lv_label_set_text(current, format::UNKNOWN);
  lv_obj_align(current, LV_ALIGN_TOP_RIGHT, -PAD, 1);

  buildChart(parent);

  footer = makeLabel(parent, &font_inter_12, theme::MUTED);
  lv_label_set_text(footer, "");
  lv_obj_set_pos(footer, PAD, FOOT_Y);

  // Sits on top of the chart rather than beside it: the two states are
  // alternatives, and a plot with a caption apologising for it underneath
  // reads as a plot that failed rather than as a week that has not happened.
  empty = makeLabel(parent, &font_inter_15, theme::MUTED);
  lv_label_set_text(empty, "Not enough history yet");
  lv_obj_align(empty, LV_ALIGN_TOP_MID, 0, CHART_Y + CHART_H / 2 - 9);

  lv_obj_update_layout(parent);
  warnIfCollapsed("weekly chart", chart);
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
  }

  if (history.count == shownCount && stamp == shownStamp) return;
  shownCount = history.count;
  shownStamp = stamp;

  const bool plottable = history.trusted && history.count >= MIN_PLOTTABLE;
  if (plottable) {
    lv_obj_remove_flag(chart, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(empty, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(chart, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(empty, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(empty, history.trusted ? "Not enough history yet"
                                             : "No history from the bridge");
    lv_label_set_text(footer, "");
    return;
  }

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

  char text[48];
  snprintf(text, sizeof(text), "Last 7 days  ·  %u samples, 3h apart",
           (unsigned)history.count);
  lv_label_set_text(footer, text);
}

}  // namespace ui
