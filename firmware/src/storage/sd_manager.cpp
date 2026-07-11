#include "sd_manager.h"
#include "../config.h"
#include <SPI.h>
#include <SD.h>

static SPIClass sdSPI(HSPI);
static bool mounted = false;

// ── Public API ──────────────────────────────────────────────

bool Storage::init() {
    sdSPI.begin(PIN_SD_SCLK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);

    // 20 MHz — MISO is routed through the GPIO matrix (pin 35), keep margin
    if (!SD.begin(PIN_SD_CS, sdSPI, 20000000)) {
        Serial.println("[SD] Mount FAILED");
        mounted = false;
        return false;
    }

    uint64_t totalBytes = SD.totalBytes() / (1024 * 1024);
    uint64_t usedBytes = SD.usedBytes() / (1024 * 1024);
    Serial.printf("[SD] Mounted — %llu MB used / %llu MB total\n", usedBytes, totalBytes);
    mounted = true;
    return true;
}

bool Storage::isMounted() {
    return mounted;
}

// ── Recursive MP3 scanner ───────────────────────────────────

static void scanDir(File dir, std::vector<SongInfo> &songs) {
    while (true) {
        if (songs.size() >= MAX_SONGS) break;   // heap-limited library cap

        File entry = dir.openNextFile();
        if (!entry) break;

        if (entry.isDirectory()) {
            scanDir(entry, songs);
        } else {
            String name = entry.name();
            // Check for .mp3 extension (case-insensitive)
            if (name.length() > 4) {
                String ext = name.substring(name.length() - 4);
                ext.toLowerCase();
                if (ext == ".mp3") {
                    SongInfo info;
                    info.filepath = String(entry.path());

                    // Check for .art file
                    String artPath = info.filepath.substring(0, info.filepath.length() - 4) + ".art";
                    info.hasArt = SD.exists(artPath);

                    songs.push_back(info);
                }
            }
        }
        entry.close();
    }
}

std::vector<SongInfo> Storage::scanMusic(const char *rootPath) {
    std::vector<SongInfo> songs;

    File root = SD.open(rootPath);
    if (!root || !root.isDirectory()) {
        Serial.println("[SD] Failed to open root directory");
        return songs;
    }

    scanDir(root, songs);
    root.close();

    // Sort alphabetically by title (derived per comparison — slower, but
    // avoids storing a title String per song)
    std::sort(songs.begin(), songs.end(), [](const SongInfo &a, const SongInfo &b) {
        return Storage::songTitle(a) < Storage::songTitle(b);
    });

    Serial.printf("[SD] Loaded %d MP3 files (cap %d)\n", songs.size(), MAX_SONGS);
    return songs;
}

String Storage::songTitle(const SongInfo &song) {
    int lastSlash = song.filepath.lastIndexOf('/');
    String basename = song.filepath.substring(lastSlash + 1);
    return basename.substring(0, basename.length() - 4);   // strip ".mp3"
}

uint8_t* Storage::loadArtFile(const String &mp3Path, size_t &outSize) {
    String artPath = mp3Path.substring(0, mp3Path.length() - 4) + ".art";

    File f = SD.open(artPath, FILE_READ);
    if (!f) {
        outSize = 0;
        return nullptr;
    }

    outSize = f.size();
    // No PSRAM on WROOM-32 — refuse art that won't fit next to the BT stack
    if (outSize == 0 || outSize > ART_MAX_BYTES) {
        Serial.printf("[SD] Art too large (%u bytes, max %u): %s\n",
                      (unsigned)outSize, (unsigned)ART_MAX_BYTES, artPath.c_str());
        f.close();
        outSize = 0;
        return nullptr;
    }
    // Skip art when the heap is tight rather than squeeze it in: a missing
    // cover degrades gracefully, but leaving the UI/decoder without working
    // memory crashes (LVGL does not check its own allocations)
    if (ESP.getMaxAllocHeap() < outSize + 12 * 1024) {
        Serial.printf("[SD] Skipping art, heap too tight (largest=%u need=%u+12K): %s\n",
                      ESP.getMaxAllocHeap(), (unsigned)outSize, artPath.c_str());
        f.close();
        outSize = 0;
        return nullptr;
    }
    uint8_t *buf = (uint8_t *)malloc(outSize);

    if (buf) {
        f.read(buf, outSize);
    } else {
        outSize = 0;
    }

    f.close();
    return buf;
}
