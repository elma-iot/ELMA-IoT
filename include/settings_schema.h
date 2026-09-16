#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <stdint.h>

#ifndef APP_DEFAULT_OLED_ENABLED
#define APP_DEFAULT_OLED_ENABLED 1
#endif

#ifndef APP_DEFAULT_STATUS_LED_PIN
#define APP_DEFAULT_STATUS_LED_PIN 22
#endif

#ifndef APP_STATUS_LED_IS_NEOPIXEL
#define APP_STATUS_LED_IS_NEOPIXEL 0
#endif

#ifndef APP_DEFAULT_WIFI_STA_TX_DBM
#define APP_DEFAULT_WIFI_STA_TX_DBM 15.0f
#endif
#ifndef APP_DEFAULT_WIFI_AP_TX_DBM
#define APP_DEFAULT_WIFI_AP_TX_DBM 15.0f
#endif

struct WiFiSettings {
    String ssid;
    String password;
    String apSsid;
    String apPassword;
    bool apFallbackEnabled = true;
    bool useStaticIp = false;
    float staTxPowerDbm = APP_DEFAULT_WIFI_STA_TX_DBM;
    float apTxPowerDbm = APP_DEFAULT_WIFI_AP_TX_DBM;
    String staticIp;
    String gateway;
    String subnet;
    String dns1;
    String dns2;
};

struct MqttSettings {
    String host;
    uint16_t port = 1883;
    String username;
    String password;
    String clientId;
    String baseTopic;
    bool discoveryEnabled = true;
};

struct OtaSettings {
    String owner;
    String repository;
    String channel;
    String assetTemplate;
    String manifestUrl;
    bool allowInsecureTls = true;
    bool autoCheck = true;
    bool autoUpdate = true;
};

struct BatterySettings {
    uint32_t dividerR1Ohms = 220000;
    uint32_t dividerR2Ohms = 220000;
    float dividerMaxVin = 4.2f;
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    float calibrationMultiplier = 2.0f;
    uint8_t adcPin = 0;
#else
    float calibrationMultiplier = 3.866f;
    uint8_t adcPin = 0;
#endif
    float measuredVoltage = 0.0f;
    uint8_t chargingSensePin = 0;
    uint32_t updateIntervalMs = 10000;
    uint16_t movingAverageWindowSize = 10;
};

struct WebAuthSettings {
    bool enabled = false;
    String username;
    String password;
};

struct AudioSettings {
    bool enabled = true;
    bool rememberLastPlayed = true;
    String equalizerPreset = "flat";
    int8_t equalizerLowDb = 0;
    int8_t equalizerPresenceDb = 0;
    int8_t equalizerHighDb = 0;
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    uint8_t doutPin = 9;
    uint8_t wsPin = 12;
    uint8_t bclkPin = 11;
#else
    uint8_t doutPin = 25;
    uint8_t wsPin = 26;
    uint8_t bclkPin = 27;
#endif
    struct LastPlaybackSettings {
        String url;
        String label;
        String type;
        String source;
        bool resumeAfterBoot = false;
    } lastPlayback;
};

struct OledSettings {
    String displayType = "oled";
    bool enabled = APP_DEFAULT_OLED_ENABLED;
    String driver;
    uint8_t i2cAddress = 0x3C;
    uint8_t width = 128;
    uint8_t height = 64;
    uint16_t rotation = 0;
    uint8_t sdaPin = 23;
    uint8_t sclPin = 19;
    int8_t resetPin = -1;
    uint16_t dimTimeoutSeconds = 0;
    uint8_t wapeTriggerPin = 0;
    String wapeTriggerEvent = "play_start";
};

struct SdSettings {
    bool enabled = true;
    uint8_t csPin = 4;
    uint8_t sckPin = 5;
    uint8_t mosiPin = 6;
    uint8_t misoPin = 7;
};

struct EffectSettings {
    String startupFile;
    uint8_t startupVolumePercent = 100;
    String alarmFile;
    uint8_t alarmVolumePercent = 100;
    String notificationFile;
    uint8_t notificationVolumePercent = 100;
    String ambientSoundFile;
    uint8_t ambientVolumePercent = 20;
    String lowBatteryFile;
    uint8_t lowBatteryVolumePercent = 100;
    String shutDownFile;
    uint8_t shutDownVolumePercent = 100;
    String updateAvailableFile;
    uint8_t updateAvailableVolumePercent = 100;
    String updateSuccessFile;
    uint8_t updateSuccessVolumePercent = 100;
};

struct DeviceSettings {
    String deviceName;
    String friendlyName;
    uint8_t statusLedPin = APP_DEFAULT_STATUS_LED_PIN;
    String statusLedType = APP_STATUS_LED_IS_NEOPIXEL ? "neopixel" : "regular";
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    uint8_t savedVolumePercent = 35;
#else
    uint8_t savedVolumePercent = 5;
#endif
    bool audioMuted = false;
    String button1Action = "previous";
    String button2Action = "next";
    bool lowBatterySleepEnabled = false;
    bool powerCycleFactoryResetEnabled = true;
    bool touchHoldFactoryResetEnabled = true;
    uint8_t lowBatterySleepThresholdPercent = 20;
    uint16_t lowBatteryWakeIntervalMinutes = 15;
};

struct UiSettings {
    bool gpioBoardAutodetect = true;
    String gpioBoardSelection;
    String peripheralDiagramLayout = "{}";
    String peripheralHelperBindings = "{}";
    String peripheralProfileSelections = "{}";
    String motorRuntimeConfig = "{}";
};

struct SettingsBundle {
    WiFiSettings wifi;
    MqttSettings mqtt;
    OtaSettings ota;
    BatterySettings battery;
    WebAuthSettings webAuth;
    AudioSettings audio;
    OledSettings oled;
    SdSettings sd;
    EffectSettings effects;
    DeviceSettings device;
    UiSettings ui;
    bool usingSavedSettings = false;
};

inline bool parseIp(const String& raw, IPAddress& address) {
    if (raw.isEmpty()) {
        return false;
    }
    return address.fromString(raw);
}
