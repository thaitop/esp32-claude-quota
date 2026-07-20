#include "screen_claude.h"

#include <Arduino.h>

#include "../display.h"
#include "fonts/ui_fonts.h"
#include "format.h"
#include "theme.h"
#include "ui_icons.h"

namespace ui {
namespace {

// 240px of height has to hold a header, two cards and the navbar. Every number
// here was chosen against the real font metrics rather than guessed, which is
// why the countdown clears the progress bar by exactly two pixels.
constexpr int16_t HEADER_H = 34;
constexpr int16_t CARD_X = 8;
constexpr int16_t CARD_W = display::WIDTH - 2 * CARD_X;
constexpr int16_t CARD_H = 76;
// Shifted up two pixels from the first pass to make room for the taller
// navbar the 34px tiles need.
constexpr int16_t CARD_Y[2] = {36, 114};

// The vertical stack inside a 76px card, with the real font metrics rather
// than round numbers: Inter 36 sets a 44px line, Inter 14 an 18px one. The
// first pass put the countdown at 58 and it ran two pixels past the card's
// bottom edge, straight through the border.
constexpr int16_t PAD = 14;
constexpr int16_t PCT_Y = -3;   // Inter 36's line box carries more leading
                                // above the digits than the design shows
constexpr int16_t PILL_Y = 8;
constexpr int16_t PILL_H = 22;
constexpr int16_t PILL_PAD = 13;
constexpr int16_t BAR_Y = 45;   // 45..53
constexpr int16_t BAR_H = 8;
constexpr int16_t FOOT_Y = 57;  // 57..72 at Inter 12, four clear of the bar
constexpr int16_t CLOCK = 14;

struct CardWidgets {
  lv_obj_t *root = nullptr;
  lv_obj_t *utilization = nullptr;
  lv_obj_t *pill = nullptr;
  lv_obj_t *pillLabel = nullptr;
  lv_obj_t *bar = nullptr;
  lv_obj_t *clock = nullptr;
  lv_obj_t *resets = nullptr;

  // Last rendered values, so a per-second countdown does not rewrite the
  // percentage and the bar forty times a minute.
  int8_t shownUtilization = -2;
  int32_t shownSeconds = -2;
};

CardWidgets cards[2];
lv_obj_t *statusLabel = nullptr;
lv_obj_t *statusDot = nullptr;
lv_obj_t *wifiGlyph = nullptr;
int shownStatus = -1;

lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t colour) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
  lv_obj_set_style_text_color(label, theme::colour(colour), LV_PART_MAIN);
  return label;
}

lv_obj_t *makePanel(lv_obj_t *parent, int16_t w, int16_t h, int16_t radius,
                    uint32_t fill) {
  lv_obj_t *panel = lv_obj_create(parent);
  lv_obj_remove_style_all(panel);
  lv_obj_set_size(panel, w, h);
  lv_obj_set_style_radius(panel, radius, LV_PART_MAIN);
  lv_obj_set_style_bg_color(panel, theme::colour(fill), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  return panel;
}

void buildHeader(lv_obj_t *parent) {
  lv_obj_t *mascot = lv_image_create(parent);
  lv_image_set_src(mascot, &img_mascot);
  lv_obj_set_pos(mascot, 10, 5);

  lv_obj_t *title = makeLabel(parent, &font_inter_27, theme::TEXT);
  lv_label_set_text(title, "Usage");
  lv_obj_set_pos(title, 46, 1);

  wifiGlyph = lv_image_create(parent);
  lv_image_set_src(wifiGlyph, &glyph_wifi);
  lv_obj_set_pos(wifiGlyph, 200, 9);
  lv_obj_set_style_image_recolor_opa(wifiGlyph, LV_OPA_COVER, LV_PART_MAIN);

  statusLabel = makeLabel(parent, &font_inter_15, theme::GREEN);
  lv_obj_set_pos(statusLabel, 230, 9);

  statusDot = makePanel(parent, 8, 8, LV_RADIUS_CIRCLE, theme::GREEN);
  lv_obj_set_pos(statusDot, 300, 13);
}

void buildCard(CardWidgets &card, lv_obj_t *parent, int index, const char *badge,
               uint32_t pillBg, uint32_t pillTx) {
  card.root = makePanel(parent, CARD_W, CARD_H, 12, theme::CARD);
  lv_obj_set_pos(card.root, CARD_X, CARD_Y[index]);
  lv_obj_set_style_border_color(card.root, theme::colour(theme::CARD_EDGE),
                                LV_PART_MAIN);
  lv_obj_set_style_border_width(card.root, 1, LV_PART_MAIN);
  lv_obj_set_style_border_opa(card.root, LV_OPA_COVER, LV_PART_MAIN);

  card.utilization = makeLabel(card.root, &font_inter_36, theme::TEXT);
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
  lv_obj_set_style_bg_color(card.bar, theme::colour(theme::TRACK), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card.bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(card.bar, BAR_H / 2, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(card.bar, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_bg_grad_dir(card.bar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);

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

void updateCard(CardWidgets &card, const QuotaWindow &window) {
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
  }

  if (window.secondsToReset != card.shownSeconds) {
    card.shownSeconds = window.secondsToReset;
    char text[24];
    char span[16];
    format::duration(window.secondsToReset, span, sizeof(span));
    snprintf(text, sizeof(text), "Resets in %s", span);
    lv_label_set_text(card.resets, text);
  }
}

void updateStatus(const AppModel &model) {
  const bool online = model.wifiAssociated && model.quota.trusted;
  const int status = (model.wifiAssociated ? 1 : 0) + (model.quota.trusted ? 2 : 0);
  if (status == shownStatus) return;
  shownStatus = status;

  const uint32_t tint =
      online ? theme::GREEN : (model.wifiAssociated ? theme::AMBER : theme::RED);
  const char *word =
      online ? "Online" : (model.wifiAssociated ? "No data" : "Offline");

  lv_label_set_text(statusLabel, word);
  lv_obj_set_style_text_color(statusLabel, theme::colour(tint), LV_PART_MAIN);
  lv_obj_set_style_bg_color(statusDot, theme::colour(tint), LV_PART_MAIN);
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
  updateStatus(model);
  updateCard(cards[0], model.quota.session);
  updateCard(cards[1], model.quota.weekly);
}

}  // namespace ui
