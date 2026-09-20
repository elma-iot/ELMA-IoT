#ifndef APP_DISABLE_AUDIO
#include "device_log.h"
#include "audio_player.h"

#include <Audio.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <esp32-hal-cpu.h>
#include <driver/gpio.h>
#include <memory>
#include <driver/i2s.h>
#include "logic_audio_dsp.h"

#include "default_config.h"
#include "playback_text.h"
#include "psram_allocator.h"
#include "storage_backend.h"

namespace {
AudioPlayer::Impl* g_impl = nullptr;

constexpr unsigned long kSwitchFadeOutMs = 70UL;
constexpr unsigned long kStartFadeInMs = 90UL;
constexpr unsigned long kSwitchQuietTimeMs = 18UL;
constexpr unsigned long kStreamWarmupMinimumMs = 350UL;
constexpr unsigned long kStreamWarmupMaximumMs = 3500UL;
constexpr size_t kStreamWarmupReadyBytesPsram = 48U * 1024U;
constexpr size_t kStreamWarmupReadyBytesRam = 3U * 1600U;
constexpr uint8_t kMaximumStreamRedirects = 4;
constexpr uint32_t kPreferredDiagnosticSampleRateHz = DefaultConfig::AUDIO_DIAGNOSTIC_PREFERRED_SAMPLE_RATE_HZ;

uint8_t percentToLibraryVolume(uint8_t volumePercent) {
    const long scaled = map(volumePercent, 0, 100, 0, DefaultConfig::AUDIO_MAX_HARDWARE_VOLUME);
    return constrain(static_cast<uint8_t>(scaled), static_cast<uint8_t>(0), DefaultConfig::AUDIO_MAX_HARDWARE_VOLUME);
}

String fallbackTitleFromPath(const String& path) {
    if (path.isEmpty()) {
        return "Local file";
    }
    const int slashIndex = path.lastIndexOf('/');
    return slashIndex >= 0 ? path.substring(slashIndex + 1) : path;
}

bool isRedirectStatus(int status) {
    return status == HTTP_CODE_MOVED_PERMANENTLY || status == HTTP_CODE_FOUND ||
        status == HTTP_CODE_SEE_OTHER || status == HTTP_CODE_TEMPORARY_REDIRECT ||
        status == HTTP_CODE_PERMANENT_REDIRECT;
}

void requestAudioPerformanceClock() {
    const uint32_t currentMhz = ESP.getCpuFreqMHz();
    if (currentMhz >= 240U) return;
    if (setCpuFrequencyMhz(240U)) {
        DebugLog.printf("[power] cpu frequency set to %u MHz reason=audio-demand\n",
                        static_cast<unsigned>(ESP.getCpuFreqMHz()));
    } else {
        DebugLog.println("[power] unable to raise cpu frequency for audio demand");
    }
}

String resolveStreamRedirects(const String& requestedUrl) {
    String current = requestedUrl;
    for (uint8_t redirect = 0; redirect < kMaximumStreamRedirects; ++redirect) {
        HTTPClient http;
        http.setConnectTimeout(5000);
        http.setTimeout(5000);
        http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
        bool begun = false;
        if (current.startsWith("https://")) {
            WiFiClientSecure client;
            client.setInsecure();
            begun = http.begin(client, current);
            if (!begun) return current;
            const int status = http.GET();
            const String location = http.getLocation();
            http.end();
            client.stop();
            if (!isRedirectStatus(status) || !location.startsWith("http")) return current;
            DebugLog.printf("[audio] pre-resolved redirect %s -> %s\n", current.c_str(), location.c_str());
            current = location;
        } else if (current.startsWith("http://")) {
            WiFiClient client;
            begun = http.begin(client, current);
            if (!begun) return current;
            const int status = http.GET();
            const String location = http.getLocation();
            http.end();
            client.stop();
            if (!isRedirectStatus(status) || !location.startsWith("http")) return current;
            DebugLog.printf("[audio] pre-resolved redirect %s -> %s\n", current.c_str(), location.c_str());
            current = location;
        } else {
            break;
        }
        delay(20);
        yield();
    }
    return current;
}

bool parseAudioFileReference(const String& raw, StorageTarget& target, String& path) {
    if (raw.startsWith("sd:/")) target=StorageTarget::Sd;
    else if(raw.startsWith("flash:/")) target=StorageTarget::Flash;
    else return false;
    path=raw.substring(raw.indexOf(':')+1);return path.indexOf('\\')<0 && path.indexOf("..")<0;
}

uint16_t readLe16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
}

uint32_t readLe32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
        (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

int16_t clampI16(int32_t sample) {
    if (sample > 32767) {
        return 32767;
    }
    if (sample < -32768) {
        return -32768;
    }
    return static_cast<int16_t>(sample);
}
}  // namespace

class AudioPlayer::Impl {
  public:
    struct OverlayState {
        int16_t* samples = nullptr;
        uint32_t frameCount = 0;
        uint32_t phaseQ16 = 0;
        uint32_t stepQ16 = 0;
        uint8_t duckPercent = 35;
        uint8_t overlayPercent = 100;
        bool active = false;
        bool finished = false;

        void clear() {
            if (samples != nullptr) {
                heap_caps_free(samples);
                samples = nullptr;
            }
            frameCount = 0;
            phaseQ16 = 0;
            stepQ16 = 0;
            active = false;
            finished = false;
        }
    };

    Audio audio;
    ElmaAudio::PitchShift pitchShift;
    float playbackSpeed = 1;
    uint32_t clockRate = 0;
    int8_t defaultLowDb = 0, defaultPresenceDb = 0, defaultHighDb = 0;
    bool sourceOverride = false;
    bool startingSource = false;
    ElmaAudio::SpeechSynth speech;
    ElmaAudio::PianoSynth piano;
    bool renderingPiano=false;
    File speechFile;
    StorageTarget speechTarget = StorageTarget::Flash;
    String speechPath;
    uint32_t speechBytes = 0;
    bool speechRendering = false;
    AppState* appState = nullptr;
    uint8_t bclkPin = DefaultConfig::I2S_BCLK_PIN;
    uint8_t wsPin = DefaultConfig::I2S_WS_PIN;
    uint8_t doutPin = DefaultConfig::I2S_DOUT_PIN;
    bool outputEnabled = true;
    uint8_t volume = DefaultConfig::DEFAULT_VOLUME_PERCENT;
    uint8_t defaultVolume = DefaultConfig::DEFAULT_VOLUME_PERCENT;
    uint8_t hardwareAudioVolume = 0;
    uint32_t requestedSampleRateHz = kPreferredDiagnosticSampleRateHz;
    uint32_t activeSampleRateHz = 0;
    uint8_t bitsPerSample = 16;
    uint8_t channelCount = DefaultConfig::AUDIO_FORCE_MONO ? 1 : 2;
    bool diagnosticTestMode = false;
    String state = "idle";
    String type = "idle";
    String title = "Idle";
    String url;
    String connectionUrl;
    String source = "none";
    bool storageLeaseActive = false;
    StorageTarget storageTarget = StorageTarget::Flash;
    bool retryPending = false;
    uint8_t retryCount = 0;
    unsigned long retryAt = 0;
    bool stopRequested = false;
    bool playbackCompletionPending = false;
    String completedPlaybackSource;
    uint32_t cachedDurationSeconds = 0;
    bool durationProbePending = false;
    bool streamWarmupPending = false;
    unsigned long streamWarmupStartedAt = 0;
    size_t streamWarmupHighWaterBytes = 0;
    OverlayState overlay;

