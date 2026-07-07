#pragma once
#include <Arduino.h>
#include "../storage/sd_manager.h"

/**
 * Audio Manager — MP3 decode + switchable output
 *
 * Decode: helix MP3 (arduino-audio-tools) — runs from internal SRAM,
 *         no PSRAM needed (ESP32-audioI2S 3.x required PSRAM).
 * Output: Bluetooth A2DP (default) via BtMgr ring buffer,
 *         or wired I2S to the PCM5102 DAC.
 */

enum class PlayMode : uint8_t {
    NORMAL = 0,
    SHUFFLE,
    REPEAT_ALL,
    REPEAT_ONE,
};

enum class OutputMode : uint8_t {
    BLUETOOTH = 0,
    WIRED,
};

namespace AudioMgr {
    /// Initialize pipeline + restore output mode from NVS
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

    /// Output routing (persisted to NVS)
    void setOutputMode(OutputMode mode);
    OutputMode getOutputMode();

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
    uint32_t durationSec();     // Estimated duration (exact for CBR)
}
