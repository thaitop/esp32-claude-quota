// The Crypto screen: one coin's price and where it has been over a day.
#pragma once

#include <lvgl.h>

#include "../model.h"

namespace ui {

void buildCryptoScreen(lv_obj_t *parent);
void updateCryptoScreen(const AppModel &model);

}  // namespace ui
