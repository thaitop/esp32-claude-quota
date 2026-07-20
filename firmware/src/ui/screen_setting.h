// The Setting screen: what the device knows about its own health.
//
// A readout and not an editor. Configuration lives in the committed tunables
// and the gitignored secrets file, and changing it means reflashing -- which is
// the accepted cost of never fighting a keyboard on a resistive panel.
#pragma once

#include <lvgl.h>

#include "../model.h"

namespace ui {

void buildSettingScreen(lv_obj_t *parent);

// Takes the clock as an argument rather than reading millis() itself: every
// row here is an age, and rows computed against different instants inside one
// repaint disagree with each other by a frame.
void updateSettingScreen(const AppModel &model, uint32_t nowMs);

}  // namespace ui
