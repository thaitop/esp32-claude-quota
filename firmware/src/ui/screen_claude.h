// The Claude screen: the two quota windows, exactly as the design shows them.
#pragma once

#include <lvgl.h>

#include "../model.h"

namespace ui {

// Builds the screen's contents into `parent` once. Call update() afterwards
// whenever the model changes -- it only touches what actually differs.
void buildClaudeScreen(lv_obj_t *parent);
void updateClaudeScreen(const AppModel &model);

}  // namespace ui
