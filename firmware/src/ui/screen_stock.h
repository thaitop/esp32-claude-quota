// The Stock screen: five tickers, each a price and the day's move.
//
// A list rather than the hero-and-tiles the Weather and Crypto screens wear:
// five symbols do not survive that layout's arithmetic the way one coin does,
// and a desk glance wants every ticker at once rather than one behind a toggle.
// A single badge in the title band says whether the US market is open, since
// all five share the one session.
#pragma once

#include <lvgl.h>

#include "../model.h"

namespace ui {

void buildStockScreen(lv_obj_t *parent);
void updateStockScreen(const AppModel &model);

}  // namespace ui
