// The navbar and the screen it selects.
//
// One Slot along the bottom edge per Screen, one Screen behind each. The Slots
// are the controls; the Screens are the destinations -- see CONTEXT.md, which
// keeps those two words apart deliberately. The bar sizes itself from
// Screen::Count, so a Screen added here widens the row rather than needing the
// geometry retuned.
#pragma once

#include <lvgl.h>

#include <cstdint>

namespace ui {

enum class Screen : uint8_t {
  Claude = 0,
  Weekly,
  Weather,
  Crypto,
  Stock,
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
