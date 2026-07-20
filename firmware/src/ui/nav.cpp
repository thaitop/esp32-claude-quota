#include "nav.h"

#include "../config.h"
#include "../display.h"
#include "theme.h"
#include "ui_icons.h"

namespace ui {
namespace {

constexpr int16_t SLOTS = (int16_t)Screen::Count;
constexpr int16_t NAV_X = 8;
constexpr int16_t NAV_W = display::WIDTH - 2 * NAV_X;
constexpr int16_t NAV_Y = display::HEIGHT - NAV_VISUAL_H - 2;
constexpr int16_t SLOT_W = NAV_W / SLOTS;
constexpr int16_t ICON = 34;
constexpr int16_t MARK_W = 26;
constexpr int16_t MARK_H = 2;

// Inset equally on all four sides. Centring each tile inside its fifth of the
// bar put the outer two 16px from the edge while the bar's own height left
// only 6px above and below them, and the row read as though it had been
// squeezed vertically.
constexpr int16_t NAV_PAD = (NAV_VISUAL_H - ICON) / 2;
constexpr int16_t ICON_GAP = (NAV_W - 2 * NAV_PAD - SLOTS * ICON) / (SLOTS - 1);

// Inactive tiles are dimmed rather than greyed. The design used a coloured
// label under each icon to show which was live; with the labels gone, the
// contrast between a full-strength tile and a muted one has to carry it.
constexpr lv_opa_t OPA_ACTIVE = LV_OPA_COVER;
constexpr lv_opa_t OPA_IDLE = LV_OPA_50;

lv_obj_t *pages[SLOTS] = {nullptr};
lv_obj_t *icons[SLOTS] = {nullptr};
lv_obj_t *marker = nullptr;
Screen active = Screen::Claude;

const lv_image_dsc_t *iconFor(int slot) {
  switch (slot) {
    case 0: return &icon_claude;
    case 1: return &icon_weekly;
    case 2: return &icon_weather;
    case 3: return &icon_crypto;
    default: return &icon_setting;
  }
}

// Where the tile sits. The touch targets still tile the bar in equal fifths,
// so the gaps between tiles stay live rather than becoming dead strips.
int16_t iconLeft(int slot) { return NAV_X + NAV_PAD + slot * (ICON + ICON_GAP); }
int16_t iconCentre(int slot) { return iconLeft(slot) + ICON / 2; }

void onSlotPressed(lv_event_t *event) {
  const int slot = (int)(intptr_t)lv_event_get_user_data(event);
  showScreen((Screen)slot);
}

}  // namespace

void buildShell(lv_obj_t *parent) {
  for (int i = 0; i < SLOTS; i++) {
    lv_obj_t *pageObj = lv_obj_create(parent);
    lv_obj_remove_style_all(pageObj);
    lv_obj_set_size(pageObj, display::WIDTH, NAV_Y);
    lv_obj_set_pos(pageObj, 0, 0);
    lv_obj_remove_flag(pageObj, LV_OBJ_FLAG_SCROLLABLE);
    if (i != 0) lv_obj_add_flag(pageObj, LV_OBJ_FLAG_HIDDEN);
    pages[i] = pageObj;
  }

  lv_obj_t *bar = lv_obj_create(parent);
  lv_obj_remove_style_all(bar);
  lv_obj_set_size(bar, NAV_W, NAV_VISUAL_H);
  lv_obj_set_pos(bar, NAV_X, NAV_Y);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(bar, 10, LV_PART_MAIN);
  lv_obj_set_style_border_color(bar, theme::colour(theme::NAV_EDGE), LV_PART_MAIN);
  lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN);
  lv_obj_set_style_border_opa(bar, LV_OPA_COVER, LV_PART_MAIN);

  for (int i = 0; i < SLOTS; i++) {
    lv_obj_t *tile = lv_image_create(parent);
    lv_image_set_src(tile, iconFor(i));
    lv_obj_set_pos(tile, iconLeft(i), NAV_Y + NAV_PAD);
    icons[i] = tile;

    // The touch target is taller than the tile it sits behind. A stylus on a
    // resistive panel lands a couple of pixels off, and a 28px target at the
    // very bottom edge of the display is where that shows up worst.
    lv_obj_t *hit = lv_obj_create(parent);
    lv_obj_remove_style_all(hit);
    lv_obj_set_size(hit, SLOT_W, NAV_TOUCH_H);
    lv_obj_set_pos(hit, NAV_X + i * SLOT_W, display::HEIGHT - NAV_TOUCH_H);
    lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(hit, onSlotPressed, LV_EVENT_CLICKED, (void *)(intptr_t)i);
  }

  marker = lv_obj_create(parent);
  lv_obj_remove_style_all(marker);
  lv_obj_set_size(marker, MARK_W, MARK_H);
  lv_obj_set_style_radius(marker, MARK_H / 2, LV_PART_MAIN);
  lv_obj_set_style_bg_color(marker, theme::colour(theme::ACCENT), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(marker, LV_OPA_COVER, LV_PART_MAIN);

  showScreen(Screen::Claude);
}

lv_obj_t *page(Screen screen) { return pages[(int)screen]; }

Screen activeScreen() { return active; }

void showScreen(Screen screen) {
  const int slot = (int)screen;
  if (slot < 0 || slot >= SLOTS) return;

  active = screen;
  for (int i = 0; i < SLOTS; i++) {
    if (i == slot) {
      lv_obj_remove_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_opa(icons[i], i == slot ? OPA_ACTIVE : OPA_IDLE, LV_PART_MAIN);
  }

  lv_obj_set_pos(marker, iconCentre(slot) - MARK_W / 2,
                 NAV_Y + NAV_VISUAL_H - MARK_H - 2);
}

}  // namespace ui
