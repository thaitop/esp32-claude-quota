// The Setting screen: what the device knows about its own health.
//
// A readout, plus one door out of it: the brightness and theme controls in the
// header, and the Config button that hands off to Config Mode (ADR-0005) for the
// three settings a person changes without a toolchain -- the weather location,
// the coins, the stocks. Everything else here is still reflash-to-change, which
// is the accepted cost of never fighting a keyboard on a resistive panel.
#pragma once

#include <lvgl.h>

#include "../model.h"

namespace ui {

// `onConfig` is invoked when the Config button in the header is tapped -- the
// one editor this otherwise read-only screen offers, handing off to Config Mode
// (net/config_portal). Passed in rather than called directly so this screen
// stays ignorant of the radio and the web server, the same seam every other
// screen keeps; main.cpp is the one place that bridges the two.
void buildSettingScreen(lv_obj_t *parent, void (*onConfig)());

// Takes the clock as an argument rather than reading millis() itself: every
// row here is an age, and rows computed against different instants inside one
// repaint disagree with each other by a frame.
void updateSettingScreen(const AppModel &model, uint32_t nowMs);

}  // namespace ui
