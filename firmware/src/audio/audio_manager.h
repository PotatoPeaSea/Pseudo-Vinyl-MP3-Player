#pragma once
#include <Arduino.h>
#include "../storage/sd_manager.h"

/**
 * Audio Manager — MP3 decoding + I2S output
 *
 * Uses ESP32-audioI2S library (schreibfaul1) for decode + playback.
 * Architecture: audio output goes through an abstract interface
 * so Bluetooth A2DP can be swapped in later.
 */

enum class PlayMode : uint8_t {
    NORMAL = 0,
    SHUFFLE,
    REPEAT_ALL,
    REPEAT_ONE,
};

namespace AudioMgr {
    /// Initialize I2S output to PCM5102 DAC
    void init();

    /// Must be called frequently from the audio task to feed the decoder
    void loop();

    /// Load a playlist and optionally start playback
    void setPlaylist(const std::vector<SongInfo> &songs);

    /// Playback controls
    void play(int index);       // Play song at index
    void pause();
    void resume();
    void stop();
    void next();
    void prev();

    /// Volume (0-21)
    void setVolume(int vol);
    int  getVolume();

    /// Playback mode
    void setPlayMode(PlayMode mode);
    PlayMode getPlayMode();
    void cyclePlayMode();       // Normal -> Shuffle -> Repeat All -> Repeat One

    /// Status queries
    bool isPlaying();
    int  currentIndex();
    const SongInfo* currentSong();
    uint32_t positionSec();     // Current position in seconds
    uint32_t durationSec();     // Total duration in seconds
}
