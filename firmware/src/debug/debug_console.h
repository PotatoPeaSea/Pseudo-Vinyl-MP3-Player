#pragma once

/**
 * Debug Console — serial commands simulate physical IO
 *
 * Only compiled in when DEBUG_MODE=1 (esp32dev-debug build environment).
 * Type commands into the serial monitor to drive the device without
 * buttons or a rotary encoder wired up. Type "help" for the command list.
 */

namespace DebugConsole {
    /// Print the banner + command reference
    void init();

    /// Read pending serial input and inject matching input events
    /// (call from the input task, alongside Input::poll)
    void poll();
}
