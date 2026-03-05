#pragma once
#include <Arduino.h>

/**
 * Input Manager — Buttons + Rotary Encoder
 *
 * Polls GPIOs with software debounce.
 * Events are consumed by the UI/audio managers.
 */

enum class InputEvent : uint8_t {
    NONE = 0,
    BTN_PLAY,           // Play/Pause button pressed
    BTN_NEXT,           // Next track button pressed
    BTN_PREV,           // Previous track button pressed
    ENC_CW,             // Encoder rotated clockwise (volume up)
    ENC_CCW,            // Encoder rotated counter-clockwise (volume down)
    ENC_PRESS,          // Encoder shaft pressed (cycle mode)
};

namespace Input {
    /// Initialize GPIO pins and attach interrupts
    void init();

    /// Poll for input events (call from input task)
    void poll();

    /// Get the next pending input event (returns NONE if empty)
    InputEvent getEvent();

    /// Check if there are pending events
    bool hasEvent();
}
