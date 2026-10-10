#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <IPAddress.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

struct NetworkSnapshot {
    bool wifiConnected = false;
    bool apMode = false;
    bool mqttConnected = false;
    String ssid;
    String ip;
    String apSsid;
    int32_t wifiRssi = 0;
};

struct PlaybackSnapshot {
    String state = "idle";
    String type = "idle";
    String title = "Idle";
    String url;
    String source = "none";
    uint8_t volumePercent = 0;
    uint32_t completionSequence = 0;
    String completedUrl;
    String completedSource;
    uint32_t positionSeconds = 0;
    uint32_t durationSeconds = 0;
};

struct BatterySnapshot {
    float voltage = 0.0f;
    float rawAdcVoltage = 0.0f;
    uint16_t rawAdc = 0;
    bool charging = false;
};

struct OtaSnapshot {
    bool busy = false;
    bool updateAvailable = false;
    String latestVersion;
    String lastResult = "idle";
    String lastError;
    String phase;
    uint8_t progressPercent = 0;
};

struct DeviceSnapshot {
    int16_t clockUtcOffsetMinutes = 0;
    String deviceName;
    String friendlyName;
};

struct SettingsSnapshot {
    bool usingSaved = false;
};

struct InputChannelSnapshot {
    int8_t configuredIndex = -1;
    uint8_t pin = 0;
    String profile = "none";
    bool nativeTouch = false;
    bool touchSupported = false;
    bool active = false;
    uint8_t sensitivityPercent = 55;
    uint16_t rawValue = 0;
    uint16_t baselineValue = 0;
};

struct InputSnapshot {
    InputChannelSnapshot button1;
    InputChannelSnapshot button2;
};

struct SystemSnapshot {
    uint32_t freeHeap = 0;
    String lastError;
};

struct AppStateSnapshot {
    NetworkSnapshot network;
    PlaybackSnapshot playback;
    BatterySnapshot battery;
    OtaSnapshot ota;
    DeviceSnapshot device;
    SettingsSnapshot settings;
    InputSnapshot input;
    SystemSnapshot system;
};

class AppState {
  public:
    AppState();
    ~AppState();
        bool begin();

    void setDevice(const String& deviceName, const String& friendlyName, bool usingSaved, int16_t clockUtcOffsetMinutes = 0);
    void setWiFiStatus(bool connected, bool apMode, const String& ssid, const IPAddress& ip, int32_t rssi, const String& apSsid);
    void setMqttConnected(bool connected);
    void setPlayback(const String& state, const String& type, const String& title, const String& url, const String& source, uint8_t volumePercent);
    void markPlaybackCompleted(const String& url, const String& source);
    void setPlaybackProgress(uint32_t positionSeconds, uint32_t durationSeconds);
    void setBattery(float voltage, float rawAdcVoltage, uint16_t rawAdc, bool charging);
    void setOta(bool busy, bool updateAvailable, const String& latestVersion, const String& lastResult, const String& lastError,
                const String& phase = "", uint8_t progressPercent = 0);
    void setInputs(const InputChannelSnapshot& button1, const InputChannelSnapshot& button2);
    void setLastError(const String& lastError);
    void setFreeHeap(uint32_t freeHeap);
    AppStateSnapshot snapshot() const;
    void toJson(JsonObject root) const;

  private:
        bool ensureMutex() const;
    mutable SemaphoreHandle_t mutex_;
    mutable StaticSemaphore_t mutexBuffer_;
    AppStateSnapshot state_;
};
