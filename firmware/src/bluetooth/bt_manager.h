#pragma once
#include <Arduino.h>
#include <vector>

/**
 * Bluetooth Manager — A2DP Source (classic ESP32 only)
 *
 * Streams decoded PCM to BT earbuds/speakers via a ring buffer:
 * the audio pipeline pushes with writeAudio(), the A2DP stack pulls
 * from its own task via the data callback.
 *
 * Device discovery runs continuously while unconnected; every sink
 * seen is collected for the UI. Selecting a device sets it as the
 * target — the next discovery hit on that name connects and the name
 * is persisted to NVS for auto-reconnect on later boots.
 */

struct BtDevice {
    String name;
    int rssi;
};

namespace BtMgr {
    /// Load persisted target device from NVS (call once at boot)
    void init();

    /// Start the A2DP source + discovery (BT output mode)
    void start();

    /// Stop A2DP (wired output mode)
    void stop();

    bool isStarted();
    bool isConnected();
    String connectedName();

    /// Set the device to connect to (persists to NVS)
    void setTarget(const String &name);
    String getTarget();

    /// Devices discovered so far (cleared on start())
    std::vector<BtDevice> scanResults();

    /// True once when the scan list changed since last call (UI polling)
    bool scanListChanged();

    /// Push decoded PCM (44.1kHz 16-bit stereo). Returns bytes accepted.
    size_t writeAudio(const uint8_t *data, size_t len);
}
