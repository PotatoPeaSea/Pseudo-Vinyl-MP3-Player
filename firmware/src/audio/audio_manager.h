#pragma once
#include <Arduino.h>
#include "../storage/sd_manager.h"

/**
 * Audio Manager — MP3 decode + Bluetooth A2DP output
 *
 * Decode: helix MP3 (arduino-audio-tools) — runs from internal SRAM,
 *         no PSRAM needed (ESP32-audioI2S 3.x required PSRAM).
 * Output: Bluetooth A2DP only, via the BtMgr ring buffer. (Wired I2S was
 *         removed — no heap headroom for it next to the BT stack.)
 */

enum class PlayMode : uint8_t {
    NORMAL = 0,
    SHUFFLE,
    REPEAT_ALL,
    REPEAT_ONE,
};

namespace AudioMgr {
    /// Initialize pipeline + restore output mode from NVS
    void init();

    /// Must be called frequently from the audio task to feed the decoder
    void loop();

    /// Point playback at the song library. NOT copied — the caller keeps
    /// ownership and the vector must outlive playback (it's the single
    /// app-lifetime library in main.cpp; avoids a full duplicate in RAM).
    void setPlaylist(const std::vector<SongInfo> *songs);

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

    /// Diagnostic: toggle a generated 440Hz sine streamed straight to the
    /// BT ring buffer — bypasses SD + MP3 decode. Isolates whether stutter
    /// comes from the radio/sink (tone stutters too) or the SD/decode path
    /// (tone is clean). Stops any current playback when enabled.
    void toggleToneTest();

    /// Status queries
    bool isPlaying();
    int  currentIndex();
    const SongInfo* currentSong();
    uint32_t positionSec();     // Current position in seconds
    uint32_t durationSec();     // Estimated duration (exact for CBR)
}
