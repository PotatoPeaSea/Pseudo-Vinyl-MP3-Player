#pragma once

/**
 * Debug Console — serial commands drive physical IO
 *
 * Always compiled in. Injects input events alongside the hardware buttons/
 * encoder, so you can drive the device from the serial monitor even when the
 * controls aren't wired up. Type "help" for the command list.
 */

namespace DebugConsole {
    /// Print the banner + command reference
    void init();

    /// Read pending serial input and inject matching input events
    /// (call from the input task, alongside Input::poll)
    void poll();
}
