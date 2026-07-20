// The Crypto screen: one coin's price and what a day of trading did to it.
//
// Three coins are tracked and a toggle at the top right steps between them, so
// which one is on screen is the screen's own state rather than the model's --
// nothing outside here needs to know, and nothing outside here can change it.
#pragma once

#include <lvgl.h>

#include "../model.h"

namespace ui {

void buildCryptoScreen(lv_obj_t *parent);
void updateCryptoScreen(const AppModel &model);

}  // namespace ui
