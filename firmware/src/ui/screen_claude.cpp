#include "screen_claude.h"

#include <Arduino.h>

#include "../display.h"
#include "fonts/ui_fonts.h"
#include "format.h"
#include "theme.h"
#include "ui_icons.h"
#include "widgets.h"

namespace ui {
namespace {

// 240px of height has to hold a header, two cards and the navbar. Every number
// here was chosen against the real font metrics rather than guessed, which is
// why the countdown clears the progress bar by exactly two pixels.
constexpr int16_t HEADER_H = 34;
constexpr int16_t CARD_X = 8;
constexpr int16_t CARD_W = display::WIDTH - 2 * CARD_X;
constexpr int16_t CARD_H = 76;
// The slimmer 36px navbar (28px tiles) freed height at the bottom; the second
// card drops to sit just clear of it, opening the gap between the two cards
// rather than growing either card and unbalancing its interior.
constexpr int16_t CARD_Y[2] = {36, 122};

// The vertical stack inside a 76px card, with the real font metrics rather
// than round numbers: Inter 30 Bold sets a 37px line on a 7px descent, Inter 12
// a 15px one. The first pass put the countdown at 58 and it ran two pixels past
// the card's bottom edge, straight through the border.
//
// The gaps are equal on all four sides now, which they were not: 14px of side
// padding against a 4px bottom gap read as a card that had been pushed up. Ten
// at the sides, nine above the digits and nine below the countdown, and the
// figure sits in the middle of its own box.
constexpr int16_t PAD = 10;
constexpr int16_t PCT_Y = 1;    // Inter 30 Bold's line box carries 8px of
                                // leading above the digits, so the ink starts
                                // at 9 -- level with the side padding
constexpr int16_t PILL_Y = 9;   // top of the digits, so pill and figure share
                                // a cap line
constexpr int16_t PILL_H = 22;
constexpr int16_t PILL_PAD = 13;
constexpr int16_t BAR_Y = 40;   // 40..48, nine clear of the baseline at 31
constexpr int16_t BAR_H = 8;
constexpr int16_t FOOT_Y = 52;  // 52..67 at Inter 12, four clear of the bar and
                                // nine off the card's bottom edge
constexpr int16_t CLOCK = 14;

// The pace marker: a thin vertical tick riding on the bar at the fraction of
// the window that has elapsed. Two pixels wide so it reads as a line rather
// than a block; two taller than the bar on each side so it is legible against a
// full indicator. A solid rect, deliberately not a gradient bar -- it costs no
// LVGL layer buffer, unlike the indicator it sits on (ADR-0003).
constexpr int16_t MARK_W = 2;
constexpr int16_t MARK_H = BAR_H + 4;
constexpr int16_t MARK_Y = BAR_Y - 2;

struct CardWidgets {
  lv_obj_t *root = nullptr;
  lv_obj_t *utilization = nullptr;
  lv_obj_t *pill = nullptr;
  lv_obj_t *pillLabel = nullptr;
  lv_obj_t *bar = nullptr;
  lv_obj_t *marker = nullptr;
  lv_obj_t *clock = nullptr;
  lv_obj_t *resets = nullptr;

