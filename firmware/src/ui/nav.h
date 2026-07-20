// The navbar and the screen it selects.
//
// Five Slots along the bottom edge, one Screen behind each. The Slots are the
// controls; the Screens are the destinations -- see CONTEXT.md, which keeps
// those two words apart deliberately.
#pragma once

#include <lvgl.h>

#include <cstdint>

namespace ui {

enum class Screen : uint8_t {
  Claude = 0,
  Weekly,
  Weather,
  Crypto,
  Setting,
  Count,
};

// Builds the page containers and the navbar into `parent`. Each page is the
// full width and everything above the navbar; screens draw into their own page
// and never have to know the navbar exists.
void buildShell(lv_obj_t *parent);

lv_obj_t *page(Screen screen);
void showScreen(Screen screen);
Screen activeScreen();

}  // namespace ui
