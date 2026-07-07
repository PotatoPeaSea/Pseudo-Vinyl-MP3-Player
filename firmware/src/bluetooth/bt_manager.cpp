#include "bt_manager.h"
#include "../config.h"
#include <BluetoothA2DPSource.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/ringbuf.h>

// ── Static state ────────────────────────────────────────────
static BluetoothA2DPSource a2dp;
static RingbufHandle_t audioRb = nullptr;
static Preferences prefs;

static String targetName;
static volatile bool started = false;
static volatile bool connected = false;
static String connectedDev;

static std::vector<BtDevice> found;
static portMUX_TYPE foundMux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool listDirty = false;

// ── A2DP callbacks (run on BT stack task) ───────────────────

// Feed PCM to the BT stack; zero-fill on underrun so the stream never stalls
static int32_t dataCallback(uint8_t *data, int32_t len) {
    size_t got = 0;
    while (got < (size_t)len) {
        size_t itemSize = 0;
        uint8_t *item = (uint8_t *)xRingbufferReceiveUpTo(audioRb, &itemSize, 0, len - got);
        if (!item) break;
        memcpy(data + got, item, itemSize);
        vRingbufferReturnItem(audioRb, item);
        got += itemSize;
    }
    if (got < (size_t)len) {
        memset(data + got, 0, len - got);
    }
    return len;
}

// Called for every discovered device; return true to connect to it
static bool ssidCallback(const char *ssid, esp_bd_addr_t address, int rssi) {
    portENTER_CRITICAL(&foundMux);
    bool known = false;
    for (auto &d : found) {
        if (d.name == ssid) { d.rssi = rssi; known = true; break; }
    }
    if (!known) {
        found.push_back({String(ssid), rssi});
        listDirty = true;
    }
    portEXIT_CRITICAL(&foundMux);

    return targetName.length() > 0 && targetName == ssid;
}

static void connectionStateChanged(esp_a2d_connection_state_t state, void *) {
    connected = (state == ESP_A2D_CONNECTION_STATE_CONNECTED);
    if (connected) {
        connectedDev = targetName;
        Serial.printf("[BT] Connected to %s\n", connectedDev.c_str());
    } else {
        Serial.println("[BT] Disconnected");
    }
}

// ── Public API ──────────────────────────────────────────────

void BtMgr::init() {
    prefs.begin("btcfg", false);
    targetName = prefs.getString("target", "");
    if (targetName.length()) {
        Serial.printf("[BT] Saved target: %s\n", targetName.c_str());
    }
}

void BtMgr::start() {
    if (started) return;

    if (!audioRb) {
        audioRb = xRingbufferCreate(BT_RINGBUF_BYTES, RINGBUF_TYPE_BYTEBUF);
    }

    portENTER_CRITICAL(&foundMux);
    found.clear();
    listDirty = true;
    portEXIT_CRITICAL(&foundMux);

    a2dp.set_local_name(BT_DEVICE_NAME);
    a2dp.set_ssid_callback(ssidCallback);
    a2dp.set_data_callback(dataCallback);
    a2dp.set_on_connection_state_changed(connectionStateChanged);
    a2dp.set_auto_reconnect(true);
    a2dp.start();

    started = true;
    Serial.println("[BT] A2DP source started, discovering...");
}

void BtMgr::stop() {
    if (!started) return;
    a2dp.end(false);   // keep BT controller memory so we can restart
    started = false;
    connected = false;
    Serial.println("[BT] A2DP source stopped");
}

bool BtMgr::isStarted() {
    return started;
}

bool BtMgr::isConnected() {
    return connected;
}

String BtMgr::connectedName() {
    return connected ? connectedDev : String("");
}

void BtMgr::setTarget(const String &name) {
    targetName = name;
    prefs.putString("target", name);
    Serial.printf("[BT] Target set: %s\n", name.c_str());
    // If already connected to something else, drop it so discovery
    // can pick up the new target
    if (connected && connectedDev != name) {
        a2dp.disconnect();
    }
}

String BtMgr::getTarget() {
    return targetName;
}

std::vector<BtDevice> BtMgr::scanResults() {
    portENTER_CRITICAL(&foundMux);
    std::vector<BtDevice> copy = found;
    portEXIT_CRITICAL(&foundMux);
    return copy;
}

bool BtMgr::scanListChanged() {
    if (!listDirty) return false;
    listDirty = false;
    return true;
}

size_t BtMgr::writeAudio(const uint8_t *data, size_t len) {
    if (!started || !connected || !audioRb) {
        return len;   // drop silently; caller gates on isConnected()
    }
    if (xRingbufferSend(audioRb, data, len, pdMS_TO_TICKS(500)) == pdTRUE) {
        return len;
    }
    return 0;
}
