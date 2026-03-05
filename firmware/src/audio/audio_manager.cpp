#include "audio_manager.h"
#include "../config.h"
#include <Audio.h>   // ESP32-audioI2S by schreibfaul1

// ── Static state ────────────────────────────────────────────
static Audio audio;
static std::vector<SongInfo> playlist;
static int currentIdx = -1;
static bool playing = false;
static PlayMode playMode = PlayMode::NORMAL;
static int volume = DEFAULT_VOLUME;

// Shuffle order
static std::vector<int> shuffleOrder;
static int shufflePos = 0;

static void buildShuffleOrder() {
    shuffleOrder.resize(playlist.size());
    for (size_t i = 0; i < playlist.size(); i++) shuffleOrder[i] = i;
    // Fisher-Yates shuffle
    for (int i = shuffleOrder.size() - 1; i > 0; i--) {
        int j = random(0, i + 1);
        std::swap(shuffleOrder[i], shuffleOrder[j]);
    }
    shufflePos = 0;
}

static int getNextIndex() {
    if (playlist.empty()) return -1;

    switch (playMode) {
        case PlayMode::REPEAT_ONE:
            return currentIdx;
        case PlayMode::SHUFFLE:
            shufflePos++;
            if (shufflePos >= (int)shuffleOrder.size()) {
                buildShuffleOrder();
            }
            return shuffleOrder[shufflePos];
        case PlayMode::REPEAT_ALL:
            return (currentIdx + 1) % playlist.size();
        case PlayMode::NORMAL:
        default:
            if (currentIdx + 1 < (int)playlist.size()) {
                return currentIdx + 1;
            }
            return -1;  // End of playlist
    }
}

static int getPrevIndex() {
    if (playlist.empty()) return -1;
    if (currentIdx <= 0) return playlist.size() - 1;
    return currentIdx - 1;
}

// ── Public API ──────────────────────────────────────────────

void Audio::init() {
    audio.setPinout(PIN_I2S_BCLK, PIN_I2S_LRCK, PIN_I2S_DOUT);
    audio.setVolume(volume);
    Serial.println("[Audio] I2S initialized (PCM5102)");
}

void Audio::loop() {
    audio.loop();

    // Auto-advance when song ends
    if (playing && !audio.isRunning()) {
        int next = getNextIndex();
        if (next >= 0) {
            play(next);
        } else {
            playing = false;
            currentIdx = -1;
            Serial.println("[Audio] Playlist finished");
        }
    }
}

void Audio::setPlaylist(const std::vector<SongInfo> &songs) {
    playlist = songs;
    currentIdx = -1;
    playing = false;
    if (playMode == PlayMode::SHUFFLE) {
        buildShuffleOrder();
    }
    Serial.printf("[Audio] Playlist loaded (%d songs)\n", playlist.size());
}

void Audio::play(int index) {
    if (index < 0 || index >= (int)playlist.size()) return;

    currentIdx = index;
    const SongInfo &song = playlist[currentIdx];

    // ESP32-audioI2S plays directly from SD
    bool ok = audio.connecttoFS(SD, song.filepath.c_str());
    if (ok) {
        playing = true;
        Serial.printf("[Audio] Playing [%d]: %s\n", currentIdx, song.title.c_str());
    } else {
        Serial.printf("[Audio] FAILED to open: %s\n", song.filepath.c_str());
        playing = false;
    }
}

void Audio::pause() {
    audio.pauseResume();
    playing = false;
    Serial.println("[Audio] Paused");
}

void Audio::resume() {
    audio.pauseResume();
    playing = true;
    Serial.println("[Audio] Resumed");
}

void Audio::stop() {
    audio.stopSong();
    playing = false;
    Serial.println("[Audio] Stopped");
}

void Audio::next() {
    int n = getNextIndex();
    if (n >= 0) play(n);
}

void Audio::prev() {
    // If more than 3 seconds in, restart current song
    if (audio.getAudioCurrentTime() > 3) {
        play(currentIdx);
    } else {
        int p = getPrevIndex();
        if (p >= 0) play(p);
    }
}

void Audio::setVolume(int vol) {
    volume = constrain(vol, 0, MAX_VOLUME);
    audio.setVolume(volume);
}

int Audio::getVolume() {
    return volume;
}

void Audio::setPlayMode(PlayMode mode) {
    playMode = mode;
    if (mode == PlayMode::SHUFFLE) buildShuffleOrder();
    Serial.printf("[Audio] Play mode: %d\n", (int)mode);
}

PlayMode Audio::getPlayMode() {
    return playMode;
}

void Audio::cyclePlayMode() {
    int m = ((int)playMode + 1) % 4;
    setPlayMode((PlayMode)m);
}

bool Audio::isPlaying() {
    return playing;
}

int Audio::currentIndex() {
    return currentIdx;
}

const SongInfo* Audio::currentSong() {
    if (currentIdx >= 0 && currentIdx < (int)playlist.size()) {
        return &playlist[currentIdx];
    }
    return nullptr;
}

uint32_t Audio::positionSec() {
    return audio.getAudioCurrentTime();
}

uint32_t Audio::durationSec() {
    return audio.getAudioFileDuration();
}
