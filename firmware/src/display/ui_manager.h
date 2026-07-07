#pragma once
#include <Arduino.h>
#include "../storage/sd_manager.h"
#include "../audio/audio_manager.h"
#include "../bluetooth/bt_manager.h"

/**
 * UI Manager — LVGL screen navigation and updates
 *
 * Screens:
 *   - Now Playing (vinyl spin + track info)
 *   - Song List (scrollable, encoder-navigable)
 *   - Settings (output toggle)
 *   - Bluetooth (device scan/pick)
 *
 * Navigation: a virtual LVGL keypad — the input task forwards encoder
 * turns as LV_KEY_NEXT/PREV and the select button as LV_KEY_ENTER via
 * sendKey().
 */

enum class Screen : uint8_t {
    NOW_PLAYING = 0,
    SONG_LIST,
    SETTINGS,
    BLUETOOTH,
};

namespace UI {
    /// Build all LVGL screens (call after Display::init)
    void init();

    /// Switch to a screen
    void showScreen(Screen screen);

    /// Screen currently shown (kept in sync even for internal navigation)
    Screen activeScreen();

    /// Queue a key for the LVGL keypad indev (LV_KEY_NEXT/PREV/ENTER)
    void sendKey(uint32_t lvKey);

    /// Update dynamic content (vinyl spin, progress, etc.)
    /// Call every frame from the UI task
    void update();

    /// Song list: populate with scanned songs
    void setSongList(const std::vector<SongInfo> &songs);

    /// Now Playing: update with current song info
    void setNowPlaying(const SongInfo *song, bool playing);

    /// Now Playing: update progress
    void setProgress(uint32_t currentSec, uint32_t totalSec);

    /// Update volume indicator
    void setVolume(int vol, int maxVol);

    /// Update play mode indicator
    void setPlayModeIndicator(PlayMode mode);

    /// Bluetooth screen: status line + discovered device list
    void setBtStatus(const String &status);
    void setBtDevices(const std::vector<BtDevice> &devices);

    /// Refresh the Settings output-mode label
    void setOutputModeLabel(OutputMode mode);
}
