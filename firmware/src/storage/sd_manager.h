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

// A playlist is just a folder: any top-level directory under the SD root.
// "All Songs" (root path "/") is always entry 0, synthesized by
// scanPlaylists — not a real directory — so there's always something to
// select even on a card with no subfolders.
struct PlaylistInfo {
    String name;    // Display name — folder name, or "All Songs" for root
    String path;    // Root path to scan for this playlist's songs
};

namespace Storage {
    /// Initialize SD card on HSPI
    bool init();

    /// Check if SD card is mounted
    bool isMounted();

    /// Scan for MP3 files, returns sorted list. Recurses into subfolders,
    /// so calling this on a playlist folder picks up songs in nested
    /// subfolders of that playlist too.
    std::vector<SongInfo> scanMusic(const char *rootPath = "/");

    /// List playlists: "All Songs" (root) plus one entry per top-level
    /// folder under rootPath, capped at MAX_PLAYLISTS. Cheap — only reads
    /// directory names, not song contents.
    std::vector<PlaylistInfo> scanPlaylists(const char *rootPath = "/");

    /// Display title for a song: filename without path or extension
    String songTitle(const SongInfo &song);

    /// Load an .art file into a buffer (caller must free with free())
    /// Returns nullptr if file not found; sets outSize to byte count
    uint8_t* loadArtFile(const String &mp3Path, size_t &outSize);
}