    void publish() {
        if (appState != nullptr) {
            appState->setPlayback(state, type, title, url, source, volume);
        }
    }

    void markPlaying() {
        state = "playing";
        if (title.isEmpty()) {
            title = PlaybackText::fallbackTitleFromUrl(url);
        }
        publish();
    }

    void setHardwareAudioVolume(uint8_t audioVolume) {
        hardwareAudioVolume = constrain(audioVolume, static_cast<uint8_t>(0), DefaultConfig::AUDIO_MAX_HARDWARE_VOLUME);
        audio.setVolume(hardwareAudioVolume);
    }

    void applyHardwareVolumePercent(uint8_t volumePercent) {
        setHardwareAudioVolume(percentToLibraryVolume(volumePercent));
    }

    void fadeToPercent(uint8_t targetVolumePercent, unsigned long durationMs) {
        const int targetAudioVolume = map(targetVolumePercent, 0, 100, 0, DefaultConfig::AUDIO_MAX_HARDWARE_VOLUME);
        if (hardwareAudioVolume == targetAudioVolume) {
            return;
        }

        const int direction = hardwareAudioVolume < targetAudioVolume ? 1 : -1;
        const unsigned long steps = static_cast<unsigned long>(abs(targetAudioVolume - hardwareAudioVolume));
        const unsigned long stepDelayMs = steps == 0 ? 0 : max<unsigned long>(4UL, durationMs / steps);

        while (hardwareAudioVolume != targetAudioVolume) {
            setHardwareAudioVolume(static_cast<uint8_t>(hardwareAudioVolume + direction));
            delay(stepDelayMs);
            yield();
        }
    }
};

namespace {
void clearOverlay(AudioPlayer::Impl* impl) {
    if (impl != nullptr) {
        impl->overlay.clear();
    }
}

void recreateAudioEngine(AudioPlayer::Impl* impl) {
    if (impl == nullptr) return;
    impl->audio.~Audio();
    new (&impl->audio) Audio();
    impl->audio.setBufsize(DefaultConfig::AUDIO_BUFFER_SIZE_RAM, DefaultConfig::AUDIO_BUFFER_SIZE_PSRAM);
    impl->audio.setI2SCommFMT_LSB(false);
    if (impl->outputEnabled) {
        impl->audio.setPinout(impl->bclkPin, impl->wsPin, impl->doutPin);
    }
    impl->audio.forceMono(DefaultConfig::AUDIO_FORCE_MONO);
    impl->audio.setConnectionTimeout(8000, 8000);
    impl->audio.setTone(impl->defaultLowDb, impl->defaultPresenceDb, impl->defaultHighDb);
    impl->applyHardwareVolumePercent(impl->volume);
    impl->clockRate = 0;
    DebugLog.printf("[audio] decoder and network buffers reset, free heap=%u free psram=%u\n",
                    static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getFreePsram()));
}

void releaseStorageLease(AudioPlayer::Impl* impl) {
    if (impl != nullptr && impl->storageLeaseActive) {
        endStorageRead(impl->storageTarget);
        impl->storageLeaseActive = false;
    }
}

void acquireStorageLease(AudioPlayer::Impl* impl, StorageTarget target) {
    if (impl == nullptr) {
        return;
    }
    releaseStorageLease(impl);
    beginStorageRead(target);
    impl->storageTarget = target;
    impl->storageLeaseActive = true;
}

