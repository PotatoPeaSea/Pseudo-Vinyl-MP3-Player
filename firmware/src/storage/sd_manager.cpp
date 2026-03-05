#include "sd_manager.h"
#include "../config.h"
#include <SPI.h>
#include <SD.h>

static SPIClass sdSPI(HSPI);
static bool mounted = false;

// ── Public API ──────────────────────────────────────────────

bool Storage::init() {
    sdSPI.begin(PIN_SD_SCLK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);

    if (!SD.begin(PIN_SD_CS, sdSPI, 25000000)) {  // 25 MHz
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

                    // Parse title from filename (strip path and extension)
                    int lastSlash = info.filepath.lastIndexOf('/');
                    String basename = info.filepath.substring(lastSlash + 1);
                    info.title = basename.substring(0, basename.length() - 4);
                    info.artist = "Unknown";

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

    // Sort alphabetically by title
    std::sort(songs.begin(), songs.end(), [](const SongInfo &a, const SongInfo &b) {
        return a.title < b.title;
    });

    Serial.printf("[SD] Found %d MP3 files\n", songs.size());
    return songs;
}

uint8_t* Storage::loadArtFile(const String &mp3Path, size_t &outSize) {
    String artPath = mp3Path.substring(0, mp3Path.length() - 4) + ".art";

    File f = SD.open(artPath, FILE_READ);
    if (!f) {
        outSize = 0;
        return nullptr;
    }

    outSize = f.size();
    // Allocate in PSRAM if available
    uint8_t *buf = (uint8_t *)heap_caps_malloc(outSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        buf = (uint8_t *)malloc(outSize);
    }

    if (buf) {
        f.read(buf, outSize);
    } else {
        outSize = 0;
    }

    f.close();
    return buf;
}
