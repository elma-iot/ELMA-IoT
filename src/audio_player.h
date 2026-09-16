#pragma once

#include <Arduino.h>

#include "app_state.h"
#include "storage_backend.h"

class AudioPlayer {
  public:
    class Impl;

    struct DiagnosticsSnapshot {
        uint32_t requestedSampleRateHz = 0;
        uint32_t activeSampleRateHz = 0;
        uint8_t bitsPerSample = 16;
        uint8_t channelCount = 2;
        uint8_t libraryVolume = 0;
        bool stereoEnabled = false;
        bool diagnosticTestMode = false;
    };

    void begin(uint8_t bclkPin, uint8_t wsPin, uint8_t doutPin, uint8_t initialVolumePercent, bool outputEnabled, AppState& appState);
    void loop();
    bool play(const String& url, const String& title, const String& mediaType, const String& source);
    bool playStorageFile(StorageTarget target, const String& path, const String& title, const String& mediaType, const String& source);
    bool playStorageOverlay(StorageTarget target, const String& path, uint8_t duckPercent = 35, uint8_t overlayPercent = 100);
    void stop();
    void releaseResourcesForUpdate();
    bool overlayActive() const;
    bool consumeOverlayFinished();
    bool consumePlaybackCompletion(String& source);
    bool reconfigureOutputPins(uint8_t bclkPin, uint8_t wsPin, uint8_t doutPin);
    bool disableOutput();
    void setVolumePercent(uint8_t volumePercent);
    void setDirectLibraryVolume(uint8_t libraryVolume);
    void setEqualizer(const String& preset, int8_t lowDb, int8_t presenceDb, int8_t highDb);
    uint8_t volumePercent() const;
    uint8_t libraryVolume() const;
    String currentTitle() const;
    String currentUrl() const;
    String currentState() const;
    uint32_t currentPositionSeconds() const;
    uint32_t durationSeconds() const;
    bool seekStorageFile(uint32_t positionSeconds);
    DiagnosticsSnapshot diagnostics() const;

    void onStationName(const char* text);
    void onStreamTitle(const char* text);
    void onInfo(const char* text);
    void onEof(const char* text);

  private:
    Impl* impl_ = nullptr;
};
