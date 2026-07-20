// LVGL 9 configuration for the ESP32-2432S028R.
//
// Deliberately short. lv_conf_internal.h wraps every option in #ifndef, so
// anything omitted here keeps LVGL's own default -- copying the 1500-line
// template would bury the handful of settings that actually matter for this
// board under a wall of values nobody chose.
//
// The exception is the widget list: LVGL enables nearly all of them by
// default, so the ones this project does not use have to be switched off
// explicitly or they ride along in the image.
//
// The guard has to be LV_CONF_H specifically, not #pragma once: lv_conf_internal.h
// tests for that macro to decide whether the include actually landed, and with
// #pragma once it warns and then silently falls back to every default -- which
// compiles fine and drags in every widget.
#ifndef LV_CONF_H
#define LV_CONF_H

// ---------------------------------------------------------------------------
// Colour and memory
// ---------------------------------------------------------------------------

#define LV_COLOR_DEPTH 16

// LVGL's own heap, used for objects and styles rather than for pixels -- the
// draw buffers are allocated separately in display.cpp. 40KB holds the five
// screens with room to spare; see ADR-0003 for why this cannot simply grow.
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (40 * 1024U)

// 32-bit alignment keeps the SPI DMA path happy.
#define LV_DRAW_BUF_ALIGN 4

#define LV_DEF_REFR_PERIOD 33  // ~30fps, which the ILI9341 over SPI can hold
#define LV_USE_OS LV_OS_NONE

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

// Warnings and errors go to the serial console. On this board a failed
// allocation otherwise shows up only as a reboot minutes later.
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

// Left off in normal builds: the overlay costs a font and a redraw region.
// Turn on while chasing frame time or heap.
#define LV_USE_SYSMON 0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0

// ---------------------------------------------------------------------------
// Fonts
// ---------------------------------------------------------------------------

// All display type is Inter, generated into src/ui/fonts by
// tools/mkfont_lvgl.py. Montserrat 14 stays enabled as LVGL's fallback so an
// unset style still renders something legible rather than nothing.
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

// ---------------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------------

// Kept: label (all type), image (icons and mascot), bar (quota progress),
// chart (the 7-day history), button and obj (navbar slots, cards).
#define LV_USE_LABEL 1
#define LV_USE_IMAGE 1
#define LV_USE_BAR 1
#define LV_USE_CHART 1
#define LV_USE_BUTTON 1
#define LV_USE_LINE 1

// Everything else off.
#define LV_USE_ANIMIMG 0
#define LV_USE_ARC 0
#define LV_USE_ARCLABEL 0
#define LV_USE_BUTTONMATRIX 0
#define LV_USE_CALENDAR 0
#define LV_USE_CANVAS 0
#define LV_USE_CHECKBOX 0
#define LV_USE_DROPDOWN 0
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_KEYBOARD 0
#define LV_USE_LED 0
#define LV_USE_LIST 0
#define LV_USE_LOTTIE 0
#define LV_USE_MENU 0
#define LV_USE_MSGBOX 0
#define LV_USE_ROLLER 0
#define LV_USE_SCALE 0
#define LV_USE_SLIDER 0
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0
#define LV_USE_SPINNER 0
#define LV_USE_SWITCH 0
#define LV_USE_TABLE 0
#define LV_USE_TABVIEW 0
#define LV_USE_TEXTAREA 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0

// ---------------------------------------------------------------------------
// Drivers
// ---------------------------------------------------------------------------

// LVGL ships a TFT_eSPI driver, but it allocates a single draw buffer and
// forces rotation 0. This project needs double buffering (ADR-0003) and
// landscape, so display.cpp wires the flush callback itself.
#define LV_USE_TFT_ESPI 0

#endif  // LV_CONF_H
