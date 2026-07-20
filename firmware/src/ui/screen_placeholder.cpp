#include "screen_placeholder.h"

#include "fonts/ui_fonts.h"
#include "theme.h"

namespace ui {

void buildPlaceholder(lv_obj_t *page, const char *title, const char *detail) {
  lv_obj_t *heading = lv_label_create(page);
  lv_label_set_text(heading, title);
  lv_obj_set_style_text_font(heading, &font_inter_27, LV_PART_MAIN);
  lv_obj_set_style_text_color(heading, theme::colour(theme::TEXT), LV_PART_MAIN);
  lv_obj_align(heading, LV_ALIGN_CENTER, 0, -18);

  lv_obj_t *note = lv_label_create(page);
  lv_label_set_text(note, detail);
  lv_obj_set_style_text_font(note, &font_inter_15, LV_PART_MAIN);
  lv_obj_set_style_text_color(note, theme::colour(theme::MUTED), LV_PART_MAIN);
  lv_obj_align(note, LV_ALIGN_CENTER, 0, 12);
}

}  // namespace ui
