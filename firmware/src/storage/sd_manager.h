#pragma once
#include <Arduino.h>
#include <vector>

/**
 * SD Card Manager — File system operations
 */

// Kept minimal to save RAM: title is derived from the filepath on demand
// (Storage::songTitle) instead of being stored per song, and artist was
// dropped entirely (no ID3 parsing — it was always "Unknown").
struct SongInfo {
    String filepath;    // Full path on SD card, e.g. "/Music/song.mp3"
    bool hasArt;        // True if matching .art file exists
};

namespace Storage {
    /// Initialize SD card on HSPI
    bool init();

    /// Check if SD card is mounted
    bool isMounted();

    /// Scan for MP3 files, returns sorted list
    std::vector<SongInfo> scanMusic(const char *rootPath = "/");

    /// Display title for a song: filename without path or extension
    String songTitle(const SongInfo &song);

    /// Load an .art file into a buffer (caller must free with free())
    /// Returns nullptr if file not found; sets outSize to byte count
    uint8_t* loadArtFile(const String &mp3Path, size_t &outSize);
}
