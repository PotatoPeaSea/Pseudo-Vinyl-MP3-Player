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
}
