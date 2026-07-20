// The Weekly Usage screen: seven days of Samples, plotted.
//
// Every Sample carries the weekly utilization as it stood at that moment, so
// the curve reads as one continuous climb towards the limit rather than as a
// row of unrelated daily totals.
#pragma once

#include <lvgl.h>

#include "../model.h"

namespace ui {

void buildWeeklyScreen(lv_obj_t *parent);
void updateWeeklyScreen(const AppModel &model);

}  // namespace ui
