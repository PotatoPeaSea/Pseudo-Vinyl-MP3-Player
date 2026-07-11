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

// Not owned — points at the app-lifetime library in main.cpp (no RAM copy)
static const std::vector<SongInfo> *playlist = nullptr;
static volatile int currentIdx = -1;
static volatile bool playing = false;
static bool decoderOpen = false;   // helix buffers (~25KB) currently allocated

// Deferred commands: play()/stop() may be called from the input or UI task,
// but opening the file and (re)initializing the helix decoder is ~25KB of
// allocations with a deep call stack — too heavy for the 3KB input task and
// it must not race the decode loop. Public calls only record the request;
// AudioMgr::loop() (audio task) executes it.
static volatile int pendingPlayIdx = -1;
static volatile bool pendingStop = false;
static PlayMode playMode = PlayMode::NORMAL;
static int volume = DEFAULT_VOLUME;

static audio_tools::AudioInfo lastInfo(44100, 2, 16);

static size_t playlistSize() {
    return playlist ? playlist->size() : 0;
}

// Shuffle order (uint8_t — MAX_SONGS is well under 256)
static std::vector<uint8_t> shuffleOrder;
static int shufflePos = 0;

static void buildShuffleOrder() {
    shuffleOrder.resize(playlistSize());
    for (size_t i = 0; i < shuffleOrder.size(); i++) shuffleOrder[i] = i;
    // Fisher-Yates shuffle
    for (int i = shuffleOrder.size() - 1; i > 0; i--) {
        int j = random(0, i + 1);
        std::swap(shuffleOrder[i], shuffleOrder[j]);
    }
    shufflePos = 0;
}

static int getNextIndex() {
    if (playlistSize() == 0) return -1;

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
            return (currentIdx + 1) % playlistSize();
        case PlayMode::NORMAL:
        default:
            if (currentIdx + 1 < (int)playlistSize()) {
                return currentIdx + 1;
            }
            return -1;  // End of playlist
    }
}

static int getPrevIndex() {
    if (playlistSize() == 0) return -1;
    if (currentIdx <= 0) return playlistSize() - 1;
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

// Free the helix decoder buffers (~25KB, split across several allocations)
// when playback fully stops. play() re-allocates via decoder.begin() —
// slower to start again, but the RAM is available while idle.
static void releaseDecoder() {
    if (!decoderOpen) return;
    decoder.end();
    decoderOpen = false;
    Serial.printf("[Audio] Decoder released (free=%u largest=%u)\n",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
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

// Actually start a track — runs on the audio task only (see doPlay note on
// the pending* variables above)
static void doPlay(int index) {
    if (index < 0 || index >= (int)playlistSize()) return;

    closePipeline();
    currentIdx = index;
    const SongInfo &song = (*playlist)[currentIdx];

    curFile = SD.open(song.filepath.c_str(), FILE_READ);
    if (!curFile) {
        Serial.printf("[Audio] FAILED to open: %s\n", song.filepath.c_str());
        return;
    }
    curFileSize = curFile.size();
    meter.bytes = 0;

    decoder.begin();
    decoderOpen = true;
    copier.begin(decoder, curFile);
    playing = true;
    Serial.printf("[Audio] Playing [%d]: %s (free=%u largest=%u)\n",
                  (int)currentIdx, Storage::songTitle(song).c_str(),
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}

void AudioMgr::loop() {
    // Execute deferred commands from other tasks
    if (pendingStop) {
        pendingStop = false;
        pendingPlayIdx = -1;
        closePipeline();
        releaseDecoder();
        currentIdx = -1;
        Serial.println("[Audio] Stopped");
    }
    int req = pendingPlayIdx;
    if (req >= 0) {
        pendingPlayIdx = -1;
        doPlay(req);
    }

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
        // Track finished — auto-advance (already on the audio task,
        // so start the next track directly)
        int nextIdx = getNextIndex();
        if (nextIdx >= 0) {
            doPlay(nextIdx);
        } else {
            closePipeline();
            releaseDecoder();
            currentIdx = -1;
            Serial.println("[Audio] Playlist finished");
        }
    }
}

void AudioMgr::setPlaylist(const std::vector<SongInfo> *songs) {
    playlist = songs;
    currentIdx = -1;
    playing = false;
    if (playMode == PlayMode::SHUFFLE) {
        buildShuffleOrder();
    }
    Serial.printf("[Audio] Playlist loaded (%d songs)\n", (int)playlistSize());
}

void AudioMgr::play(int index) {
    if (index < 0 || index >= (int)playlistSize()) return;
    pendingPlayIdx = index;   // executed by the audio task in loop()
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
    pendingStop = true;   // executed by the audio task in loop()
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
    if (currentIdx >= 0 && currentIdx < (int)playlistSize()) {
        return &(*playlist)[currentIdx];
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
