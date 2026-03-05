#pragma once
#include <Arduino.h>
#include <vector>

/**
 * SD Card Manager — File system operations
 */

struct SongInfo {
    String filepath;    // Full path on SD card, e.g. "/Music/song.mp3"
    String title;       // From ID3 tag, or filename
    String artist;      // From ID3 tag, or "Unknown"
    bool hasArt;        // True if matching .art file exists
};

namespace Storage {
    /// Initialize SD card on SPI3
    bool init();

    /// Check if SD card is mounted
    bool isMounted();

    /// Scan for MP3 files, returns sorted list
    std::vector<SongInfo> scanMusic(const char *rootPath = "/");

    /// Load an .art file into a buffer (caller must free with free())
    /// Returns nullptr if file not found; sets outSize to byte count
    uint8_t* loadArtFile(const String &mp3Path, size_t &outSize);
}
