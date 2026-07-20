// LVGL bring-up: panel, draw buffers, touch input, backlight.
//
// The only file in the project that knows both LVGL and the board's wiring.
// Everything above it talks to LVGL objects; nothing above it touches TFT_eSPI.
#pragma once

#include <cstdint>

namespace display {

constexpr int16_t WIDTH = 320;
constexpr int16_t HEIGHT = 240;

// Brings up the panel, registers the display and the touch input device, and
// turns the backlight on. Returns false if either draw buffer could not be
// allocated, which on this board means something upstream has already eaten
// the heap -- see ADR-0003.
bool begin();

// Drives LVGL's timers. Call every loop; it rate-limits itself.
void tick();

// Blanks or restores the panel. "On" means at whatever brightness was last
// set, not full -- the two are independent: blanking is a gesture, brightness
// is a setting that survives it and survives a reboot.
void setBacklight(bool on);
bool backlightOn();

// Brightness as a percentage, clamped to BACKLIGHT_MIN_PCT..BACKLIGHT_MAX_PCT
// and rounded to nothing -- the Setting screen steps it, this just stores it.
// Persisted to NVS, so the device comes back at the level it was left at.
// Setting it while the panel is blanked stores the value without lighting it.
void setBrightness(uint8_t percent);
uint8_t brightness();

// Free heap in bytes, for the Setting screen's diagnostics readout.
uint32_t freeHeap();

// The raw controller reading behind the last touch, before it was mapped to
// screen coordinates. Only useful while calibrating.
void lastRawTouch(int16_t &x, int16_t &y);

}  // namespace display
