#include "sd_manager.h"
#include "../config.h"
#include <SPI.h>
#include <SD.h>
#include <esp_heap_caps.h>  // heap_caps_get_largest_free_block (PSRAM headroom check)

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

// Folders SD cards commonly grow on their own — never worth listing as a
// playlist.
static bool isJunkFolder(const String &name) {
    return name == "System Volume Information" || name.startsWith(".");
}

std::vector<PlaylistInfo> Storage::scanPlaylists(const char *rootPath) {
    std::vector<PlaylistInfo> lists;

    PlaylistInfo all;
    all.name = "All Songs";
    all.path = rootPath;
    lists.push_back(all);

    File root = SD.open(rootPath);
    if (!root || !root.isDirectory()) {
        Serial.println("[SD] scanPlaylists: failed to open root directory");
        return lists;
    }

    while (lists.size() < MAX_PLAYLISTS) {
        File entry = root.openNextFile();
        if (!entry) break;

        if (entry.isDirectory()) {
            String name = String(entry.name());
            if (!isJunkFolder(name)) {
                PlaylistInfo pl;
                pl.name = name;
                pl.path = String(entry.path());
                lists.push_back(pl);
            }
        }
        entry.close();
    }
    root.close();

    // Keep "All Songs" first, sort the folder playlists alphabetically
    std::sort(lists.begin() + 1, lists.end(), [](const PlaylistInfo &a, const PlaylistInfo &b) {
        return a.name < b.name;
    });

    Serial.printf("[SD] Found %d playlist(s) (cap %d)\n", lists.size(), MAX_PLAYLISTS);
    return lists;
}

uint8_t* Storage::loadArtFile(const String &mp3Path, size_t &outSize) {
    String artPath = mp3Path.substring(0, mp3Path.length() - 4) + ".art";

    File f = SD.open(artPath, FILE_READ);
    if (!f) {
        Serial.printf("[SD] No art file at %s\n", artPath.c_str());
        outSize = 0;
        return nullptr;
    }

    outSize = f.size();
    // Refuse art larger than the display can use (see ART_MAX_SIDE) — bounds
    // the buffer regardless of which heap it lands in
    if (outSize == 0 || outSize > ART_MAX_BYTES) {
        Serial.printf("[SD] Art too large (%u bytes, max %u): %s\n",
                      (unsigned)outSize, (unsigned)ART_MAX_BYTES, artPath.c_str());
        f.close();
        outSize = 0;
        return nullptr;
    }
    // Skip art when memory is tight rather than squeeze it in: a missing
    // cover degrades gracefully, but leaving the UI/decoder without working
    // memory crashes (LVGL does not check its own allocations). Art buffers
    // are always well over the 4KB CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL
    // threshold (min is 90x90x2=16.2KB), so malloc() below lands in PSRAM —
    // the guard must check PSRAM headroom, not internal SRAM. Checking
    // ESP.getMaxAllocHeap() (internal-only) here was a leftover from the
    // no-PSRAM WROOM-32 and would reject every art load on this board: the
    // internal heap sits around ~80-90KB after BT+SD, which is below what a
    // 240x240 (112.5KB) art buffer needs even though PSRAM has ~4MB free.
    size_t psramLargest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    if (psramLargest < outSize + 8 * 1024) {
        Serial.printf("[SD] Skipping art, PSRAM too tight (largest=%u need=%u+8K): %s\n",
                      (unsigned)psramLargest, (unsigned)outSize, artPath.c_str());
        f.close();
        outSize = 0;
        return nullptr;
    }
    uint8_t *buf = (uint8_t *)malloc(outSize);

    if (buf) {
        f.read(buf, outSize);
    } else {
        Serial.printf("[SD] malloc(%u) failed loading %s (psram largest=%u)\n",
                      (unsigned)outSize, artPath.c_str(), (unsigned)psramLargest);
        outSize = 0;
    }

    f.close();
    return buf;
}
