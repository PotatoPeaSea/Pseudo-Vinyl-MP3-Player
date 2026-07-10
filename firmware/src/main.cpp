/**
 * Pseudo Vinyl MP3 Player — Firmware
 * Board: ESP32-WROOM-32 (classic ESP32, BT Classic A2DP source)
 *
 * Entry point: initializes all peripherals and spawns FreeRTOS tasks.
 *
 * Task layout (dual-core):
 *   Core 0: audio_task   — MP3 decode + output feed (BT stack also lives here)
 *   Core 1: ui_task      — LVGL display refresh (~60fps)
 *   Core 1: input_task   — Button/encoder polling (5ms)
 *
 * Controls:
 *   Now Playing: PLAY=play/pause, NEXT/PREV=track, encoder=volume,
 *                encoder press=cycle play mode
 *   Menus:       encoder=move focus, PLAY=select, NEXT/PREV=switch screen,
 *                encoder press=back
 */

#include <Arduino.h>
#include <lvgl.h>   // LV_KEY_* codes for UI::sendKey
#include "config.h"
#include "display/display_manager.h"
#include "display/ui_manager.h"
#include "input/input_manager.h"
#include "storage/sd_manager.h"
#include "audio/audio_manager.h"
#include "bluetooth/bt_manager.h"
#if DEBUG_MODE
#include "debug/debug_console.h"
#endif

// ── Task Handles ────────────────────────────────────────────
static TaskHandle_t audioTaskHandle = nullptr;
static TaskHandle_t uiTaskHandle = nullptr;
static TaskHandle_t inputTaskHandle = nullptr;

// ── Song library (shared state) ─────────────────────────────
static std::vector<SongInfo> songs;

// ═══════════════════════════════════════════════════════════
// AUDIO TASK (Core 0) — MP3 decode + output feed
// ═══════════════════════════════════════════════════════════
void audioTask(void *param) {
    Serial.println("[Task] Audio task started on Core 0");

    for (;;) {
        AudioMgr::loop();
        vTaskDelay(pdMS_TO_TICKS(1));   // Yield briefly
    }
}

// ═══════════════════════════════════════════════════════════
// UI TASK (Core 1) — LVGL refresh + state sync
// ═══════════════════════════════════════════════════════════
void uiTask(void *param) {
    Serial.println("[Task] UI task started on Core 1");

    bool lastBtConnected = false;
    uint32_t lastBtPoll = 0;

    for (;;) {
        // Update now-playing info
        const SongInfo *current = AudioMgr::currentSong();
        UI::setNowPlaying(current, AudioMgr::isPlaying());
        UI::setProgress(AudioMgr::positionSec(), AudioMgr::durationSec());
        UI::setVolume(AudioMgr::getVolume(), MAX_VOLUME);
        UI::setPlayModeIndicator(AudioMgr::getPlayMode());

        // Bluetooth state → UI (poll a few times a second, not every frame)
        uint32_t now = millis();
        if (now - lastBtPoll > 250) {
            lastBtPoll = now;

            if (BtMgr::scanListChanged()) {
                UI::setBtDevices(BtMgr::scanResults());
            }

            bool conn = BtMgr::isConnected();
            if (conn != lastBtConnected) {
                lastBtConnected = conn;
                UI::setBtStatus(conn ? ("Connected: " + BtMgr::connectedName())
                                     : (BtMgr::isStarted() ? "Searching..." : "Off"));
            }
        }

        // Spin vinyl + LVGL timers
        UI::update();
        Display::update();

        vTaskDelay(pdMS_TO_TICKS(UI_REFRESH_MS));
    }
}

// ═══════════════════════════════════════════════════════════
// INPUT TASK (Core 1) — Buttons + encoder → events
// ═══════════════════════════════════════════════════════════
static void handleMenuScreenCycle(bool forward) {
    // Menu screen order: SONG_LIST → SETTINGS → BLUETOOTH → NOW_PLAYING
    static const Screen order[] = {
        Screen::SONG_LIST, Screen::SETTINGS, Screen::BLUETOOTH, Screen::NOW_PLAYING
    };
    Screen cur = UI::activeScreen();
    int idx = 0;
    for (int i = 0; i < 4; i++) {
        if (order[i] == cur) { idx = i; break; }
    }
    idx = (idx + (forward ? 1 : 3)) % 4;
    UI::showScreen(order[idx]);
}