  // Last rendered values, so a per-second countdown does not rewrite the
  // percentage and the bar forty times a minute.
  int8_t shownUtilization = -2;
  int32_t shownSeconds = -2;
};

CardWidgets cards[2];
lv_obj_t *statusDot = nullptr;
lv_obj_t *wifiGlyph = nullptr;
lv_obj_t *headerClock = nullptr;
int shownStatus = -1;
int shownBars = -1;
// Neither a real time nor the empty string an unsynced clock renders as, so
// the first update always draws.
char shownClock[6] = "-";

void buildHeader(lv_obj_t *parent) {
  lv_obj_t *badge = lv_image_create(parent);
  lv_image_set_src(badge, &icon_usage_hdr);
  lv_obj_set_pos(badge, 10, 5);

  // Smaller than the design's 27px and bold instead. At 27 Regular the word
  // outweighed the figures it introduces; 22 Bold reads as a label rather than
  // as the loudest thing on the screen. Sat at y=4 so its 26px line box centres
  // on the badge beside it.
  lv_obj_t *title = makeLabel(parent, &font_inter_22_bold, theme::TEXT);
  lv_label_set_text(title, "Usage");
  lv_obj_set_pos(title, 46, 4);

  // The wall clock, right-aligned rather than positioned from the left: "09:05"
  // and "23:59" are not the same width in a proportional face, and a
  // left-anchored label would walk the gap to the wifi glyph about as the hour
  // changed. Its right edge lands at 262, eight clear of the glyph at 270.
  // Muted, because the header's job is the title and the state -- the time is
  // there to be glanced at, not read.
  // y=8 centres its 19px line box on the glyph's midline at 17.
  headerClock = makeLabel(parent, &font_inter_15, theme::MUTED);
  lv_label_set_text(headerClock, "");
  lv_obj_align(headerClock, LV_ALIGN_TOP_RIGHT, -58, 8);

  wifiGlyph = lv_image_create(parent);
  lv_image_set_src(wifiGlyph, &glyph_wifi);
  lv_obj_set_pos(wifiGlyph, 270, 9);
  lv_obj_set_style_image_recolor_opa(wifiGlyph, LV_OPA_COVER, LV_PART_MAIN);

  // No status word. The dot already carries the state in colour and the glyph
  // carries the radio, so spelling "Online" out beside them said the same thing
  // a third time and cost 70px of the header to do it.
  statusDot = makePanel(parent, 8, 8, LV_RADIUS_CIRCLE, theme::GREEN);
  lv_obj_set_pos(statusDot, 302, 13);
}

void buildCard(CardWidgets &card, lv_obj_t *parent, int index, const char *badge,
               uint32_t pillBg, uint32_t pillTx) {
  card.root = makePanel(parent, CARD_W, CARD_H, 12, theme::CARD);
  lv_obj_set_pos(card.root, CARD_X, CARD_Y[index]);
  lv_obj_add_style(card.root, theme::borderStyle(theme::CARD_EDGE), LV_PART_MAIN);
  lv_obj_set_style_border_width(card.root, 1, LV_PART_MAIN);
  lv_obj_set_style_border_opa(card.root, LV_OPA_COVER, LV_PART_MAIN);

  card.utilization = makeLabel(card.root, &font_inter_30_bold, theme::TEXT);
  lv_obj_set_pos(card.utilization, PAD, PCT_Y);

  // The design's chevron is gone: nothing here opens a detail view, and an
  // affordance that does nothing is a promise the firmware does not keep.
  //
  // The pill sizes to its own text. A fixed width fits "Current" and then
  // leaves "Weekly" rattling around inside the same box.
  // Sized from the measured label rather than with LV_SIZE_CONTENT. Content
  // sizing resolved to zero width here whether the label was centred or laid
  // out with flex -- the pill rendered as an invisible 0x22 sliver and clipped
  // its own text, with nothing reported anywhere. Measuring is one extra
  // layout pass at build time and it cannot fail quietly.
  card.pill = makePanel(card.root, 10, PILL_H, PILL_H / 2, pillBg);
  card.pillLabel = makeLabel(card.pill, &font_inter_15, pillTx);
  lv_label_set_text(card.pillLabel, badge);
  lv_obj_update_layout(card.pillLabel);
  lv_obj_set_width(card.pill, lv_obj_get_width(card.pillLabel) + 2 * PILL_PAD);
  lv_obj_align(card.pill, LV_ALIGN_TOP_RIGHT, -PAD, PILL_Y);
  lv_obj_center(card.pillLabel);

  card.bar = lv_bar_create(card.root);
  lv_obj_remove_style_all(card.bar);
  lv_obj_set_size(card.bar, CARD_W - 2 * PAD, BAR_H);
  lv_obj_set_pos(card.bar, PAD, BAR_Y);
  lv_bar_set_range(card.bar, 0, 100);
  lv_obj_set_style_radius(card.bar, BAR_H / 2, LV_PART_MAIN);
  lv_obj_add_style(card.bar, theme::bgStyle(theme::TRACK), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card.bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(card.bar, BAR_H / 2, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(card.bar, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_dir(card.bar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);

  // Created after the bar so it draws over the indicator. Starts hidden: with no
  // trusted utilization there is nothing to pace against, so updateMarker keeps
  // it off until a reading arrives.
  card.marker = makePanel(card.root, MARK_W, MARK_H, 0, theme::TEXT);
  lv_obj_set_pos(card.marker, PAD, MARK_Y);
  lv_obj_add_flag(card.marker, LV_OBJ_FLAG_HIDDEN);

  card.resets = makeLabel(card.root, &font_inter_12, theme::MUTED);
  lv_obj_set_pos(card.resets, PAD + CLOCK + 6, FOOT_Y);

  card.clock = lv_image_create(card.root);
  lv_image_set_src(card.clock, &glyph_clock);
  lv_obj_set_style_image_recolor_opa(card.clock, LV_OPA_COVER, LV_PART_MAIN);
  // Centred against the label rather than sharing its y. A label's box starts
  // at the line's ascent, so the letters sit lower inside it than the top edge
  // suggests and a top-aligned glyph reads as floating. The extra pixel leans
  // it towards the x-height, where the eye puts the line's centre.
  lv_obj_align_to(card.clock, card.resets, LV_ALIGN_OUT_LEFT_MID, -6, 1);
}

// Place the pace marker at the elapsed fraction of the window, reddened when the
// fill (utilization) has run past it -- quota burning faster than the clock.
// Hidden whenever there is nothing trustworthy to pace against.
void updateMarker(CardWidgets &card, const QuotaWindow &window,
                  int32_t windowSeconds) {
  const bool known = window.trusted && window.utilization >= 0 &&
                     window.secondsToReset >= 0 && windowSeconds > 0;
  if (!known) {
    lv_obj_add_flag(card.marker, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_remove_flag(card.marker, LV_OBJ_FLAG_HIDDEN);

  int32_t elapsed = windowSeconds - window.secondsToReset;
  if (elapsed < 0) elapsed = 0;
  if (elapsed > windowSeconds) elapsed = windowSeconds;

  // 64-bit intermediates: weekly's 604800s times a ~300px travel overflows a
  // 32-bit multiply. Travel is the bar minus the marker's own width so the tick
  // stays fully inside the track at both ends.
  const int32_t travel = (CARD_W - 2 * PAD) - MARK_W;
  const int32_t x = PAD + (int32_t)((int64_t)elapsed * travel / windowSeconds);
  lv_obj_set_pos(card.marker, x, MARK_Y);

  const int32_t elapsedPct = (int32_t)((int64_t)elapsed * 100 / windowSeconds);
  const uint32_t tint = window.utilization > elapsedPct ? theme::RED : theme::TEXT;
  lv_obj_set_style_bg_color(card.marker, theme::colour(tint), LV_PART_MAIN);
}

void updateCard(CardWidgets &card, const QuotaWindow &window,
                int32_t windowSeconds) {
  const int8_t utilization = window.trusted ? window.utilization : -1;

  if (utilization != card.shownUtilization) {
    card.shownUtilization = utilization;

    char text[8];
    format::utilization(window, text, sizeof(text));
    lv_label_set_text(card.utilization, text);

    uint32_t from, to;
    theme::ramp(utilization, from, to);
    lv_obj_set_style_bg_color(card.bar, theme::colour(from), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(card.bar, theme::colour(to), LV_PART_INDICATOR);
    lv_obj_set_style_image_recolor(card.clock, theme::colour(from), LV_PART_MAIN);
    lv_bar_set_value(card.bar, utilization < 0 ? 0 : utilization, LV_ANIM_ON);

    // Utilization moving can flip the marker across the pace line even when the
    // second has not ticked, so recompute it here too.
    updateMarker(card, window, windowSeconds);
  }

  if (window.secondsToReset != card.shownSeconds) {
    card.shownSeconds = window.secondsToReset;
    char text[24];
    char span[16];
    format::duration(window.secondsToReset, span, sizeof(span));
    snprintf(text, sizeof(text), "Resets in %s", span);
    lv_label_set_text(card.resets, text);

    updateMarker(card, window, windowSeconds);
  }
}

void updateClock(const AppModel &model) {
  if (strcmp(model.wallClock, shownClock) == 0) return;
  strncpy(shownClock, model.wallClock, sizeof(shownClock) - 1);
  shownClock[sizeof(shownClock) - 1] = '\0';
  lv_label_set_text(headerClock, shownClock);
}

// The wifi glyph as signal bars: full/high/low by RSSI while associated, the
// crossed-out glyph when the radio is not on the network at all. RSSI is dBm,
// so less negative is stronger; the -60/-70 thresholds are the usual "strong /
// ok / weak" bands and the 10dBm gaps keep a hovering signal from flickering
// between glyphs every second.
int wifiBars(const AppModel &model) {
  if (!model.wifiAssociated) return 0;  // off
  if (model.rssi >= -60) return 3;      // full
  if (model.rssi >= -70) return 2;      // high
  return 1;                             // low
}

const lv_image_dsc_t *wifiGlyphFor(int bars) {
  switch (bars) {
  case 3:  return &glyph_wifi;
  case 2:  return &glyph_wifi_high;
  case 1:  return &glyph_wifi_low;
  default: return &glyph_wifi_off;
  }
}

void updateStatus(const AppModel &model) {
  const bool online = model.wifiAssociated && model.quota.trusted;
  const int status = (model.wifiAssociated ? 1 : 0) + (model.quota.trusted ? 2 : 0);
  const int bars = wifiBars(model);
  if (status == shownStatus && bars == shownBars) return;
  shownStatus = status;
  shownBars = bars;

  // Green associated and fed, amber associated but the bridge is quiet, red off
  // the network entirely.
  const uint32_t tint =
      online ? theme::GREEN : (model.wifiAssociated ? theme::AMBER : theme::RED);

  lv_obj_set_style_bg_color(statusDot, theme::colour(tint), LV_PART_MAIN);
  lv_image_set_src(wifiGlyph, wifiGlyphFor(bars));
  lv_obj_set_style_image_recolor(
      wifiGlyph, theme::colour(model.wifiAssociated ? theme::TEXT : theme::NAV_EDGE),
      LV_PART_MAIN);
}

}  // namespace

void buildClaudeScreen(lv_obj_t *parent) {
  buildHeader(parent);
  buildCard(cards[0], parent, 0, "Current", theme::PILL_SESSION_BG,
            theme::PILL_SESSION_TX);
  buildCard(cards[1], parent, 1, "Weekly", theme::PILL_WEEKLY_BG,
            theme::PILL_WEEKLY_TX);
  (void)HEADER_H;

  // A widget that resolves to no width is invisible and reports nothing --
  // which is exactly how the pills disappeared under LV_SIZE_CONTENT. Cheap
  // enough to keep as a tripwire for the next layout that collapses.
  lv_obj_update_layout(parent);
  for (int i = 0; i < 2; i++) {
    const int32_t w = lv_obj_get_width(cards[i].pill);
    if (w < lv_obj_get_width(cards[i].pillLabel)) {
      Serial.printf("card%d pill collapsed: %d wide, label needs %d\n", i, (int)w,
                    (int)lv_obj_get_width(cards[i].pillLabel));
    }
  }
}

void updateClaudeScreen(const AppModel &model) {
  // A Mode switch moved the RGB behind the Ramp, the status dot and the wifi
  // glyph while the values under them held; forget the sentinels those colours
  // are gated on so this pass repaints them. The structural colours (pills,
  // card, text) rode the switch on their shared styles already.
  static uint8_t shownGen = 0;
  if (theme::generation() != shownGen) {
    shownGen = theme::generation();
    shownStatus = -1;
    shownBars = -1;
    cards[0].shownUtilization = cards[1].shownUtilization = -2;
    cards[0].shownSeconds = cards[1].shownSeconds = -2;
  }

  updateStatus(model);
  updateClock(model);
  updateCard(cards[0], model.quota.session, QUOTA_WINDOW_SESSION_S);
  updateCard(cards[1], model.quota.weekly, QUOTA_WINDOW_WEEKLY_S);
}

}  // namespace ui
