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

void setBacklight(bool on);
bool backlightOn();

// Free heap in bytes, for the Setting screen's diagnostics readout.
uint32_t freeHeap();

// The raw controller reading behind the last touch, before it was mapped to
// screen coordinates. Only useful while calibrating.
void lastRawTouch(int16_t &x, int16_t &y);

}  // namespace display
