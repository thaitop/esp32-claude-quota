// The two constructors every screen needs, and the title every screen wears.
//
// Extracted from the Claude screen once a second screen wanted the same three
// lines of style calls. Nothing here decides layout -- position is the
// caller's business, because each screen's spacing was measured against its own
// font metrics rather than derived from a grid.
#pragma once

#include <Arduino.h>
#include <lvgl.h>

#include "fonts/ui_fonts.h"
#include "theme.h"

namespace ui {

inline lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t colour) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
  lv_obj_set_style_text_color(label, theme::colour(colour), LV_PART_MAIN);
  return label;
}

inline lv_obj_t *makePanel(lv_obj_t *parent, int16_t w, int16_t h, int16_t radius,
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

// The bordered surface the Claude, Weather and Crypto screens all put their
// figures on. Same fill, same edge, same radius, because three cards that
// differ by a pixel read as three products.
inline lv_obj_t *makeCard(lv_obj_t *parent, int16_t x, int16_t y, int16_t w,
                          int16_t h) {
  lv_obj_t *card = makePanel(parent, w, h, 12, theme::CARD);
  lv_obj_set_pos(card, x, y);
  lv_obj_set_style_border_color(card, theme::colour(theme::CARD_EDGE), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_border_opa(card, LV_OPA_COVER, LV_PART_MAIN);
  return card;
}

// The heading each screen but Claude carries. Claude has the mascot beside its
// title and builds its own; everything else is this, in the same face and at
// the same baseline, so the set reads as one product.
//
// Inter 22 Bold, matching the Claude screen's "Usage" exactly -- it was Inter
// 27 Regular here, which is the same argument Claude's header already settled:
// at 27 the word outweighs the figures it introduces, and a heading that is
// lighter and larger on four screens than on the fifth reads as two products
// rather than one. Same face, same size, same y as Claude's.
// The icon rides at the same size and the same baseline as Claude's mascot:
// the navbar tiles are drawn at 34 for a row read from a distance, and at full
// size in a header they crowd the title and reach within a pixel of the first
// card. At 26 they sit level with the cap height beside them.
//
// Pass the `_hdr` copy of the artwork, not the navbar tile. The header used to
// take the 34px tile and shrink it with lv_image_set_scale(), which transforms
// about a pivot inside the source and clips to the object's box -- on the
// device that sliced the top off the icon on every screen but Claude, whose
// mascot is the one header image that was never scaled. The generator now emits
// a 26px copy of each tile; the sizes are in tools/mkicons_lvgl.py.
constexpr int16_t TITLE_ICON = 26;

inline lv_obj_t *makeTitle(lv_obj_t *parent, const char *text,
                           const lv_image_dsc_t *icon) {
  if (icon != nullptr) {
    lv_obj_t *tile = lv_image_create(parent);
    lv_image_set_src(tile, icon);
    lv_obj_set_pos(tile, 10, 3);
  }

  lv_obj_t *title = makeLabel(parent, &font_inter_22_bold, theme::TEXT);
  lv_label_set_text(title, text);
  // 44 with an icon, matching the 46 Claude's mascot leaves; 12 without, which
  // is the side padding every card on every screen already uses.
  lv_obj_set_pos(title, icon != nullptr ? 44 : 12, 4);
  return title;
}

// A widget that resolves to no width is invisible and reports nothing, which
// is how the Claude screen's pills disappeared under LV_SIZE_CONTENT. Three
// lines, and it turns a long debugging session into a line on the console.
inline void warnIfCollapsed(const char *what, lv_obj_t *widget) {
  if (lv_obj_get_width(widget) > 0 && lv_obj_get_height(widget) > 0) return;
  Serial.printf("%s collapsed: %dx%d\n", what, (int)lv_obj_get_width(widget),
                (int)lv_obj_get_height(widget));
}

// Complains about anything a Screen has drawn past the bottom of its page.
//
// The navbar is opaque and sits over the last 46 pixels, so a row that overruns
// is not clipped and not reported -- it is simply not there, and the screen
// looks like it was designed with one fewer row. That is how the Setting
// screen's uptime row went missing: nine rows on an 18px pitch reach 202
// against a page 194 tall, which is eight pixels nobody can see and nothing
// counts. Comparing the arithmetic against what LVGL resolved costs one pass
// at boot.
inline void warnIfOverflowing(const char *name, lv_obj_t *page) {
  const int32_t limit = lv_obj_get_height(page);
  for (uint32_t i = 0; i < lv_obj_get_child_count(page); i++) {
    lv_obj_t *child = lv_obj_get_child(page, i);
    const int32_t bottom = lv_obj_get_y(child) + lv_obj_get_height(child);
    if (bottom > limit) {
      Serial.printf("%s child %u runs to %d, page is %d tall\n", name, (unsigned)i,
                    (int)bottom, (int)limit);
    }
  }
}

}  // namespace ui
