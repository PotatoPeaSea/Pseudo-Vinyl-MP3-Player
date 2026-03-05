#pragma once
#include <Arduino.h>

/**
 * UI Manager — LVGL screen navigation and updates
 *
 * Screens:
 *   - Now Playing (vinyl spin + track info)
 *   - Song List (scrollable)
 *   - Settings (with BT placeholder)
 */

enum class Screen : uint8_t {
    NOW_PLAYING = 0,
    SONG_LIST,
    SETTINGS,
};

namespace UI {
    /// Build all LVGL screens (call after Display::init)
    void init();

    /// Switch to a screen
    void showScreen(Screen screen);

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
}
