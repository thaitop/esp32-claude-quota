// A page that admits it has nothing on it yet.
//
// Used for the screens that arrive in later steps. It exists so the navbar can
// be built and exercised now: a slot that switches to a blank page looks
// broken, and a slot that does nothing at all cannot be tested.
#pragma once

#include <lvgl.h>

namespace ui {

void buildPlaceholder(lv_obj_t *page, const char *title, const char *detail);

}  // namespace ui