void inputTask(void *param) {
    Serial.println("[Task] Input task started on Core 1");

    for (;;) {
        Input::poll();
#if DEBUG_MODE
        DebugConsole::poll();
#endif

        while (Input::hasEvent()) {
            InputEvent evt = Input::getEvent();
            Screen screen = UI::activeScreen();
            bool onNowPlaying = (screen == Screen::NOW_PLAYING);

            switch (evt) {
                case InputEvent::BTN_PLAY:
                    if (onNowPlaying) {
                        if (AudioMgr::isPlaying()) {
                            AudioMgr::pause();
                        } else if (AudioMgr::currentIndex() >= 0) {
                            AudioMgr::resume();
                        } else if (!songs.empty()) {
                            AudioMgr::play(0);
                        }
                    } else {
                        // Select focused item in menus
                        UI::sendKey(LV_KEY_ENTER);
                    }
                    break;

                case InputEvent::BTN_NEXT:
                    if (onNowPlaying) {
                        AudioMgr::next();
                    } else {
                        handleMenuScreenCycle(true);
                    }
                    break;

                case InputEvent::BTN_PREV:
                    if (onNowPlaying) {
                        AudioMgr::prev();
                    } else {
                        handleMenuScreenCycle(false);
                    }
                    break;

                case InputEvent::ENC_CW:
                    if (onNowPlaying) {
                        AudioMgr::setVolume(AudioMgr::getVolume() + 1);
                    } else {
                        UI::sendKey(LV_KEY_NEXT);
                    }
                    break;

                case InputEvent::ENC_CCW:
                    if (onNowPlaying) {
                        AudioMgr::setVolume(AudioMgr::getVolume() - 1);
                    } else {
                        UI::sendKey(LV_KEY_PREV);
                    }
                    break;

                case InputEvent::ENC_PRESS:
                    if (onNowPlaying) {
                        AudioMgr::cyclePlayMode();
                    } else if (screen == Screen::SONG_LIST) {
                        UI::showScreen(Screen::NOW_PLAYING);
                    } else {
                        // Back to song list from Settings/Bluetooth
                        UI::showScreen(Screen::SONG_LIST);
                    }
                    break;

                default:
                    break;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(INPUT_POLL_MS));
    }
}

// ═══════════════════════════════════════════════════════════
// ARDUINO ENTRY POINTS
// ═══════════════════════════════════════════════════════════

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n═══════════════════════════════════");
    Serial.println("  Pseudo Vinyl MP3 Player v2.0");
    Serial.println("  ESP32-WROOM-32 + A2DP");
    Serial.println("═══════════════════════════════════\n");

    Serial.printf("[Heap] boot start: free=%u largest=%u\n",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());

    // 1. Display
    Display::init();
    UI::init();
    Serial.println("[Boot] Display ready");
    Serial.printf("[Heap] after display+UI: free=%u largest=%u\n",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());

    // 2. Input
    Input::init();
#if DEBUG_MODE
    DebugConsole::init();
#endif

    // 3. Bluetooth (target device from NVS) + audio pipeline.
    //    AudioMgr::init starts A2DP if the saved output mode is Bluetooth.
    //    Done BEFORE the SD mount so the Classic-BT / A2DP stack (needs
    //    ~120KB to init) gets the heap while it's still plentiful — the
    //    FATFS mount retains ~80KB and would otherwise starve it.
    Serial.printf("[Heap] before BT/audio: free=%u largest=%u\n",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    BtMgr::init();
    AudioMgr::init();
    UI::setBtStatus(BtMgr::isStarted() ? "Searching..." : "Off");
    Serial.printf("[Heap] after BT/audio: free=%u largest=%u\n",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());

    // 4. SD Card (mounted last — FATFS holds ~80KB while mounted)
    if (Storage::init()) {
        songs = Storage::scanMusic("/");
        AudioMgr::setPlaylist(songs);
        UI::setSongList(songs);
        Serial.printf("[Boot] %d songs loaded\n", songs.size());
    } else {
        Serial.println("[Boot] WARNING: No SD card!");
    }
    Serial.printf("[Heap] after SD: free=%u largest=%u\n",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());

    // 5. Spawn FreeRTOS tasks
    xTaskCreatePinnedToCore(audioTask, "audio", 8192, nullptr, 3, &audioTaskHandle, 0);
    xTaskCreatePinnedToCore(uiTask,    "ui",    8192, nullptr, 1, &uiTaskHandle,    1);
    xTaskCreatePinnedToCore(inputTask, "input", 4096, nullptr, 2, &inputTaskHandle, 1);

    Serial.println("[Boot] All tasks launched\n");
}

void loop() {
    // All work is done in FreeRTOS tasks
    vTaskDelay(pdMS_TO_TICKS(1000));
}
