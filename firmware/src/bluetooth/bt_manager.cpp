#include "bt_manager.h"
#include "../config.h"
#include <BluetoothA2DPSource.h>
#include <Preferences.h>
#include <esp_bt.h>   // esp_bt_sleep_disable
#include <freertos/FreeRTOS.h>
#include <freertos/ringbuf.h>

// ── Static state ────────────────────────────────────────────
static BluetoothA2DPSource a2dp;
static RingbufHandle_t audioRb = nullptr;
static Preferences prefs;

static String targetName;
static volatile bool started = false;
static volatile bool connected = false;
static volatile bool streaming = false;   // A2DP media stream running
static String connectedDev;

static std::vector<BtDevice> found;
static portMUX_TYPE foundMux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool listDirty = false;

// ── A2DP callbacks (run on BT stack task) ───────────────────

// Stutter diagnosis counters, logged rate-limited from the callbacks.
// Underruns = consumer outran the producer (zero-fill went to the speaker).
// Send timeouts = producer outran the consumer for >500ms (PCM was dropped).
// Underruns while playing → jitter or a decode rate below 44.1k stereo;
// see docs/MEMORY.md.
static volatile uint32_t underrunBytes = 0;
static volatile uint32_t sendTimeouts = 0;
static volatile uint32_t lastProducerWrite = 0;   // millis() of last writeAudio

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

        // Only count as an underrun if the producer wrote recently —
        // draining silence while nothing is playing is expected, not a fault
        uint32_t now = millis();
        if (now - lastProducerWrite < 1000) {
            underrunBytes += len - got;
            static uint32_t lastLog = 0;
            if (now - lastLog > 2000) {
                lastLog = now;
                // 176400 bytes/s at 44.1k stereo → /176 ≈ ms of silence inserted
                Serial.printf("[BT] underrun: %u bytes (~%u ms) zero-filled in last 2s\n",
                              underrunBytes, underrunBytes / 176);
                underrunBytes = 0;
            }
        }
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
    if (!known && found.size() < MAX_BT_DEVICES) {
        found.push_back({String(ssid), rssi});
        listDirty = true;
    }
    portEXIT_CRITICAL(&foundMux);

    return targetName.length() > 0 && targetName == ssid;
}

static void connectionStateChanged(esp_a2d_connection_state_t state, void *) {
    connected = (state == ESP_A2D_CONNECTION_STATE_CONNECTED);
    Serial.printf("[BT] state=%d free=%u largest=%u\n", (int)state,
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    switch (state) {
        case ESP_A2D_CONNECTION_STATE_CONNECTED:
            connectedDev = targetName;
            Serial.printf("[BT] Connected to %s\n", connectedDev.c_str());
            // Kick the media-start handshake NOW, while the heap still has
            // large blocks. The library's own trigger is a 10s heartbeat —
            // by then play() may have fragmented the heap below what
            // Bluedroid's media task needs at START, and the stream then
            // sits in IDLE forever: the data callback never runs and the
            // PCM ring buffer stays full ("Could not write result to out").
            // The ACK lands in the library's state machine, which issues
            // MEDIA_CTRL_START; its heartbeat remains as the retry path.
            esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY);
            break;
        case ESP_A2D_CONNECTION_STATE_CONNECTING:
            Serial.println("[BT] Connecting...");
            break;
        case ESP_A2D_CONNECTION_STATE_DISCONNECTING:
            Serial.println("[BT] Disconnecting...");
            break;
        default:
            streaming = false;   // no media stream without a connection
            Serial.println("[BT] Disconnected");
            break;
    }
}

// The stack only pulls from the ring buffer while audio state is STARTED
// (2). If this never logs 2 after a connect, the media-start handshake
// failed — playback will be silent with the ring buffer stuck full.
static void audioStateChanged(esp_a2d_audio_state_t state, void *) {
    streaming = (state == ESP_A2D_AUDIO_STATE_STARTED);
    Serial.printf("[BT] audio state=%d%s free=%u largest=%u\n", (int)state,
                  streaming ? " (streaming)" : "",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
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
    a2dp.set_on_audio_state_changed(audioStateChanged);
    // Auto-reconnect OFF: with a saved address the library hammers a direct
    // connect (retries x1000) and never runs an inquiry scan, so no devices
    // ever appear in the UI list. We instead rely on continuous discovery +
    // the ssid_callback to auto-connect when the target is seen, which keeps
    // the scan list populated.
    a2dp.set_auto_reconnect(false);
    // Secure Simple Pairing ("Just Works") — required by modern BT speakers
    // (JBL, etc.). Without it the ESP32 falls back to legacy PIN pairing.
    a2dp.set_ssp_enabled(true);
    a2dp.start();

    // Disable BT modem sleep (sniff-mode power save). With it on, the radio
    // periodically idles and source-mode A2DP audio chops at the speaker
    // even though the PCM pipeline is healthy — hardware showed stutter
    // with ZERO ring-buffer underruns/timeouts, i.e. the loss was after
    // the data callback, on the radio link.
    esp_err_t slp = esp_bt_sleep_disable();
    Serial.printf("[BT] modem sleep disabled: %s\n", esp_err_to_name(slp));

    started = true;
    Serial.println("[BT] A2DP source started, discovering...");
}

void BtMgr::stop() {
    if (!started) return;
    a2dp.end(false);   // keep BT controller memory so we can restart
    started = false;
    connected = false;
    streaming = false;
    Serial.println("[BT] A2DP source stopped");
}

bool BtMgr::isStarted() {
    return started;
}

bool BtMgr::isConnected() {
    return connected;
}

bool BtMgr::isStreaming() {
    return streaming;
}

String BtMgr::connectedName() {
    return connected ? connectedDev : String("");
}

void BtMgr::setTarget(const String &name) {
    if (name == targetName) return;   // no change — avoid redundant NVS writes
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
    lastProducerWrite = millis();
    if (xRingbufferSend(audioRb, data, len, pdMS_TO_TICKS(500)) == pdTRUE) {
        return len;
    }
    // Full buffer for 500ms straight = consumer stalled; this PCM is lost
    sendTimeouts++;
    static uint32_t lastLog = 0;
    uint32_t now = millis();
    if (now - lastLog > 2000) {
        lastLog = now;
        Serial.printf("[BT] ringbuf send timeout x%u (consumer stalled, PCM dropped)\n",
                      sendTimeouts);
        sendTimeouts = 0;
    }
    return 0;
}