bool loadWavOverlay(StorageTarget target, const String& path, AudioPlayer::Impl::OverlayState& overlay, uint32_t outputSampleRateHz) {
    fs::FS* fs = getStorageFs(target);
    if (fs == nullptr || !storageMounted(target)) {
        return false;
    }

    beginStorageRead(target);
    File file = storageOpen(target, path, "r");
    if (!file || file.isDirectory()) {
        endStorageRead(target);
        return false;
    }

    if (file.size() < 44) {
        file.close();
        endStorageRead(target);
        return false;
    }

    std::unique_ptr<uint8_t[]> header(new uint8_t[12]);
    if (file.read(header.get(), 12) != 12 || memcmp(header.get(), "RIFF", 4) != 0 || memcmp(header.get() + 8, "WAVE", 4) != 0) {
        file.close();
        endStorageRead(target);
        return false;
    }

    bool fmtFound = false;
    bool dataFound = false;
    uint16_t audioFormat = 0;
    uint16_t channelCount = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
    uint32_t dataOffset = 0;
    uint32_t dataSize = 0;

    while (file.available()) {
        uint8_t chunkHeader[8];
        if (file.read(chunkHeader, 8) != 8) {
            break;
        }
        const uint32_t chunkSize = readLe32(chunkHeader + 4);
        const uint32_t chunkDataPos = file.position();

        if (memcmp(chunkHeader, "fmt ", 4) == 0 && chunkSize >= 16) {
            std::unique_ptr<uint8_t[]> fmtData(new uint8_t[chunkSize]);
            if (file.read(fmtData.get(), chunkSize) != static_cast<int>(chunkSize)) {
                break;
            }
            audioFormat = readLe16(fmtData.get());
            channelCount = readLe16(fmtData.get() + 2);
            sampleRate = readLe32(fmtData.get() + 4);
            bitsPerSample = readLe16(fmtData.get() + 14);
            fmtFound = true;
        } else if (memcmp(chunkHeader, "data", 4) == 0) {
            dataOffset = chunkDataPos;
            dataSize = chunkSize;
            dataFound = true;
            file.seek(chunkDataPos + chunkSize + (chunkSize & 1U));
        } else {
            file.seek(chunkDataPos + chunkSize + (chunkSize & 1U));
        }

        if (fmtFound && dataFound) {
            break;
        }
    }

    if (!fmtFound || !dataFound || audioFormat != 1 || (bitsPerSample != 8 && bitsPerSample != 16) ||
        (channelCount != 1 && channelCount != 2) || sampleRate == 0) {
        file.close();
        endStorageRead(target);
        return false;
    }

    const uint32_t bytesPerFrame = (bitsPerSample / 8U) * channelCount;
    if (bytesPerFrame == 0 || dataSize < bytesPerFrame) {
        file.close();
        endStorageRead(target);
        return false;
    }

    const uint32_t frameCount = dataSize / bytesPerFrame;
    const size_t stereoSampleCount = static_cast<size_t>(frameCount) * 2U;
    const size_t allocBytes = stereoSampleCount * sizeof(int16_t);
    int16_t* samples = static_cast<int16_t*>(heap_caps_malloc(allocBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (samples == nullptr) {
        samples = static_cast<int16_t*>(heap_caps_malloc(allocBytes, MALLOC_CAP_8BIT));
    }
    if (samples == nullptr) {
        file.close();
        endStorageRead(target);
        return false;
    }

    file.seek(dataOffset);
    const size_t rawBytes = static_cast<size_t>(frameCount) * bytesPerFrame;
    std::unique_ptr<uint8_t[]> raw(new uint8_t[rawBytes]);
    if (file.read(raw.get(), rawBytes) != static_cast<int>(rawBytes)) {
        file.close();
        endStorageRead(target);
        heap_caps_free(samples);
        return false;
    }
    file.close();
    endStorageRead(target);

    for (uint32_t frame = 0; frame < frameCount; ++frame) {
        const size_t rawIndex = static_cast<size_t>(frame) * bytesPerFrame;
        int16_t left = 0;
        int16_t right = 0;
        if (bitsPerSample == 16) {
            left = static_cast<int16_t>(readLe16(raw.get() + rawIndex));
            right = channelCount == 2 ? static_cast<int16_t>(readLe16(raw.get() + rawIndex + 2)) : left;
        } else {
            left = static_cast<int16_t>((static_cast<int>(raw[rawIndex]) - 128) << 8);
            right = channelCount == 2 ? static_cast<int16_t>((static_cast<int>(raw[rawIndex + 1]) - 128) << 8) : left;
        }
        samples[frame * 2U] = left;
        samples[frame * 2U + 1U] = right;
    }

    overlay.clear();
    overlay.samples = samples;
    overlay.frameCount = frameCount;
    overlay.phaseQ16 = 0;
    overlay.stepQ16 = static_cast<uint32_t>((static_cast<uint64_t>(sampleRate) << 16) / max<uint32_t>(1U, outputSampleRateHz));
    if (overlay.stepQ16 == 0) {
        overlay.stepQ16 = 1;
    }
    overlay.active = true;
    overlay.finished = false;
    return true;
}
}  // namespace

void audio_info(const char* info) {
    if (info != nullptr) {
        DebugLog.printf("[audio] %s\n", info);
    }
}

void audio_showstation(const char* info) {
    if (g_impl != nullptr && info != nullptr) {
        const String nextTitle = PlaybackText::normalizeTitle(String(info), g_impl->url);
        if (!nextTitle.isEmpty() && g_impl->title != nextTitle) {
            g_impl->title = nextTitle;
            g_impl->publish();
        }
    }
}

void audio_showstreamtitle(const char* info) {
    if (g_impl != nullptr && info != nullptr) {
        const String nextTitle = PlaybackText::normalizeTitle(String(info), g_impl->url);
        if (!nextTitle.isEmpty() && g_impl->title != nextTitle) {
            g_impl->title = nextTitle;
            g_impl->publish();
        }
    }
}

void audio_eof_stream(const char* info) {
    // ESP32-audioI2S can emit the generic stream callback while an SD-backed
    // MP3 is still being initialized. Local files have their own MP3 EOF
    // callback, so never let this transient tear down or retry SD playback.
    if (g_impl != nullptr && !g_impl->storageLeaseActive) {
        if (g_impl->appState != nullptr) {
            g_impl->appState->markPlaybackCompleted(g_impl->url, g_impl->source);
        }
        g_impl->completedPlaybackSource = g_impl->source;
        g_impl->playbackCompletionPending = true;
        g_impl->state = "idle";
        g_impl->publish();
        if (!g_impl->stopRequested && !g_impl->url.isEmpty() && g_impl->retryCount < 3) {
            g_impl->retryPending = true;
            g_impl->retryCount++;
            g_impl->retryAt = millis() + 2000UL * g_impl->retryCount;
        }
    }
    (void)info;
}

void audio_process_i2s(uint32_t* sample, bool* continueI2S) {
    if (g_impl != nullptr && sample != nullptr) *sample = g_impl->pitchShift.process(*sample);
    if (g_impl != nullptr && sample != nullptr && g_impl->overlay.active && g_impl->overlay.samples != nullptr) {
        const uint32_t frameIndex = g_impl->overlay.phaseQ16 >> 16;
        if (frameIndex >= g_impl->overlay.frameCount) {
            g_impl->overlay.active = false;
            g_impl->overlay.finished = true;
        } else {
            const int16_t baseLeft = static_cast<int16_t>((*sample) >> 16);
            const int16_t baseRight = static_cast<int16_t>((*sample) & 0xffff);
            const int16_t overlayLeft = g_impl->overlay.samples[frameIndex * 2U];
            const int16_t overlayRight = g_impl->overlay.samples[frameIndex * 2U + 1U];
            const int32_t mixedLeft =
                (static_cast<int32_t>(baseLeft) * static_cast<int32_t>(g_impl->overlay.duckPercent)) / 100 +
                (static_cast<int32_t>(overlayLeft) * static_cast<int32_t>(g_impl->overlay.overlayPercent)) / 100;
            const int32_t mixedRight =
                (static_cast<int32_t>(baseRight) * static_cast<int32_t>(g_impl->overlay.duckPercent)) / 100 +
                (static_cast<int32_t>(overlayRight) * static_cast<int32_t>(g_impl->overlay.overlayPercent)) / 100;
            *sample = (static_cast<uint32_t>(static_cast<uint16_t>(clampI16(mixedLeft))) << 16) |
                static_cast<uint16_t>(clampI16(mixedRight));
            g_impl->overlay.phaseQ16 += g_impl->overlay.stepQ16;
        }
    }
    if (continueI2S != nullptr) {
        *continueI2S = true;
    }
}

void audio_id3data(const char* info) {
    if (info != nullptr) {
        DebugLog.printf("[audio] id3 %s\n", info);
    }
}

void audio_bitrate(const char* info) {
    if (info != nullptr) {
        DebugLog.printf("[audio] bitrate %s\n", info);
    }
}

void audio_commercial(const char* info) {
    if (info != nullptr) {
        DebugLog.printf("[audio] codec %s\n", info);
    }
}

void audio_eof_mp3(const char* info) {
    if (g_impl != nullptr && g_impl->storageLeaseActive) {
        if (g_impl->appState != nullptr) {
            g_impl->appState->markPlaybackCompleted(g_impl->url, g_impl->source);
        }
        g_impl->completedPlaybackSource = g_impl->source;
        g_impl->playbackCompletionPending = true;
        releaseStorageLease(g_impl);
        g_impl->state = "idle";
        g_impl->type = "idle";
        g_impl->title = "Idle";
        g_impl->url = "";
        g_impl->source = "manual";
        g_impl->publish();
    }
    DebugLog.printf("[audio] eof mp3 %s\n", info == nullptr ? "" : info);
}

void audio_eof_speech(const char* info) {
    DebugLog.printf("[audio] eof speech %s\n", info == nullptr ? "" : info);
}

void AudioPlayer::begin(uint8_t bclkPin, uint8_t wsPin, uint8_t doutPin, uint8_t initialVolumePercent, bool outputEnabled, AppState& appState) {
    if (impl_ == nullptr) {
        impl_ = allocatePreferPsram<Impl>();
    }
    if (impl_ == nullptr) {
        DebugLog.println("[audio] failed to allocate player implementation");
        return;
    }
    impl_->appState = &appState;
    g_impl = impl_;
    impl_->bclkPin = bclkPin;
    impl_->wsPin = wsPin;
    impl_->doutPin = doutPin;
    impl_->outputEnabled = outputEnabled;
    impl_->audio.setBufsize(DefaultConfig::AUDIO_BUFFER_SIZE_RAM, DefaultConfig::AUDIO_BUFFER_SIZE_PSRAM);
    impl_->audio.setI2SCommFMT_LSB(false);
    if (outputEnabled) {
        impl_->audio.setPinout(bclkPin, wsPin, doutPin);
    }
    impl_->requestedSampleRateHz = kPreferredDiagnosticSampleRateHz;
    impl_->diagnosticTestMode = DefaultConfig::AUDIO_DIAGNOSTIC_TEST;
    impl_->audio.forceMono(DefaultConfig::AUDIO_FORCE_MONO);
    impl_->channelCount = DefaultConfig::AUDIO_FORCE_MONO ? 1 : 2;
    impl_->audio.setConnectionTimeout(8000, 8000);
    impl_->defaultVolume = constrain(initialVolumePercent, static_cast<uint8_t>(0), static_cast<uint8_t>(100));
    impl_->volume = constrain(initialVolumePercent, static_cast<uint8_t>(0), static_cast<uint8_t>(100));
    impl_->applyHardwareVolumePercent(impl_->volume);
    if (outputEnabled) {
        DebugLog.printf("[audio] init driver=ESP32-audioI2S target=MAX98357A fmt=std-i2s bclk=%u ws=%u dout=%u requested_rate=%lu volume_percent=%u lib_volume=%u mono=%s\n",
                      bclkPin,
                      wsPin,
                      doutPin,
                      static_cast<unsigned long>(impl_->requestedSampleRateHz),
                      impl_->volume,
                      impl_->hardwareAudioVolume,
                      DefaultConfig::AUDIO_FORCE_MONO ? "on" : "off");
    } else {
        DebugLog.println("[audio] output disabled");
    }
    impl_->publish();
}

void AudioPlayer::loop() {
    if (impl_ == nullptr) {
        return;
    }
    if (impl_->speechRendering) {
        int16_t pcm[512];const size_t count = (impl_->renderingPiano?impl_->piano.render(pcm,512):impl_->speech.render(pcm,512));
        if (count) {
            const size_t bytes=count*sizeof(int16_t);
            if (impl_->speechFile.write(reinterpret_cast<uint8_t*>(pcm),bytes)!=bytes) {stop();if (impl_->appState) impl_->appState->setLastError("Offline speech storage write failed");return;}
            impl_->speechBytes+=bytes;
        } else {
            uint8_t header[44]={};memcpy(header,"RIFF",4);memcpy(header+8,"WAVEfmt ",8);memcpy(header+36,"data",4);
            auto le32=[&](unsigned at,uint32_t value){for(unsigned n=0;n<4;++n)header[at+n]=value>>(n*8);};
            le32(4,36+impl_->speechBytes);le32(16,16);header[20]=1;header[22]=1;le32(24,16000);le32(28,32000);header[32]=2;header[34]=16;le32(40,impl_->speechBytes);
            impl_->speechFile.seek(0);bool written=impl_->speechFile.write(header,44)==44;impl_->speechFile.close();endStorageWrite(impl_->speechTarget);impl_->speechRendering=false;
            if (!written){stop();if(impl_->appState)impl_->appState->setLastError("Offline speech WAV finalization failed");return;}
            impl_->startingSource=true;bool started=playStorageFile(impl_->speechTarget,impl_->speechPath,impl_->renderingPiano?"Piano melody":"Offline speech",impl_->renderingPiano?"melody":"tts",impl_->renderingPiano?"piano":"offline-tts");impl_->startingSource=false;
            if (!started){stop();if (impl_->appState)impl_->appState->setLastError("Offline speech playback failed");return;}
        }
    }
    impl_->audio.loop();
    if (impl_->streamWarmupPending) {
        const unsigned long elapsed = millis() - impl_->streamWarmupStartedAt;
        const size_t buffered = impl_->audio.inBufferFilled();
        impl_->streamWarmupHighWaterBytes = max(impl_->streamWarmupHighWaterBytes, buffered);
        const size_t readyBytes = ESP.getPsramSize() > 0 ? kStreamWarmupReadyBytesPsram : kStreamWarmupReadyBytesRam;
        const bool bufferReady = impl_->streamWarmupHighWaterBytes >= readyBytes && elapsed >= kStreamWarmupMinimumMs;
        const bool warmupExpired = elapsed >= kStreamWarmupMaximumMs && impl_->streamWarmupHighWaterBytes > 0;
        if (!impl_->audio.isRunning()) {
            impl_->streamWarmupPending = false;
        } else if (bufferReady || warmupExpired) {
            impl_->streamWarmupPending = false;
            DebugLog.printf("[audio] prebuffer ready high_water=%u bytes elapsed=%lu ms%s\n",
                            static_cast<unsigned>(impl_->streamWarmupHighWaterBytes),
                            elapsed,
                            warmupExpired && !bufferReady ? " fallback" : "");
            impl_->markPlaying();
            impl_->fadeToPercent(impl_->volume, kStartFadeInMs);
        }
    }
    const uint32_t decodedRate=impl_->audio.getSampleRate();
    const uint32_t clock=static_cast<uint32_t>(decodedRate*impl_->playbackSpeed);
    if (impl_->audio.isRunning() && clock && clock!=impl_->clockRate) {if(i2s_set_sample_rates(I2S_NUM_0,clock)==ESP_OK){impl_->clockRate=clock;impl_->activeSampleRateHz=clock;}else if(impl_->appState)impl_->appState->setLastError("Unsupported audio playback clock");}
    if (!impl_->audio.isRunning() && !impl_->speechRendering && !impl_->speechPath.isEmpty()) {fs::FS* fs=getStorageFs(impl_->speechTarget);if(fs){beginStorageWrite(impl_->speechTarget);fs->remove(impl_->speechPath);endStorageWrite(impl_->speechTarget);}impl_->speechPath="";}
    if (impl_->durationProbePending && impl_->storageLeaseActive && impl_->audio.getAudioCurrentTime() > 0) {
        impl_->cachedDurationSeconds = impl_->audio.getAudioFileDuration();
        impl_->durationProbePending = false;
    }
    if (impl_->retryPending && millis() >= impl_->retryAt) {
        impl_->retryPending = false;
        impl_->audio.stopSong();
        recreateAudioEngine(impl_);
        impl_->audio.connecttohost((impl_->connectionUrl.isEmpty() ? impl_->url : impl_->connectionUrl).c_str());
        impl_->state = "buffering";
        impl_->streamWarmupPending = true;
        impl_->streamWarmupStartedAt = millis();
        impl_->streamWarmupHighWaterBytes = 0;
        impl_->applyHardwareVolumePercent(0);
        impl_->publish();
    }
}

bool AudioPlayer::play(const String& url, const String& title, const String& mediaType, const String& source) {
    if (impl_ == nullptr || !impl_->outputEnabled || url.isEmpty()) {
        return false;
    }

    if (!impl_->startingSource) {
        if (impl_->speechRendering) {impl_->speechFile.close();endStorageWrite(impl_->speechTarget);impl_->speechRendering=false;}
        if (!impl_->speechPath.isEmpty()) {impl_->audio.stopSong();releaseStorageLease(impl_);fs::FS* scratchFs=getStorageFs(impl_->speechTarget);if(scratchFs){beginStorageWrite(impl_->speechTarget);scratchFs->remove(impl_->speechPath);endStorageWrite(impl_->speechTarget);}impl_->speechPath="";}
        if (impl_->sourceOverride) {impl_->audio.setTone(impl_->defaultLowDb,impl_->defaultPresenceDb,impl_->defaultHighDb);impl_->sourceOverride=false;}
        impl_->playbackSpeed=1;impl_->pitchShift.reset(1);impl_->clockRate=0;
        impl_->volume=impl_->defaultVolume;
    }
    requestAudioPerformanceClock();
    const String normalizedUrl = PlaybackText::normalizeUrl(url);
    const String normalizedTitle = PlaybackText::normalizeTitle(title, normalizedUrl);

    if (impl_->audio.isRunning() || impl_->state == "playing" || impl_->state == "buffering") {
        impl_->fadeToPercent(0, kSwitchFadeOutMs);
        impl_->audio.stopSong();
        delay(kSwitchQuietTimeMs);
    }
    clearOverlay(impl_);
    releaseStorageLease(impl_);
    if (!impl_->url.isEmpty() || impl_->state == "error") {
        recreateAudioEngine(impl_);
    }

    const String connectionUrl = resolveStreamRedirects(normalizedUrl);

    impl_->stopRequested = false;
    impl_->retryPending = false;
    impl_->retryCount = 0;
    impl_->url = normalizedUrl;
    impl_->connectionUrl = connectionUrl;
    impl_->title = normalizedTitle;
    impl_->type = mediaType;
    impl_->source = source;
    impl_->state = "buffering";
    impl_->streamWarmupPending = true;
    impl_->streamWarmupStartedAt = millis();
    impl_->streamWarmupHighWaterBytes = 0;
    impl_->cachedDurationSeconds = 0;
    impl_->durationProbePending = false;
    impl_->publish();
    impl_->applyHardwareVolumePercent(0);

    bool connected = impl_->audio.connecttohost(connectionUrl.c_str());
    if (!connected) {
        recreateAudioEngine(impl_);
        delay(120);
        connected = impl_->audio.connecttohost(connectionUrl.c_str());
    }
    if (!connected) {
        impl_->applyHardwareVolumePercent(impl_->volume);
        impl_->state = "error";
        DebugLog.printf("[audio] connecttohost failed for %s\n", normalizedUrl.c_str());
        impl_->publish();
        return false;
    }

    impl_->activeSampleRateHz = impl_->audio.getSampleRate();
    impl_->bitsPerSample = impl_->audio.getBitsPerSample();
    impl_->channelCount = impl_->audio.getChannels();
    DebugLog.printf("[audio] connecttohost ok for %s\n", normalizedUrl.c_str());
    DebugLog.printf("[audio] stream connected rate=%lu bits=%u channels=%u; waiting for prebuffer\n",
                  static_cast<unsigned long>(impl_->activeSampleRateHz),
                  static_cast<unsigned>(impl_->bitsPerSample),
                  static_cast<unsigned>(impl_->channelCount));
    return true;
}

bool AudioPlayer::playStorageFile(StorageTarget target, const String& path, const String& title, const String& mediaType, const String& source) {
    if (impl_ == nullptr || !impl_->outputEnabled || path.isEmpty()) {
        return false;
    }

    if (!impl_->startingSource) {
        if (impl_->speechRendering) {impl_->speechFile.close();endStorageWrite(impl_->speechTarget);impl_->speechRendering=false;}
        if (!impl_->speechPath.isEmpty()) {impl_->audio.stopSong();releaseStorageLease(impl_);fs::FS* scratchFs=getStorageFs(impl_->speechTarget);if(scratchFs){beginStorageWrite(impl_->speechTarget);scratchFs->remove(impl_->speechPath);endStorageWrite(impl_->speechTarget);}impl_->speechPath="";}
        if (impl_->sourceOverride) {impl_->audio.setTone(impl_->defaultLowDb,impl_->defaultPresenceDb,impl_->defaultHighDb);impl_->sourceOverride=false;}
        impl_->playbackSpeed=1;impl_->pitchShift.reset(1);impl_->clockRate=0;
        impl_->volume=impl_->defaultVolume;
    }
    requestAudioPerformanceClock();
    fs::FS* fs = getStorageFs(target);
    if (fs == nullptr || !storageMounted(target)) {
        return false;
    }

    const String normalizedTitle = title.isEmpty() ? fallbackTitleFromPath(path) : title;
    const String sourceUrl = String(storageTargetId(target)) + ":" + path;
    const bool switchingSdTrack = impl_->storageLeaseActive && impl_->storageTarget == StorageTarget::Sd && target == StorageTarget::Sd;

    if (impl_->audio.isRunning() || impl_->state == "playing" || impl_->state == "buffering") {
        impl_->fadeToPercent(0, kSwitchFadeOutMs);
        impl_->audio.stopSong();
        delay(kSwitchQuietTimeMs);
        if (switchingSdTrack) {
            delay(90);
        }
    }
    clearOverlay(impl_);
    if (!impl_->url.isEmpty() || impl_->state == "error") {
        recreateAudioEngine(impl_);
    }
    acquireStorageLease(impl_, target);

    impl_->stopRequested = false;
    impl_->retryPending = false;
    impl_->retryCount = 0;
    impl_->url = sourceUrl;
    impl_->connectionUrl = "";
    impl_->title = normalizedTitle;
    impl_->type = mediaType;
    impl_->source = source;
    impl_->state = "buffering";
    impl_->cachedDurationSeconds = 0;
    impl_->durationProbePending = true;
    impl_->publish();
    impl_->applyHardwareVolumePercent(0);

    bool connected = impl_->audio.connecttoFS(*fs, path.c_str());
    if (!connected) {
        delay(120);
        connected = impl_->audio.connecttoFS(*fs, path.c_str());
    }
    if (!connected && target == StorageTarget::Sd) {
        releaseStorageLease(impl_);
        if (remountActiveStorageBackend(target)) {
            acquireStorageLease(impl_, target);
            fs = getStorageFs(target);
            if (fs != nullptr) {
                DebugLog.printf("[audio] retrying SD playback after remount path=%s\n", path.c_str());
                connected = impl_->audio.connecttoFS(*fs, path.c_str());
                if (!connected) {
                    delay(120);
                    connected = impl_->audio.connecttoFS(*fs, path.c_str());
                }
            }
        } else {
            acquireStorageLease(impl_, target);
        }
    }
    if (!connected) {
        releaseStorageLease(impl_);
        impl_->applyHardwareVolumePercent(impl_->volume);
        impl_->state = "error";
        DebugLog.printf("[audio] connecttoFS failed target=%s path=%s\n", storageTargetId(target), path.c_str());
        impl_->publish();
        return false;
    }

    impl_->activeSampleRateHz = impl_->audio.getSampleRate();
    impl_->bitsPerSample = impl_->audio.getBitsPerSample();
    impl_->channelCount = impl_->audio.getChannels();
    impl_->applyHardwareVolumePercent(impl_->volume);
    DebugLog.printf("[audio] connecttoFS ok target=%s path=%s\n", storageTargetId(target), path.c_str());
    DebugLog.printf("[audio] local playback started rate=%lu bits=%u channels=%u lib_volume=%u\n",
                  static_cast<unsigned long>(impl_->activeSampleRateHz),
                  static_cast<unsigned>(impl_->bitsPerSample),
                  static_cast<unsigned>(impl_->channelCount),
                  static_cast<unsigned>(impl_->hardwareAudioVolume));
    impl_->markPlaying();
    return true;
}

bool AudioPlayer::playStorageOverlay(StorageTarget target, const String& path, uint8_t duckPercent, uint8_t overlayPercent) {
    if (impl_ == nullptr || !impl_->outputEnabled || path.isEmpty() || !impl_->audio.isRunning() ||
        !(impl_->state == "playing" || impl_->state == "buffering")) {
        return false;
    }

    String lowered = path;
    lowered.toLowerCase();
    if (!lowered.endsWith(".wav")) {
        return false;
    }

    AudioPlayer::Impl::OverlayState overlay;
    overlay.duckPercent = constrain(duckPercent, static_cast<uint8_t>(0), static_cast<uint8_t>(100));
    overlay.overlayPercent = constrain(overlayPercent, static_cast<uint8_t>(0), static_cast<uint8_t>(100));
    if (!loadWavOverlay(target, path, overlay, max<uint32_t>(1U, impl_->audio.getSampleRate()))) {
        return false;
    }

    clearOverlay(impl_);
    impl_->overlay = overlay;
    return true;
}

void AudioPlayer::stop() {
    if (impl_ == nullptr) {
        return;
    }

    if (impl_->speechRendering) {impl_->speechFile.close();endStorageWrite(impl_->speechTarget);impl_->speechRendering=false;}
    impl_->stopRequested = true;
    impl_->retryPending = false;
    impl_->streamWarmupPending = false;
    if (impl_->audio.isRunning() || impl_->state == "playing" || impl_->state == "buffering") {
        impl_->fadeToPercent(0, kSwitchFadeOutMs);
        delay(kSwitchQuietTimeMs);
    }
    impl_->completedPlaybackSource = impl_->source;
    impl_->playbackCompletionPending = true;
    impl_->audio.stopSong();
    clearOverlay(impl_);
    releaseStorageLease(impl_);
    if (!impl_->speechPath.isEmpty()) {fs::FS* fs=getStorageFs(impl_->speechTarget);if(fs){beginStorageWrite(impl_->speechTarget);fs->remove(impl_->speechPath);endStorageWrite(impl_->speechTarget);}impl_->speechPath="";}
    impl_->playbackSpeed=1;impl_->pitchShift.reset(1);impl_->clockRate=0;impl_->sourceOverride=false;impl_->volume=impl_->defaultVolume;impl_->audio.setTone(impl_->defaultLowDb,impl_->defaultPresenceDb,impl_->defaultHighDb);
    DebugLog.println("[audio] playback stopped");
    impl_->state = "idle";
    impl_->type = "idle";
    impl_->title = "Idle";
    impl_->url = "";
    impl_->connectionUrl = "";
    impl_->source = "manual";
    impl_->publish();
}

void AudioPlayer::releaseResourcesForUpdate() {
    if (impl_ == nullptr) return;
    stop();
    // stopSong only silences I2S; it retains the decoder, input buffer and
    // network connection. Recreate the idle driver to actually release them.
    recreateAudioEngine(impl_);
}

bool AudioPlayer::reconfigureOutputPins(uint8_t bclkPin, uint8_t wsPin, uint8_t doutPin) {
    if (impl_ == nullptr) {
        return false;
    }

    const bool resumePlayback = (impl_->audio.isRunning() || impl_->state == "playing" || impl_->state == "buffering") && !impl_->url.isEmpty();
    const String resumeUrl = impl_->url;
    const String resumeTitle = impl_->title;
    const String resumeType = impl_->type;
    const String resumeSource = impl_->source;

    impl_->retryPending = false;
    impl_->retryCount = 0;
    impl_->stopRequested = false;

    if (resumePlayback) {
        impl_->fadeToPercent(0, kSwitchFadeOutMs);
        impl_->audio.stopSong();
        delay(kSwitchQuietTimeMs);
    }

    if (impl_->outputEnabled) {
        gpio_reset_pin(static_cast<gpio_num_t>(impl_->bclkPin));
        gpio_reset_pin(static_cast<gpio_num_t>(impl_->wsPin));
        gpio_reset_pin(static_cast<gpio_num_t>(impl_->doutPin));
    }

    impl_->audio.setI2SCommFMT_LSB(false);
    impl_->audio.setPinout(bclkPin, wsPin, doutPin);
    impl_->audio.forceMono(DefaultConfig::AUDIO_FORCE_MONO);
    impl_->channelCount = DefaultConfig::AUDIO_FORCE_MONO ? 1 : 2;
    impl_->applyHardwareVolumePercent(impl_->volume);
    impl_->bclkPin = bclkPin;
    impl_->wsPin = wsPin;
    impl_->doutPin = doutPin;
    impl_->outputEnabled = true;
    DebugLog.printf("[audio] reconfigured target=MAX98357A fmt=std-i2s bclk=%u ws=%u dout=%u requested_rate=%lu lib_volume=%u mono=%s\n",
                  bclkPin,
                  wsPin,
                  doutPin,
                  static_cast<unsigned long>(impl_->requestedSampleRateHz),
                  impl_->hardwareAudioVolume,
                  DefaultConfig::AUDIO_FORCE_MONO ? "on" : "off");

    if (!resumePlayback) {
        impl_->publish();
        return true;
    }

    return play(resumeUrl, resumeTitle, resumeType, resumeSource);
}

bool AudioPlayer::disableOutput() {
    if (impl_ == nullptr) {
        return false;
    }

    impl_->retryPending = false;
    impl_->retryCount = 0;
    impl_->stopRequested = true;

    if (impl_->audio.isRunning() || impl_->state == "playing" || impl_->state == "buffering") {
        impl_->fadeToPercent(0, kSwitchFadeOutMs);
        impl_->audio.stopSong();
        delay(kSwitchQuietTimeMs);
    }

    clearOverlay(impl_);
    releaseStorageLease(impl_);

    if (impl_->outputEnabled) {
        gpio_reset_pin(static_cast<gpio_num_t>(impl_->bclkPin));
        gpio_reset_pin(static_cast<gpio_num_t>(impl_->wsPin));
        gpio_reset_pin(static_cast<gpio_num_t>(impl_->doutPin));
    }

    impl_->outputEnabled = false;
    impl_->state = "idle";
    impl_->type = "idle";
    impl_->title = "Idle";
    impl_->url = "";
    impl_->source = "disabled";
    DebugLog.println("[audio] output disabled");
    impl_->publish();
    return true;
}

void AudioPlayer::setVolumePercent(uint8_t volumePercent) {
    if (impl_ == nullptr) {
        return;
    }
    const uint8_t nextVolume = constrain(volumePercent, static_cast<uint8_t>(0), static_cast<uint8_t>(100));
    if (impl_->volume == nextVolume) {
        return;
    }
    impl_->defaultVolume = constrain(volumePercent, static_cast<uint8_t>(0), static_cast<uint8_t>(100));
    impl_->volume = nextVolume;
    impl_->applyHardwareVolumePercent(impl_->streamWarmupPending ? 0 : impl_->volume);
    DebugLog.printf("[audio] volume percent=%u lib_volume=%u\n", impl_->volume, impl_->hardwareAudioVolume);
    impl_->publish();
}

void AudioPlayer::setDirectLibraryVolume(uint8_t libraryVolume) {
    if (impl_ == nullptr) {
        return;
    }
    impl_->setHardwareAudioVolume(libraryVolume);
    impl_->volume = static_cast<uint8_t>(constrain(map(impl_->hardwareAudioVolume, 0, DefaultConfig::AUDIO_MAX_HARDWARE_VOLUME, 0, 100), 0L, 100L));
    DebugLog.printf("[audio] direct lib_volume=%u mapped_percent=%u\n", impl_->hardwareAudioVolume, impl_->volume);
    impl_->publish();
}

void AudioPlayer::setEqualizer(const String& preset, int8_t lowDb, int8_t presenceDb, int8_t highDb) {
    if (impl_ == nullptr) {
        return;
    }
    impl_->defaultLowDb=lowDb;impl_->defaultPresenceDb=presenceDb;impl_->defaultHighDb=highDb;
    impl_->audio.setTone(lowDb, presenceDb, highDb);
    DebugLog.printf("[audio] equalizer preset=%s low=%d presence=%d high=%d dB\n",
                  preset.c_str(), lowDb, presenceDb, highDb);
}

uint8_t AudioPlayer::volumePercent() const {
    return impl_ == nullptr ? 0 : impl_->volume;
}

uint8_t AudioPlayer::libraryVolume() const {
    return impl_ == nullptr ? 0 : impl_->hardwareAudioVolume;
}

String AudioPlayer::currentTitle() const {
    return impl_ == nullptr ? String() : impl_->title;
}

String AudioPlayer::currentUrl() const {
    return impl_ == nullptr ? String() : impl_->url;
}

String AudioPlayer::currentState() const {
    return impl_ == nullptr ? String("idle") : impl_->state;
}

uint32_t AudioPlayer::currentPositionSeconds() const {
    return impl_ != nullptr && impl_->storageLeaseActive ? impl_->audio.getAudioCurrentTime() : 0;
}

uint32_t AudioPlayer::durationSeconds() const {
    return impl_ != nullptr && impl_->storageLeaseActive ? impl_->cachedDurationSeconds : 0;
}

bool AudioPlayer::seekStorageFile(uint32_t positionSeconds) {
    if (impl_ == nullptr || !impl_->storageLeaseActive ||
        !(impl_->state == "playing" || impl_->state == "buffering")) {
        return false;
    }
    const uint32_t duration = impl_->cachedDurationSeconds;
    if (duration == 0) {
        return false;
    }
    const uint32_t clamped = min<uint32_t>(min<uint32_t>(positionSeconds, duration), 65535U);
    String lowercaseUrl = impl_->url;
    lowercaseUrl.toLowerCase();
    bool sought = false;
    if (lowercaseUrl.endsWith(".flac") && duration > 0) {
        const uint32_t dataStart = impl_->audio.getAudioDataStartPos();
        const uint32_t fileSize = impl_->audio.getFileSize();
        const uint32_t audioBytes = fileSize > dataStart ? fileSize - dataStart : 0;
        const uint32_t targetByte = dataStart + static_cast<uint32_t>(
            (static_cast<uint64_t>(audioBytes) * clamped) / duration);
        sought = audioBytes > 0 && impl_->audio.setFilePos(targetByte);
    } else {
        sought = impl_->audio.setAudioPlayPosition(static_cast<uint16_t>(clamped));
    }
    if (sought && impl_->appState != nullptr) {
        impl_->appState->setPlaybackProgress(clamped, duration);
    }
    return sought;
}

bool AudioPlayer::overlayActive() const {
    return impl_ != nullptr && impl_->overlay.active;
}

bool AudioPlayer::consumeOverlayFinished() {
    if (impl_ == nullptr || !impl_->overlay.finished) {
        return false;
    }
    impl_->overlay.finished = false;
    clearOverlay(impl_);
    return true;
}

bool AudioPlayer::consumePlaybackCompletion(String& source) {
    if (impl_ == nullptr || !impl_->playbackCompletionPending) {
        return false;
    }
    source = impl_->completedPlaybackSource;
    impl_->completedPlaybackSource = "";
    impl_->playbackCompletionPending = false;
    return true;
}

AudioPlayer::DiagnosticsSnapshot AudioPlayer::diagnostics() const {
    DiagnosticsSnapshot snapshot;
    if (impl_ == nullptr) {
        return snapshot;
    }

    snapshot.requestedSampleRateHz = impl_->requestedSampleRateHz;
    snapshot.activeSampleRateHz = impl_->activeSampleRateHz;
    snapshot.bitsPerSample = impl_->bitsPerSample;
    snapshot.channelCount = impl_->channelCount;
    snapshot.libraryVolume = impl_->hardwareAudioVolume;
    snapshot.stereoEnabled = impl_->channelCount >= 2;
    snapshot.diagnosticTestMode = impl_->diagnosticTestMode;
    return snapshot;
}

void AudioPlayer::onStationName(const char* text) { (void)text; }
void AudioPlayer::onStreamTitle(const char* text) { (void)text; }
void AudioPlayer::onInfo(const char* text) { (void)text; }
void AudioPlayer::onEof(const char* text) { (void)text; }


bool AudioPlayer::validateSource(JsonVariantConst source, String& error) {
    if (!source.is<JsonObjectConst>()) {error="Audio source must be an object";return false;}
    for(const char* field:{"text","path","language","title"}) if(!source[field].isNull()&&!source[field].is<const char*>()) {error="Audio source text/path fields must be strings";return false;}
    if(!source["equalizer"].isNull()&&!source["equalizer"].is<JsonObjectConst>()) {error="Equalizer must be an object";return false;}
    String text=source["text"] | "",path=source["path"] | "";
    const bool melody=!source["melody"].isNull();
    if (static_cast<int>(!text.isEmpty())+static_cast<int>(!path.isEmpty())+static_cast<int>(melody)!=1) {error="Specify exactly one file path, speech text or melody";return false;}
    if(melody){
        auto m=source["melody"];auto notes=m["notes"];
        if(!m.is<JsonObjectConst>()||!ElmaAudio::PianoSynth::validInstrument(m["instrument"] | "")||!notes.is<JsonArrayConst>()||notes.size()>128){error="Invalid melody instrument or note count";return false;}
        for(JsonVariantConst n:notes.as<JsonArrayConst>()){
            if(!n.is<JsonObjectConst>()||!n["note"].is<int>()||!n["start"].is<float>()||!n["duration"].is<float>()||!n["velocity"].is<float>()){error="Invalid melody note fields";return false;}
            int pitch=n["note"];float start=n["start"],duration=n["duration"],velocity=n["velocity"];
            if(pitch<12||pitch>108||!std::isfinite(start)||!std::isfinite(duration)||!std::isfinite(velocity)||start<0||duration<=0||start+duration>60||velocity<=0||velocity>1){error="Melody notes must fit within 60 seconds";return false;}
        }
    }
    if (!text.isEmpty()) {
        String language=source["language"] | "en";
        if (language!="en" || text.length()>256) {error="Offline speech supports basic English, at most 256 characters";return false;}
        if(!ElmaAudio::SpeechSynth::validText(text.c_str())) {error="Offline speech needs English letters or numbers";return false;}
    } else if(!melody) {
        StorageTarget target;String file;String lower=path;lower.toLowerCase();
        if(!parseAudioFileReference(path,target,file) || (!lower.endsWith(".mp3")&&!lower.endsWith(".aac")&&!lower.endsWith(".wav")&&!lower.endsWith(".m4a")&&!lower.endsWith(".flac")) || file.indexOf("..")>=0) {error="Use an absolute sd:/ or flash:/ MP3, AAC, WAV, M4A or FLAC file";return false;}
    }
    auto valid=[](JsonVariantConst value,float fallback,float minimum,float maximum){if(!value.isNull()&&!value.is<float>())return false;float number=value|fallback;return std::isfinite(number)&&number>=minimum&&number<=maximum;};
    if(!valid(source["speechRate"],.85,.5,1.5)||!valid(source["voicePitch"],125,80,220)||!valid(source["intonation"],.65,0,1)) {error="Invalid speech rate, voice pitch or intonation";return false;}
    if(!valid(source["speed"],1,.25,4)||!valid(source["pitch"],0,-24,24)||!valid(source["volume"],100,0,100)) {error="Invalid speed, pitch or volume";return false;}
    for(const char* band:{"lowDb","presenceDb","highDb"}) if(!valid(source["equalizer"][band],0,-6,6)) {error="Equalizer bands must be -6 to +6 dB";return false;}
    error="";return true;
}
bool AudioPlayer::playSource(JsonVariantConst source, String& error) {
    if(!impl_ || !impl_->outputEnabled) {error="Audio output is disabled";return false;}
    if(!validateSource(source,error)) return false;
    auto applyOptions=[&](){
    impl_->sourceOverride=true;impl_->playbackSpeed=source["speed"] | 1.f;float pitch=source["pitch"] | 0.f;impl_->pitchShift.reset(std::pow(2.f,pitch/12)/impl_->playbackSpeed);impl_->clockRate=0;
    impl_->volume=source["volume"] | impl_->defaultVolume;impl_->applyHardwareVolumePercent(impl_->volume);
    impl_->audio.setTone(source["equalizer"]["lowDb"] | impl_->defaultLowDb,source["equalizer"]["presenceDb"] | impl_->defaultPresenceDb,source["equalizer"]["highDb"] | impl_->defaultHighDb);
    };
    const String text=source["text"] | "";
    const bool melody=!source["melody"].isNull();
    if(!text.isEmpty() || melody) {
        uint32_t frames=0;
        if(melody){ElmaAudio::PianoNote events[128];size_t count=0;for(JsonVariantConst n:source["melody"]["notes"].as<JsonArrayConst>()){auto& event=events[count++];event.note=n["note"];event.start=n["start"];event.duration=n["duration"];event.velocity=n["velocity"];}impl_->piano.begin(source["melody"]["instrument"],events,count);frames=impl_->piano.maximumFrames();}
        else{impl_->speech.begin(text.c_str(),source["speechRate"]|.85f,source["voicePitch"]|125.f,source["intonation"]|.65f);frames=impl_->speech.maximumFrames();}
        StorageTarget target=storageMounted(StorageTarget::Sd)?StorageTarget::Sd:StorageTarget::Flash;
        const StorageBackendSummary summary=getStorageSummary(target);
        if(!summary.mounted || summary.freeBytes < frames*2ULL+65536) {error="Insufficient scratch storage for offline speech";return false;}
        stop();applyOptions();impl_->renderingPiano=melody;impl_->speechTarget=target;impl_->speechPath="/.elma-speech.wav";beginStorageWrite(target);impl_->speechFile=storageOpen(target,impl_->speechPath,"w");
        if(!impl_->speechFile){endStorageWrite(target);impl_->speechPath="";error="Cannot create speech scratch file";return false;}
        uint8_t header[44]={};if(impl_->speechFile.write(header,44)!=44){impl_->speechRendering=true;stop();error="Cannot write speech scratch file";return false;}
        impl_->speechBytes=0;impl_->speechRendering=true;impl_->state="buffering";impl_->type=melody?"melody":"tts";impl_->source=melody?"piano":"offline-tts";impl_->title=melody?"Piano melody":"Offline speech";impl_->url="";impl_->publish();
    } else {
        stop();applyOptions();StorageTarget target;String path;parseAudioFileReference(source["path"].as<String>(),target,path);impl_->startingSource=true;bool started=playStorageFile(target,path,source["title"] | "","media","logic-source");impl_->startingSource=false;
        if(!started){error="Cannot play audio source file";return false;}
    }
    impl_->publish();return true;
}

#endif // APP_DISABLE_AUDIO
