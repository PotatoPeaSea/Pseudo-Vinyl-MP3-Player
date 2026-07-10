#include "audio_manager.h"
#include "../config.h"
#include "../bluetooth/bt_manager.h"
#include <SPI.h>
#include <SD.h>
#include <Preferences.h>
#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"

// ── Output plumbing ─────────────────────────────────────────
//
//   SD File → StreamCopy → EncodedAudioStream(helix) → meter →
//     VolumeStream → BtMgr ring buffer (A2DP)

// Forwards PCM into the Bluetooth ring buffer
class BtPrint : public Print {
public:
    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t *data, size_t len) override {
        return BtMgr::writeAudio(data, len);
    }
};

// Counts decoded PCM bytes for the position/duration estimate
class MeterPrint : public Print {
public:
    Print *out = nullptr;
    volatile uint64_t bytes = 0;
    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t *data, size_t len) override {
        if (!out) return len;
        size_t written = out->write(data, len);
        bytes += written;
        return written;
    }
};

// ── Static state ────────────────────────────────────────────
static BtPrint btOut;
static audio_tools::VolumeStream volumeOut;
static MeterPrint meter;
static audio_tools::MP3DecoderHelix helix;
static audio_tools::EncodedAudioStream decoder(&meter, &helix);
static audio_tools::StreamCopy copier;
static File curFile;
static size_t curFileSize = 0;

static Preferences audioPrefs;

static std::vector<SongInfo> playlist;
static int currentIdx = -1;
static bool playing = false;
static PlayMode playMode = PlayMode::NORMAL;
static int volume = DEFAULT_VOLUME;

static audio_tools::AudioInfo lastInfo(44100, 2, 16);

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

// ── Pipeline helpers ────────────────────────────────────────

static void applyVolume() {
    // Perceptual-ish curve: square of the linear knob position
    float f = (float)volume / MAX_VOLUME;
    volumeOut.setVolume(f * f);
}

static void applyOutputRouting() {
    volumeOut.setOutput(btOut);
    BtMgr::start();
    Serial.println("[Audio] Output: Bluetooth A2DP");
}

static void closePipeline() {
    playing = false;
    if (curFile) curFile.close();
}

// ── Public API ──────────────────────────────────────────────

void AudioMgr::init() {
    audioPrefs.begin("audiocfg", false);
    volume = audioPrefs.getUChar("volume", DEFAULT_VOLUME);

    auto vcfg = volumeOut.defaultConfig();
    vcfg.copyFrom(lastInfo);
    volumeOut.begin(vcfg);
    applyVolume();

    applyOutputRouting();
    Serial.println("[Audio] Pipeline initialized (helix MP3)");
}

void AudioMgr::loop() {
    if (!playing) return;

    // With no BT sink connected, hold playback instead of racing through
    // the file (BT writes would be dropped)
    if (!BtMgr::isConnected()) {
        vTaskDelay(pdMS_TO_TICKS(50));
        return;
    }

    // Propagate decoder format changes (sample rate etc.) downstream.
    // Note: A2DP is fixed 44.1kHz — non-44.1k files play off-speed on BT.
    audio_tools::AudioInfo info = helix.audioInfo();
    if (info.sample_rate != 0 && info != lastInfo) {
        lastInfo = info;
        volumeOut.setAudioInfo(info);
        Serial.printf("[Audio] Format: %d Hz, %d ch\n", info.sample_rate, info.channels);
    }

    size_t copied = copier.copy();
    if (copied == 0 && curFile && curFile.available() == 0) {
        // Track finished — auto-advance
        int nextIdx = getNextIndex();
        if (nextIdx >= 0) {
            play(nextIdx);
        } else {
            closePipeline();
            currentIdx = -1;
            Serial.println("[Audio] Playlist finished");
        }
    }
}

void AudioMgr::setPlaylist(const std::vector<SongInfo> &songs) {
    playlist = songs;
    currentIdx = -1;
    playing = false;
    if (playMode == PlayMode::SHUFFLE) {
        buildShuffleOrder();
    }
    Serial.printf("[Audio] Playlist loaded (%d songs)\n", playlist.size());
}

void AudioMgr::play(int index) {
    if (index < 0 || index >= (int)playlist.size()) return;

    closePipeline();
    currentIdx = index;
    const SongInfo &song = playlist[currentIdx];

    curFile = SD.open(song.filepath.c_str(), FILE_READ);
    if (!curFile) {
        Serial.printf("[Audio] FAILED to open: %s\n", song.filepath.c_str());
        return;
    }
    curFileSize = curFile.size();
    meter.bytes = 0;

    decoder.begin();
    copier.begin(decoder, curFile);
    playing = true;
    Serial.printf("[Audio] Playing [%d]: %s\n", currentIdx, song.title.c_str());
}

void AudioMgr::pause() {
    playing = false;
    Serial.println("[Audio] Paused");
}

void AudioMgr::resume() {
    if (curFile) {
        playing = true;
        Serial.println("[Audio] Resumed");
    }
}

void AudioMgr::stop() {
    closePipeline();
    Serial.println("[Audio] Stopped");
}

void AudioMgr::next() {
    int n = getNextIndex();
    if (n >= 0) play(n);
}

void AudioMgr::prev() {
    // If more than 3 seconds in, restart current song
    if (positionSec() > 3) {
        play(currentIdx);
    } else {
        int p = getPrevIndex();
        if (p >= 0) play(p);
    }
}

void AudioMgr::setVolume(int vol) {
    volume = constrain(vol, 0, MAX_VOLUME);
    audioPrefs.putUChar("volume", volume);
    applyVolume();
}

int AudioMgr::getVolume() {
    return volume;
}

void AudioMgr::setPlayMode(PlayMode mode) {
    playMode = mode;
    if (mode == PlayMode::SHUFFLE) buildShuffleOrder();
    Serial.printf("[Audio] Play mode: %d\n", (int)mode);
}

PlayMode AudioMgr::getPlayMode() {
    return playMode;
}

void AudioMgr::cyclePlayMode() {
    int m = ((int)playMode + 1) % 4;
    setPlayMode((PlayMode)m);
}

bool AudioMgr::isPlaying() {
    return playing;
}

int AudioMgr::currentIndex() {
    return currentIdx;
}

const SongInfo* AudioMgr::currentSong() {
    if (currentIdx >= 0 && currentIdx < (int)playlist.size()) {
        return &playlist[currentIdx];
    }
    return nullptr;
}

uint32_t AudioMgr::positionSec() {
    uint32_t byteRate = lastInfo.sample_rate * lastInfo.channels * 2;
    if (byteRate == 0) return 0;
    return meter.bytes / byteRate;
}

uint32_t AudioMgr::durationSec() {
    // Scale elapsed time by compressed bytes consumed vs. file size —
    // exact for CBR, converges quickly for VBR
    if (!curFile || curFileSize == 0) return 0;
    size_t consumed = curFile.position();
    if (consumed == 0) return 0;
    return (uint64_t)positionSec() * curFileSize / consumed;
}
