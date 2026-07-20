// The Weather screen: current conditions where the display is sitting.
#pragma once

#include <lvgl.h>

#include "../model.h"

namespace ui {

void buildWeatherScreen(lv_obj_t *parent);
void updateWeatherScreen(const AppModel &model);

}  // namespace ui
