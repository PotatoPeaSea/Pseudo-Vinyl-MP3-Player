/**
 * Pseudo Vinyl MP3 Player — Firmware
 *
 * Entry point: initializes all peripherals and spawns FreeRTOS tasks.
 *
 * Task layout (dual-core ESP32-S3):
 *   Core 0: audio_task   — MP3 decode + I2S output (high priority)
 *   Core 1: ui_task      — LVGL display refresh (~60fps)
 *   Core 1: input_task   — Button/encoder polling (5ms)
 */

#include <Arduino.h>
#include "config.h"
#include "display/display_manager.h"
#include "display/ui_manager.h"
#include "input/input_manager.h"
#include "storage/sd_manager.h"
#include "audio/audio_manager.h"

// ── Task Handles ────────────────────────────────────────────
static TaskHandle_t audioTaskHandle = nullptr;
static TaskHandle_t uiTaskHandle = nullptr;
static TaskHandle_t inputTaskHandle = nullptr;

// ── Song library (shared state) ─────────────────────────────
static std::vector<SongInfo> songs;
static Screen currentScreen = Screen::SONG_LIST;

// ═══════════════════════════════════════════════════════════
// AUDIO TASK (Core 0) — MP3 decode + I2S output
// ═══════════════════════════════════════════════════════════
void audioTask(void *param) {
    Serial.println("[Task] Audio task started on Core 0");
    Audio::init();

    for (;;) {
        Audio::loop();
        vTaskDelay(pdMS_TO_TICKS(1));   // Yield briefly
    }
}

// ═══════════════════════════════════════════════════════════
// UI TASK (Core 1) — LVGL refresh + state sync
// ═══════════════════════════════════════════════════════════
void uiTask(void *param) {
    Serial.println("[Task] UI task started on Core 1");

    for (;;) {
        // Update now-playing info
        const SongInfo *current = Audio::currentSong();
        UI::setNowPlaying(current, Audio::isPlaying());
        UI::setProgress(Audio::positionSec(), Audio::durationSec());
        UI::setVolume(Audio::getVolume(), MAX_VOLUME);
        UI::setPlayModeIndicator(Audio::getPlayMode());

        // Spin vinyl + LVGL timers
        UI::update();
        Display::update();

        vTaskDelay(pdMS_TO_TICKS(UI_REFRESH_MS));
    }
}

// ═══════════════════════════════════════════════════════════
// INPUT TASK (Core 1) — Buttons + encoder → events
// ═══════════════════════════════════════════════════════════
void inputTask(void *param) {
    Serial.println("[Task] Input task started on Core 1");

    for (;;) {
        Input::poll();

        while (Input::hasEvent()) {
            InputEvent evt = Input::getEvent();

            switch (evt) {
                case InputEvent::BTN_PLAY:
                    if (currentScreen == Screen::NOW_PLAYING) {
                        if (Audio::isPlaying()) {
                            Audio::pause();
                        } else if (Audio::currentIndex() >= 0) {
                            Audio::resume();
                        } else if (!songs.empty()) {
                            Audio::play(0);
                            UI::showScreen(Screen::NOW_PLAYING);
                            currentScreen = Screen::NOW_PLAYING;
                        }
                    } else if (currentScreen == Screen::SONG_LIST) {
                        // Toggle to Now Playing
                        UI::showScreen(Screen::NOW_PLAYING);
                        currentScreen = Screen::NOW_PLAYING;
                    } else if (currentScreen == Screen::SETTINGS) {
                        UI::showScreen(Screen::SONG_LIST);
                        currentScreen = Screen::SONG_LIST;
                    }
                    break;

                case InputEvent::BTN_NEXT:
                    if (currentScreen == Screen::NOW_PLAYING) {
                        Audio::next();
                    } else {
                        // Navigate screens: Song List → Settings → Now Playing
                        if (currentScreen == Screen::SONG_LIST) {
                            UI::showScreen(Screen::SETTINGS);
                            currentScreen = Screen::SETTINGS;
                        } else if (currentScreen == Screen::SETTINGS) {
                            UI::showScreen(Screen::NOW_PLAYING);
                            currentScreen = Screen::NOW_PLAYING;
                        } else {
                            UI::showScreen(Screen::SONG_LIST);
                            currentScreen = Screen::SONG_LIST;
                        }
                    }
                    break;

                case InputEvent::BTN_PREV:
                    if (currentScreen == Screen::NOW_PLAYING) {
                        Audio::prev();
                    } else {
                        // Navigate screens backwards
                        if (currentScreen == Screen::SETTINGS) {
                            UI::showScreen(Screen::SONG_LIST);
                            currentScreen = Screen::SONG_LIST;
                        } else if (currentScreen == Screen::SONG_LIST) {
                            UI::showScreen(Screen::NOW_PLAYING);
                            currentScreen = Screen::NOW_PLAYING;
                        } else {
                            UI::showScreen(Screen::SETTINGS);
                            currentScreen = Screen::SETTINGS;
                        }
                    }
                    break;

                case InputEvent::ENC_CW:
                    Audio::setVolume(Audio::getVolume() + 1);
                    break;

                case InputEvent::ENC_CCW:
                    Audio::setVolume(Audio::getVolume() - 1);
                    break;

                case InputEvent::ENC_PRESS:
                    Audio::cyclePlayMode();
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
    Serial.println("  Pseudo Vinyl MP3 Player v1.0");
    Serial.println("═══════════════════════════════════\n");

    // 1. Display
    Display::init();
    UI::init();
    Serial.println("[Boot] Display ready");

    // 2. SD Card
    if (Storage::init()) {
        songs = Storage::scanMusic("/");
        Audio::setPlaylist(songs);
        UI::setSongList(songs);
        Serial.printf("[Boot] %d songs loaded\n", songs.size());
    } else {
        Serial.println("[Boot] WARNING: No SD card!");
    }

    // 3. Input
    Input::init();

    // 4. Spawn FreeRTOS tasks
    xTaskCreatePinnedToCore(audioTask, "audio", 8192, nullptr, 3, &audioTaskHandle, 0);
    xTaskCreatePinnedToCore(uiTask,    "ui",    8192, nullptr, 1, &uiTaskHandle,    1);
    xTaskCreatePinnedToCore(inputTask, "input", 4096, nullptr, 2, &inputTaskHandle, 1);

    Serial.println("[Boot] All tasks launched\n");
}

void loop() {
    // All work is done in FreeRTOS tasks
    vTaskDelay(pdMS_TO_TICKS(1000));
}
