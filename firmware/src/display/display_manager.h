#pragma once
#include <Arduino.h>

/**
 * Display Manager — TFT_eSPI + LVGL integration for GC9A01
 */
namespace Display {
    /// Initialize TFT_eSPI, LVGL, and display buffers
    void init();

    /// Call from the UI task loop to drive LVGL timers
    void update();

    /// Set backlight brightness (0-255)
    void setBacklight(uint8_t brightness);

    /// Put the GC9A01 panel into sleep mode (SLPIN + DISPOFF) and stop
    /// LVGL rendering. NOTE: this module's backlight has no control pin
    /// (see setBacklight() above) — the backlight stays lit, so this saves
    /// panel-driver/SPI power but does not produce a visually black screen.
    /// See docs/HANDOFF.md for what "off" actually looks like here.
    void sleep();

    /// Wake the panel (SLPOUT + DISPON) and resume LVGL rendering.
    void wake();

    /// True after sleep(), false after wake() or init().
    bool isAsleep();
}
