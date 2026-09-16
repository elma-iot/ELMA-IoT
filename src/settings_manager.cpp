#include "settings_manager.h"

#include <ctype.h>
#include <math.h>

#include "default_config.h"
#include "motor_runtime_config.h"
#include "wifi_power_policy.h"
#include "gpio_pin_policy.h"

namespace {
#if defined(CONFIG_IDF_TARGET_ESP32S3)
constexpr auto kPinChip = GpioPinPolicy::Chip::S3;
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
constexpr auto kPinChip = GpioPinPolicy::Chip::C3;
#else
constexpr auto kPinChip = GpioPinPolicy::Chip::Esp32;
#endif
bool safeOutputPin(int pin) {
#if APP_COMPILED_BOARD_PROFILE_ID == 7
    if (pin == 16 || pin == 17) return false;
#elif APP_COMPILED_BOARD_PROFILE_ID >= 3 && APP_COMPILED_BOARD_PROFILE_ID <= 6
    if (pin >= 33 && pin <= 37) return false;
#endif
    return GpioPinPolicy::output(kPinChip, pin);
}
constexpr char PREF_NAMESPACE[] = "notifier";
constexpr char PREF_MARKER[] = "saved";
constexpr float kLegacyEsp32BatteryCalibration = 3.866f;
constexpr uint8_t kDocumentedBuzzerPin = 7;

void normalizeEqualizer(AudioSettings& audio) {
    audio.equalizerPreset.trim();
    audio.equalizerPreset.toLowerCase();
    if (audio.equalizerPreset == "flat") {
        audio.equalizerLowDb = 0; audio.equalizerPresenceDb = 0; audio.equalizerHighDb = 0;
    } else if (audio.equalizerPreset == "clear") {
        audio.equalizerLowDb = -1; audio.equalizerPresenceDb = 1; audio.equalizerHighDb = 4;
    } else if (audio.equalizerPreset == "rock") {
        audio.equalizerLowDb = 4; audio.equalizerPresenceDb = 1; audio.equalizerHighDb = 3;
    } else if (audio.equalizerPreset == "bass") {
        audio.equalizerLowDb = 6; audio.equalizerPresenceDb = 0; audio.equalizerHighDb = -1;
    } else if (audio.equalizerPreset == "classical") {
        audio.equalizerLowDb = 1; audio.equalizerPresenceDb = 2; audio.equalizerHighDb = 4;
    } else if (audio.equalizerPreset == "voice") {
        audio.equalizerLowDb = -3; audio.equalizerPresenceDb = 5; audio.equalizerHighDb = 2;
    } else if (audio.equalizerPreset == "jazz") {
        audio.equalizerLowDb = 3; audio.equalizerPresenceDb = 2; audio.equalizerHighDb = 3;
    } else if (audio.equalizerPreset == "podcast") {
        audio.equalizerLowDb = -4; audio.equalizerPresenceDb = 6; audio.equalizerHighDb = 1;
    } else if (audio.equalizerPreset == "night") {
        audio.equalizerLowDb = -3; audio.equalizerPresenceDb = 2; audio.equalizerHighDb = -3;
    } else if (audio.equalizerPreset != "custom") {
        audio.equalizerPreset = "flat";
        audio.equalizerLowDb = 0; audio.equalizerPresenceDb = 0; audio.equalizerHighDb = 0;
    }
    audio.equalizerLowDb = constrain(audio.equalizerLowDb, static_cast<int8_t>(-6), static_cast<int8_t>(6));
    audio.equalizerPresenceDb = constrain(audio.equalizerPresenceDb, static_cast<int8_t>(-6), static_cast<int8_t>(6));
    audio.equalizerHighDb = constrain(audio.equalizerHighDb, static_cast<int8_t>(-6), static_cast<int8_t>(6));
}

String defaultPeripheralHelperBindings() {
    return "{}";
}

String defaultPeripheralProfileSelections() {
    return "{}";
}

String defaultMotorRuntimeConfig() {
    return "{}";
}

bool approximatelyEqual(float left, float right, float tolerance = 0.05f) {
    return fabsf(left - right) <= tolerance;
}

bool isValidC3ExposedPin(uint8_t pin) {
    return pin <= 10 || pin == 20 || pin == 21;
}

bool isValidBatteryAdcPin(uint8_t pin) {
    if (pin == 0) {
        return true;
    }
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    if (pin < 1 || pin > 20) {
        return false;
    }
    return true;
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
    return pin <= 5;
#else
    return true;
#endif
}

bool isValidStatusLedPin(uint8_t pin) {
    return safeOutputPin(pin);
}

bool isValidWapeTriggerPin(uint8_t pin) {
    if (pin == 0) {
        return true;
    }
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    return pin <= 48;
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
    return isValidC3ExposedPin(pin);
#else
    return pin <= 39;
#endif
}

bool isValidSdPin(uint8_t pin) {
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    return pin <= 48;
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
    return isValidC3ExposedPin(pin);
#else
    return pin <= 39;
#endif
}

bool isValidI2sPin(uint8_t pin) {
    return safeOutputPin(pin);
}

bool audioUsesPin(const AudioSettings& audio, int pin) {
    if (!audio.enabled || pin < 0) {
        return false;
    }
    return audio.bclkPin == pin || audio.wsPin == pin || audio.doutPin == pin;
}

bool hasDistinctI2sPins(const AudioSettings& settings) {
    if (!settings.enabled) {
        return true;
    }
    return settings.bclkPin != settings.wsPin && settings.bclkPin != settings.doutPin && settings.wsPin != settings.doutPin;
}

bool sdUsesPin(const SdSettings& sd, int pin) {
    if (!sd.enabled || pin < 0) {
        return false;
    }
    return sd.csPin == pin || sd.sckPin == pin || sd.mosiPin == pin || sd.misoPin == pin;
}

bool hasDistinctSdPins(const SdSettings& sd) {
    return sd.csPin != sd.sckPin && sd.csPin != sd.mosiPin && sd.csPin != sd.misoPin &&
        sd.sckPin != sd.mosiPin && sd.sckPin != sd.misoPin && sd.mosiPin != sd.misoPin;
}

bool batteryPinConflicts(const BatterySettings& battery, const AudioSettings& audio, const SdSettings& sd) {
    if (battery.adcPin == 0) {
        return false;
    }
    return audioUsesPin(audio, battery.adcPin) || sdUsesPin(sd, battery.adcPin);
}

bool chargingSensePinConflicts(const BatterySettings& battery, const AudioSettings& audio, const DeviceSettings& device, const SdSettings& sd) {
    if (battery.chargingSensePin == 0) {
        return false;
    }
    return battery.chargingSensePin == battery.adcPin || audioUsesPin(audio, battery.chargingSensePin) ||
           battery.chargingSensePin == device.statusLedPin || sdUsesPin(sd, battery.chargingSensePin);
}

bool oledPinConflicts(const OledSettings& oled, const AudioSettings& audio, const BatterySettings& battery, const DeviceSettings& device, const SdSettings& sd) {
    String displayType = oled.displayType;
    displayType.trim();
    displayType.toLowerCase();
    if (!oled.enabled || displayType == "wape") {
        return false;
    }

    const auto conflictsWithReservedPins = [&](int pin) {
        if (pin < 0) {
            return false;
        }

                return audioUsesPin(audio, pin) ||
               pin == battery.adcPin || pin == battery.chargingSensePin ||
               pin == device.statusLedPin || pin == DefaultConfig::BUTTON1_PIN ||
             pin == DefaultConfig::BUTTON2_PIN || pin == kDocumentedBuzzerPin ||
             sdUsesPin(sd, pin);
    };

    if (oled.sdaPin == oled.sclPin) {
        return true;
    }
    if (oled.resetPin >= 0 && (oled.resetPin == oled.sdaPin || oled.resetPin == oled.sclPin)) {
        return true;
    }

    return conflictsWithReservedPins(oled.sdaPin) || conflictsWithReservedPins(oled.sclPin) ||
           conflictsWithReservedPins(oled.resetPin);
}

bool wapeTriggerPinConflicts(const OledSettings& oled, const AudioSettings& audio, const BatterySettings& battery, const DeviceSettings& device, const SdSettings& sd) {
    if (oled.wapeTriggerPin == 0) {
        return false;
    }
    return audioUsesPin(audio, oled.wapeTriggerPin) ||
           oled.wapeTriggerPin == battery.adcPin || oled.wapeTriggerPin == device.statusLedPin || sdUsesPin(sd, oled.wapeTriggerPin)
#if defined(CONFIG_IDF_TARGET_ESP32S3)
           || oled.wapeTriggerPin == 21
#endif
        ;
}

bool sdPinConflictsWithRequiredFunctions(const SdSettings& sd, const AudioSettings& audio, const BatterySettings& battery, const DeviceSettings& device) {
    if (!sd.enabled) {
        return false;
    }

    return audioUsesPin(audio, sd.csPin) || audioUsesPin(audio, sd.sckPin) || audioUsesPin(audio, sd.mosiPin) || audioUsesPin(audio, sd.misoPin) ||
        sdUsesPin(sd, battery.adcPin) || sdUsesPin(sd, battery.chargingSensePin) || sdUsesPin(sd, device.statusLedPin);
}

String normalizeDisplayType(String value) {
    value.trim();
    value.toLowerCase();
    return value == "wape" ? String("wape") : String("oled");
}

String normalizeWapeTriggerEvent(String value) {
    value.trim();
    value.toLowerCase();
    value.replace('-', '_');
    value.replace(' ', '_');
    if (value == "device_start" || value == "charging_start") {
        return value;
    }
    return String("play_start");
}

String defaultDeviceBaseName() {
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3)
    return "elma-iot";
#else
    return DefaultConfig::DEVICE_NAME;
#endif
}

String defaultFriendlyBaseName() {
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3)
    return "ELMA IoT";
#else
    return DefaultConfig::FRIENDLY_NAME;
#endif
}

bool usesLegacyOtaRepository(const String& owner, const String& repository) {
    String normalizedOwner = owner;
    normalizedOwner.trim();
    normalizedOwner.toLowerCase();

    String normalizedRepository = repository;
    normalizedRepository.trim();
    normalizedRepository.toLowerCase();

    // ESP32-S3-Ceiling-Speaker was the device-specific repository used by
    // early ceiling-speaker builds. Some installations already have the new
    // owner saved alongside that old repository name, so match it regardless
    // of owner. The other legacy names only belonged to the previous owner.
    return normalizedRepository == "esp32-s3-ceiling-speaker" ||
        (normalizedOwner == "elik745i" && (
            normalizedRepository == "elma-iot" ||
            normalizedRepository == "esp32-notifier-for-homeassistant"
        ));
}

String defaultOtaAssetTemplate() {
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    #ifdef APP_ENABLE_HACS_MQTT
    #ifdef APP_DISABLE_WEB_UI
        return "esp32s3-notifier-hacs-slim-${version}.bin";
    #else
        return "esp32s3-notifier-hacs-${version}.bin";
    #endif
    #else
        return "esp32s3-notifier-${version}.bin";
    #endif
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
    return "esp32c3-notifier-hacs-${version}.bin";
#else
    #ifdef APP_ENABLE_HACS_MQTT
        #ifdef APP_DISABLE_WEB_UI
        return "esp32-notifier-hacs-slim-${version}.bin";
        #else
        return "esp32-notifier-hacs-${version}.bin";
        #endif
    #else
        return DefaultConfig::OTA_ASSET_TEMPLATE;
    #endif
#endif
}

template <typename T>
T clampValue(T value, T low, T high) {
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

String fallbackIfEmpty(const String& value, const String& fallback) {
    return value.isEmpty() ? fallback : value;
}

String hardwareIdSuffix(bool uppercase = false) {
    const uint64_t chipId = ESP.getEfuseMac();
    const uint32_t shortId = static_cast<uint32_t>(chipId & 0xFFFFFF);

    char buffer[7];
    snprintf(buffer, sizeof(buffer), uppercase ? "%06lX" : "%06lx", static_cast<unsigned long>(shortId));
    return String(buffer);
}

String defaultDeviceName() {
    return defaultDeviceBaseName() + "-" + hardwareIdSuffix(false);
}

String defaultFriendlyName() {
    return defaultFriendlyBaseName() + " " + hardwareIdSuffix(true);
}

String defaultMqttBaseTopic() {
    String topic = defaultDeviceBaseName();
    topic.replace('-', '_');
    topic += "_";
    topic += hardwareIdSuffix(false);
    topic.toLowerCase();
    return topic;
}

bool matchesAnyNormalized(const String& value, std::initializer_list<const char*> patterns) {
    for (const char* pattern : patterns) {
        if (value == String(pattern)) {
            return true;
        }
    }
    return false;
}

bool matchesLegacyGeneratedName(const String& value, std::initializer_list<const char*> bases, char separator) {
    for (const char* base : bases) {
        const String prefix = String(base) + separator;
        if (!value.startsWith(prefix)) {
            continue;
        }
        const String suffix = value.substring(prefix.length());
        if (suffix.length() != 6) {
            continue;
        }
        bool valid = true;
        for (size_t index = 0; index < suffix.length(); ++index) {
            if (!isxdigit(static_cast<unsigned char>(suffix.charAt(index)))) {
                valid = false;
                break;
            }
        }
        if (valid) {
            return true;
        }
    }
    return false;
}

bool isLegacyDefaultDeviceName(const String& value) {
    String normalized = value;
    normalized.trim();
    normalized.toLowerCase();
    return normalized == defaultDeviceBaseName() ||
        matchesAnyNormalized(normalized, {"esp32-notifier", "esp32s3-notifier", "ceiling-speaker", "elma-iot"}) ||
        matchesLegacyGeneratedName(normalized, {"esp32-notifier", "esp32s3-notifier", "ceiling-speaker", "elma-iot"}, '-');
}

bool isLegacyDefaultFriendlyName(const String& value) {
    String normalized = value;
    normalized.trim();
    return normalized == defaultFriendlyBaseName() ||
        matchesAnyNormalized(normalized, {"ESP32 Notifier", "ESP32-S3 Notifier", "Ceiling Speaker", "ELMA IoT"}) ||
        matchesLegacyGeneratedName(normalized, {"ESP32 Notifier", "ESP32-S3 Notifier", "Ceiling Speaker", "ELMA IoT"}, ' ') ||
        isLegacyDefaultDeviceName(normalized);
}

bool isLegacyDefaultMqttBaseTopic(const String& value) {
    String normalized = value;
    normalized.trim();
    normalized.toLowerCase();
    normalized.replace('-', '_');
    return normalized == DefaultConfig::MQTT_BASE_TOPIC ||
        matchesAnyNormalized(normalized, {"esp32_notifier", "esp32s3_notifier", "ceiling_speaker", "elma_iot"}) ||
        matchesLegacyGeneratedName(normalized, {"esp32_notifier", "esp32s3_notifier", "ceiling_speaker", "elma_iot"}, '_');
}

bool isLegacyDefaultMqttClientId(const String& value) {
    String normalized = value;
    normalized.trim();
    normalized.toLowerCase();
    return normalized == defaultDeviceBaseName() ||
        matchesAnyNormalized(normalized, {"esp32-notifier", "esp32s3-notifier", "ceiling-speaker", "elma-iot"}) ||
        matchesLegacyGeneratedName(normalized, {"esp32-notifier", "esp32s3-notifier", "ceiling-speaker", "elma-iot"}, '-');
}

String normalizeButtonAction(String value, const char* fallback) {
    value.trim();
    value.toLowerCase();
    value.replace('-', '_');
    value.replace(' ', '_');

    if (value == "none" || value == "previous" || value == "next" || value == "play_pause" ||
        value == "replay_current" || value == "stop" || value == "volume_up" || value == "volume_down" ||
        value == "ha_previous" || value == "ha_next") {
        return value;
    }

    return String(fallback);
}

String normalizeEffectFileRef(String value) {
    value.trim();
    value.replace('\\', '/');

    if (value.isEmpty()) {
        return "";
    }

    const int separatorIndex = value.indexOf(':');
    if (separatorIndex <= 0) {
        return "";
    }

    String target = value.substring(0, separatorIndex);
    target.trim();
    target.toLowerCase();
    if (target != "sd" && target != "flash") {
        return "";
    }

    String path = value.substring(separatorIndex + 1);
    path.trim();
    if (path.isEmpty()) {
        return "";
    }
    if (!path.startsWith("/")) {
        path = "/" + path;
    }
    while (path.indexOf("//") >= 0) {
        path.replace("//", "/");
    }
    if (path.indexOf("..") >= 0) {
        return "";
    }

    return target + ":" + path;
}

String normalizePeripheralDiagramLayout(String value) {
    value.trim();
    if (value.isEmpty()) {
        return "{}";
    }

    JsonDocument document;
    DeserializationError error = deserializeJson(document, value);
    if (error || !document.is<JsonObject>()) {
        return "{}";
    }

    String normalized;
    serializeJson(document.as<JsonObjectConst>(), normalized);
    return normalized;
}

String normalizePeripheralHelperBindings(String value) {
    value.trim();
    if (value.isEmpty()) {
        return "{}";
    }

    JsonDocument document;
    DeserializationError error = deserializeJson(document, value);
    if (error || !document.is<JsonObject>()) {
        return "{}";
    }

    String normalized;
    serializeJson(document.as<JsonObjectConst>(), normalized);
    return normalized;
}

String normalizePeripheralProfileSelections(String value) {
    value.trim();
    if (value.isEmpty()) {
        return "{}";
    }

    JsonDocument document;
    DeserializationError error = deserializeJson(document, value);
    if (error || !document.is<JsonObject>()) {
        return "{}";
    }

    String normalized;
    serializeJson(document.as<JsonObjectConst>(), normalized);
    return normalized;
}

}  // namespace

bool SettingsManager::begin() {
    return preferences_.begin(PREF_NAMESPACE, false);
}

SettingsBundle SettingsManager::defaults() const {
    SettingsBundle settings;
    const String uniqueDeviceName = defaultDeviceName();
    const String uniqueFriendlyName = defaultFriendlyName();

    settings.wifi.ssid = DefaultConfig::WIFI_SSID;
    settings.wifi.password = DefaultConfig::WIFI_PASSWORD;
    settings.wifi.apSsid = "";
    settings.wifi.apPassword = DefaultConfig::WIFI_AP_PASSWORD;
    settings.wifi.apFallbackEnabled = DefaultConfig::WIFI_AP_FALLBACK_ENABLED;

    settings.mqtt.host = DefaultConfig::MQTT_HOST;
    settings.mqtt.port = DefaultConfig::MQTT_PORT;
    settings.mqtt.username = DefaultConfig::MQTT_USERNAME;
    settings.mqtt.password = DefaultConfig::MQTT_PASSWORD;
    settings.mqtt.baseTopic = defaultMqttBaseTopic();
    settings.mqtt.discoveryEnabled = DefaultConfig::MQTT_DISCOVERY_ENABLED;

    settings.ota.owner = DefaultConfig::OTA_OWNER;
    settings.ota.repository = DefaultConfig::OTA_REPOSITORY;
    settings.ota.channel = DefaultConfig::OTA_CHANNEL;
    settings.ota.assetTemplate = defaultOtaAssetTemplate();
    settings.ota.manifestUrl = DefaultConfig::OTA_MANIFEST_URL;
    settings.ota.allowInsecureTls = DefaultConfig::OTA_ALLOW_INSECURE_TLS;
    settings.ota.autoCheck = true;
    settings.ota.autoUpdate = true;

    settings.battery.dividerR1Ohms = 220000;
    settings.battery.dividerR2Ohms = 220000;
    settings.battery.dividerMaxVin = 4.2f;
    settings.battery.calibrationMultiplier = DefaultConfig::BATTERY_CALIBRATION;
    settings.battery.adcPin = 0;
    settings.battery.measuredVoltage = 0.0f;
    settings.battery.chargingSensePin = 0;
    settings.battery.updateIntervalMs = DefaultConfig::BATTERY_UPDATE_INTERVAL_MS;
    settings.battery.movingAverageWindowSize = DefaultConfig::BATTERY_MOVING_AVERAGE_WINDOW;

    settings.webAuth.enabled = DefaultConfig::WEB_AUTH_ENABLED;
    settings.webAuth.username = DefaultConfig::WEB_USERNAME;
    settings.webAuth.password = DefaultConfig::WEB_PASSWORD;

    settings.audio.enabled = true;
    settings.audio.doutPin = DefaultConfig::I2S_DOUT_PIN;
    settings.audio.wsPin = DefaultConfig::I2S_WS_PIN;
    settings.audio.bclkPin = DefaultConfig::I2S_BCLK_PIN;

    settings.effects.startupFile = "";
    settings.effects.startupVolumePercent = 100;
    settings.effects.alarmFile = "";
    settings.effects.alarmVolumePercent = 100;
    settings.effects.notificationFile = "";
    settings.effects.notificationVolumePercent = 100;
    settings.effects.ambientSoundFile = "";
    settings.effects.ambientVolumePercent = 20;
    settings.effects.lowBatteryFile = "";
    settings.effects.lowBatteryVolumePercent = 100;
    settings.effects.shutDownFile = "";
    settings.effects.shutDownVolumePercent = 100;
    settings.effects.updateAvailableFile = "";
    settings.effects.updateAvailableVolumePercent = 100;
    settings.effects.updateSuccessFile = "";
    settings.effects.updateSuccessVolumePercent = 100;

    settings.oled.enabled = DefaultConfig::OLED_ENABLED;
    settings.oled.displayType = "oled";
    settings.oled.driver = DefaultConfig::OLED_DRIVER;
    settings.oled.i2cAddress = DefaultConfig::OLED_I2C_ADDRESS;
    settings.oled.width = DefaultConfig::OLED_WIDTH;
    settings.oled.height = DefaultConfig::OLED_HEIGHT;
    settings.oled.rotation = DefaultConfig::OLED_ROTATION;
    settings.oled.sdaPin = DefaultConfig::OLED_SDA_PIN;
    settings.oled.sclPin = DefaultConfig::OLED_SCL_PIN;
    settings.oled.resetPin = DefaultConfig::OLED_RESET_PIN;
    settings.oled.dimTimeoutSeconds = DefaultConfig::OLED_DIM_TIMEOUT_SECONDS;
    settings.oled.wapeTriggerPin = 0;
    settings.oled.wapeTriggerEvent = "play_start";

    settings.sd.enabled = false;
    settings.sd.csPin = 4;
    settings.sd.sckPin = 5;
    settings.sd.mosiPin = 6;
    settings.sd.misoPin = 7;

    settings.device.deviceName = uniqueDeviceName;
    settings.device.friendlyName = uniqueFriendlyName;
    settings.device.statusLedPin = DefaultConfig::STATUS_LED_PIN;
    settings.device.statusLedType = DefaultConfig::STATUS_LED_TYPE;
    settings.device.savedVolumePercent = DefaultConfig::DEFAULT_VOLUME_PERCENT;
    settings.device.audioMuted = DefaultConfig::DEFAULT_AUDIO_MUTED;
    settings.device.button1Action = DefaultConfig::BUTTON1_DEFAULT_ACTION;
    settings.device.button2Action = DefaultConfig::BUTTON2_DEFAULT_ACTION;
    settings.device.lowBatterySleepEnabled = DefaultConfig::LOW_BATTERY_SLEEP_ENABLED;
    settings.device.powerCycleFactoryResetEnabled = DefaultConfig::POWER_CYCLE_FACTORY_RESET_ENABLED;
    settings.device.touchHoldFactoryResetEnabled = DefaultConfig::TOUCH_HOLD_FACTORY_RESET_ENABLED;
    settings.device.lowBatterySleepThresholdPercent = DefaultConfig::LOW_BATTERY_SLEEP_THRESHOLD_PERCENT;
    settings.device.lowBatteryWakeIntervalMinutes = DefaultConfig::LOW_BATTERY_WAKE_INTERVAL_MINUTES;
    settings.ui.gpioBoardAutodetect = true;
    settings.ui.gpioBoardSelection = "";
    settings.ui.peripheralDiagramLayout = "{}";
    settings.ui.peripheralHelperBindings = defaultPeripheralHelperBindings();
    settings.ui.peripheralProfileSelections = defaultPeripheralProfileSelections();
    settings.ui.motorRuntimeConfig = defaultMotorRuntimeConfig();
    settings.usingSavedSettings = false;
    return settings;
}

SettingsBundle SettingsManager::sanitize(const SettingsBundle& input) const {
    SettingsBundle settings = input;
    settings.wifi.staTxPowerDbm = WifiPowerPolicy::normalize(settings.wifi.staTxPowerDbm);
    settings.wifi.apTxPowerDbm = WifiPowerPolicy::normalize(settings.wifi.apTxPowerDbm);
    settings.wifi.ssid.trim();
    settings.wifi.password.trim();
    settings.wifi.apSsid.trim();
    settings.wifi.apPassword.trim();
    settings.device.deviceName.trim();
    settings.device.friendlyName.trim();
    settings.device.statusLedType.trim();
    settings.device.statusLedType.toLowerCase();
    settings.mqtt.clientId.trim();
    settings.mqtt.baseTopic.trim();
    settings.mqtt.host.trim();
    settings.ota.owner.trim();
    settings.ota.repository.trim();
    settings.ota.channel.trim();
    settings.ota.assetTemplate.trim();
    settings.ota.manifestUrl.trim();
    settings.webAuth.username.trim();
    settings.ui.gpioBoardSelection.trim();
    settings.audio.lastPlayback.url.trim();
    settings.audio.lastPlayback.label.trim();
    settings.audio.lastPlayback.type.trim();
    settings.audio.lastPlayback.source.trim();

    if (settings.device.deviceName.isEmpty() || isLegacyDefaultDeviceName(settings.device.deviceName)) {
        settings.device.deviceName = defaultDeviceName();
    }
    if (settings.device.friendlyName.isEmpty() || isLegacyDefaultFriendlyName(settings.device.friendlyName)) {
        settings.device.friendlyName = defaultFriendlyName();
    }
    if (settings.mqtt.baseTopic.isEmpty() || isLegacyDefaultMqttBaseTopic(settings.mqtt.baseTopic)) {
        settings.mqtt.baseTopic = defaultMqttBaseTopic();
    }
    if (settings.mqtt.clientId.isEmpty() || isLegacyDefaultMqttClientId(settings.mqtt.clientId)) {
        settings.mqtt.clientId = settings.device.deviceName;
    }
    // Evaluate this once before changing either field. Rechecking after the
    // owner is migrated can hide a legacy repository and leave OTA on a 404.
    const bool legacyOtaRepository = usesLegacyOtaRepository(settings.ota.owner, settings.ota.repository);
    if (settings.ota.owner.isEmpty() || legacyOtaRepository) {
        settings.ota.owner = DefaultConfig::OTA_OWNER;
    }
    if (settings.ota.repository.isEmpty() || legacyOtaRepository) {
        settings.ota.repository = DefaultConfig::OTA_REPOSITORY;
    }
    if (settings.ota.assetTemplate.isEmpty() || settings.ota.assetTemplate == DefaultConfig::OTA_ASSET_TEMPLATE ||
        settings.ota.assetTemplate == "esp32-notifier-hacs-${version}.bin" || settings.ota.assetTemplate == "esp32-notifier-hacs-slim-${version}.bin" ||
        settings.ota.assetTemplate == "esp32s3-notifier-${version}.bin" || settings.ota.assetTemplate == "esp32s3-notifier-hacs-${version}.bin" ||
        settings.ota.assetTemplate == "esp32s3-notifier-hacs-slim-${version}.bin") {
        settings.ota.assetTemplate = defaultOtaAssetTemplate();
    }
    if (settings.ota.autoUpdate) {
        settings.ota.autoCheck = true;
    }
    if (settings.mqtt.port == 0) {
        settings.mqtt.port = DefaultConfig::MQTT_PORT;
    }
    if (!settings.wifi.apPassword.isEmpty() && settings.wifi.apPassword.length() < 8) {
        settings.wifi.apPassword = DefaultConfig::WIFI_AP_PASSWORD;
    }
    if (settings.wifi.apPassword.isEmpty()) {
        settings.wifi.apPassword = DefaultConfig::WIFI_AP_PASSWORD;
    }
    if (settings.device.savedVolumePercent > 100) {
        settings.device.savedVolumePercent = 100;
    }
    if (!isValidStatusLedPin(settings.device.statusLedPin)) {
        settings.device.statusLedPin = DefaultConfig::STATUS_LED_PIN;
    }
    if (settings.device.statusLedType != "regular" && settings.device.statusLedType != "neopixel") {
        settings.device.statusLedType = DefaultConfig::STATUS_LED_TYPE;
    }
    settings.device.button1Action = normalizeButtonAction(settings.device.button1Action, DefaultConfig::BUTTON1_DEFAULT_ACTION);
    settings.device.button2Action = normalizeButtonAction(settings.device.button2Action, DefaultConfig::BUTTON2_DEFAULT_ACTION);
    settings.effects.startupFile = normalizeEffectFileRef(settings.effects.startupFile);
    settings.effects.startupVolumePercent = clampValue<uint8_t>(settings.effects.startupVolumePercent, static_cast<uint8_t>(0), static_cast<uint8_t>(100));
    settings.effects.alarmFile = normalizeEffectFileRef(settings.effects.alarmFile);
    settings.effects.alarmVolumePercent = clampValue<uint8_t>(settings.effects.alarmVolumePercent, static_cast<uint8_t>(0), static_cast<uint8_t>(100));
    settings.effects.notificationFile = normalizeEffectFileRef(settings.effects.notificationFile);
    settings.effects.notificationVolumePercent = clampValue<uint8_t>(settings.effects.notificationVolumePercent, static_cast<uint8_t>(0), static_cast<uint8_t>(100));
    settings.effects.ambientSoundFile = normalizeEffectFileRef(settings.effects.ambientSoundFile);
    settings.effects.ambientVolumePercent = clampValue<uint8_t>(settings.effects.ambientVolumePercent, static_cast<uint8_t>(0), static_cast<uint8_t>(100));
    settings.effects.lowBatteryFile = normalizeEffectFileRef(settings.effects.lowBatteryFile);
    settings.effects.lowBatteryVolumePercent = clampValue<uint8_t>(settings.effects.lowBatteryVolumePercent, static_cast<uint8_t>(0), static_cast<uint8_t>(100));
    settings.effects.shutDownFile = normalizeEffectFileRef(settings.effects.shutDownFile);
    settings.effects.shutDownVolumePercent = clampValue<uint8_t>(settings.effects.shutDownVolumePercent, static_cast<uint8_t>(0), static_cast<uint8_t>(100));
    settings.effects.updateAvailableFile = normalizeEffectFileRef(settings.effects.updateAvailableFile);
    settings.effects.updateAvailableVolumePercent = clampValue<uint8_t>(settings.effects.updateAvailableVolumePercent, static_cast<uint8_t>(0), static_cast<uint8_t>(100));
    settings.effects.updateSuccessFile = normalizeEffectFileRef(settings.effects.updateSuccessFile);
    settings.effects.updateSuccessVolumePercent = clampValue<uint8_t>(settings.effects.updateSuccessVolumePercent, static_cast<uint8_t>(0), static_cast<uint8_t>(100));
    settings.device.lowBatterySleepThresholdPercent = clampValue<uint8_t>(settings.device.lowBatterySleepThresholdPercent, static_cast<uint8_t>(1), static_cast<uint8_t>(100));
    settings.device.lowBatteryWakeIntervalMinutes = clampValue<uint16_t>(settings.device.lowBatteryWakeIntervalMinutes, static_cast<uint16_t>(0), static_cast<uint16_t>(1440));
    settings.battery.dividerR1Ohms = clampValue<uint32_t>(settings.battery.dividerR1Ohms, 10u, 10000000u);
    settings.battery.dividerR2Ohms = clampValue<uint32_t>(settings.battery.dividerR2Ohms, 10u, 10000000u);
    settings.battery.dividerMaxVin = clampValue<float>(settings.battery.dividerMaxVin, 0.1f, 100.0f);
    const float dividerRatio = 1.0f + static_cast<float>(settings.battery.dividerR1Ohms) / settings.battery.dividerR2Ohms;
    settings.battery.calibrationMultiplier = clampValue<float>(settings.battery.calibrationMultiplier, 0.1f, dividerRatio > 10.0f ? dividerRatio : 10.0f);
    settings.battery.measuredVoltage = clampValue<float>(settings.battery.measuredVoltage, 0.0f, 20.0f);
    if (!isValidI2sPin(settings.audio.bclkPin)) {
        settings.audio.bclkPin = DefaultConfig::I2S_BCLK_PIN;
    }
    if (!isValidI2sPin(settings.audio.wsPin)) {
        settings.audio.wsPin = DefaultConfig::I2S_WS_PIN;
    }
    if (!isValidI2sPin(settings.audio.doutPin)) {
        settings.audio.doutPin = DefaultConfig::I2S_DOUT_PIN;
    }
    if (!hasDistinctI2sPins(settings.audio)) {
        settings.audio.bclkPin = DefaultConfig::I2S_BCLK_PIN;
        settings.audio.wsPin = DefaultConfig::I2S_WS_PIN;
        settings.audio.doutPin = DefaultConfig::I2S_DOUT_PIN;
    }
    if (!settings.audio.rememberLastPlayed) {
        settings.audio.lastPlayback.resumeAfterBoot = false;
    }
    normalizeEqualizer(settings.audio);
    settings.audio.lastPlayback.type.toLowerCase();
    if (settings.audio.lastPlayback.type != "stream" && settings.audio.lastPlayback.type != "media") {
        settings.audio.lastPlayback.resumeAfterBoot = false;
    }
    if (settings.audio.lastPlayback.url.isEmpty()) {
        settings.audio.lastPlayback.label = "";
        settings.audio.lastPlayback.type = "";
        settings.audio.lastPlayback.source = "";
        settings.audio.lastPlayback.resumeAfterBoot = false;
    }
    if (!isValidSdPin(settings.sd.csPin) || !isValidSdPin(settings.sd.sckPin) || !isValidSdPin(settings.sd.mosiPin) ||
        !isValidSdPin(settings.sd.misoPin) || !hasDistinctSdPins(settings.sd)) {
        settings.sd = SdSettings();
    }
    if (sdPinConflictsWithRequiredFunctions(settings.sd, settings.audio, settings.battery, settings.device)) {
        settings.sd.enabled = false;
    }
    if (!isValidBatteryAdcPin(settings.battery.adcPin) || batteryPinConflicts(settings.battery, settings.audio, settings.sd)) {
        settings.battery.adcPin = 0;
    }
    if (!isValidBatteryAdcPin(settings.battery.chargingSensePin) || chargingSensePinConflicts(settings.battery, settings.audio, settings.device, settings.sd)) {
        settings.battery.chargingSensePin = 0;
    }
    if (sdPinConflictsWithRequiredFunctions(settings.sd, settings.audio, settings.battery, settings.device)) {
        settings.sd.enabled = false;
    }
    if (sdUsesPin(settings.sd, settings.device.statusLedPin)) {
        settings.device.statusLedPin = DefaultConfig::STATUS_LED_PIN;
    }
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    if (approximatelyEqual(settings.battery.calibrationMultiplier, kLegacyEsp32BatteryCalibration)) {
        settings.battery.calibrationMultiplier = DefaultConfig::BATTERY_CALIBRATION;
    }
#endif
    settings.battery.updateIntervalMs = settings.battery.updateIntervalMs < 250 ? 250 : settings.battery.updateIntervalMs;
    settings.battery.movingAverageWindowSize = clampValue<uint16_t>(settings.battery.movingAverageWindowSize, static_cast<uint16_t>(1), static_cast<uint16_t>(32));
    settings.oled.displayType = normalizeDisplayType(settings.oled.displayType);
    settings.oled.driver.toLowerCase();
    if (settings.oled.driver != "ssd1306" && settings.oled.driver != "sh1106") {
        settings.oled.driver = "ssd1306";
    }
    settings.oled.i2cAddress = clampValue<uint8_t>(settings.oled.i2cAddress, static_cast<uint8_t>(1), static_cast<uint8_t>(127));
    settings.oled.width = clampValue<uint8_t>(settings.oled.width, static_cast<uint8_t>(64), static_cast<uint8_t>(128));
    settings.oled.height = clampValue<uint8_t>(settings.oled.height, static_cast<uint8_t>(32), static_cast<uint8_t>(64));
    if (settings.oled.rotation != 0 && settings.oled.rotation != 90 && settings.oled.rotation != 180 && settings.oled.rotation != 270) {
        settings.oled.rotation = 0;
    }
    settings.oled.dimTimeoutSeconds = clampValue<uint16_t>(settings.oled.dimTimeoutSeconds, static_cast<uint16_t>(0), static_cast<uint16_t>(3600));
    if (!safeOutputPin(settings.oled.sdaPin) || !safeOutputPin(settings.oled.sclPin) ||
        (settings.oled.resetPin >= 0 && !safeOutputPin(settings.oled.resetPin)) ||
        oledPinConflicts(settings.oled, settings.audio, settings.battery, settings.device, settings.sd)) {
        settings.oled.enabled = false;
        settings.oled.sdaPin = DefaultConfig::OLED_SDA_PIN;
        settings.oled.sclPin = DefaultConfig::OLED_SCL_PIN;
        settings.oled.resetPin = DefaultConfig::OLED_RESET_PIN;
    }
    if (!isValidWapeTriggerPin(settings.oled.wapeTriggerPin) || wapeTriggerPinConflicts(settings.oled, settings.audio, settings.battery, settings.device, settings.sd)) {
        settings.oled.wapeTriggerPin = 0;
    }
    settings.oled.wapeTriggerEvent = normalizeWapeTriggerEvent(settings.oled.wapeTriggerEvent);
    settings.ui.peripheralDiagramLayout = normalizePeripheralDiagramLayout(settings.ui.peripheralDiagramLayout);
    settings.ui.peripheralHelperBindings = normalizePeripheralHelperBindings(settings.ui.peripheralHelperBindings);
    settings.ui.peripheralProfileSelections = normalizePeripheralProfileSelections(settings.ui.peripheralProfileSelections);
    settings.ui.motorRuntimeConfig.trim();
    if (settings.ui.motorRuntimeConfig.isEmpty()) {
        settings.ui.motorRuntimeConfig = defaultMotorRuntimeConfig();
    }
    settings.usingSavedSettings = input.usingSavedSettings;
    return settings;
}

SettingsBundle SettingsManager::load() {
    SettingsBundle settings = defaults();
    settings.usingSavedSettings = readBool(PREF_MARKER, false);
    if (!settings.usingSavedSettings) {
        settings.mqtt.clientId = settings.device.deviceName;
        return sanitize(settings);
    }

    const String storedMotorRuntimeConfig = readString("ui_motor", settings.ui.motorRuntimeConfig);

    settings.wifi.ssid = readString("wifi_ssid", settings.wifi.ssid);
    settings.wifi.password = readString("wifi_pass", settings.wifi.password);
    settings.wifi.apSsid = readString("wifi_apssid", settings.wifi.apSsid);
    settings.wifi.apPassword = readString("wifi_appass", settings.wifi.apPassword);
    settings.wifi.apFallbackEnabled = readBool("wifi_apfb", settings.wifi.apFallbackEnabled);
    settings.wifi.useStaticIp = readBool("wifi_static", settings.wifi.useStaticIp);
    settings.wifi.staTxPowerDbm = readFloat("wifi_sta_tx", settings.wifi.staTxPowerDbm);
    settings.wifi.apTxPowerDbm = readFloat("wifi_ap_tx", settings.wifi.apTxPowerDbm);
    settings.wifi.staticIp = readString("wifi_ip", settings.wifi.staticIp);
    settings.wifi.gateway = readString("wifi_gw", settings.wifi.gateway);
    settings.wifi.subnet = readString("wifi_sub", settings.wifi.subnet);
    settings.wifi.dns1 = readString("wifi_dns1", settings.wifi.dns1);
    settings.wifi.dns2 = readString("wifi_dns2", settings.wifi.dns2);

    settings.mqtt.host = readString("mqtt_host", settings.mqtt.host);
    settings.mqtt.port = readUInt("mqtt_port", settings.mqtt.port);
    settings.mqtt.username = readString("mqtt_user", settings.mqtt.username);
    settings.mqtt.password = readString("mqtt_pass", settings.mqtt.password);
    settings.mqtt.clientId = readString("mqtt_cid", settings.device.deviceName);
    settings.mqtt.baseTopic = readString("mqtt_base", settings.mqtt.baseTopic);
    settings.mqtt.discoveryEnabled = readBool("mqtt_disc", settings.mqtt.discoveryEnabled);

    settings.ota.owner = readString("ota_owner", settings.ota.owner);
    settings.ota.repository = readString("ota_repo", settings.ota.repository);
    settings.ota.channel = readString("ota_chan", settings.ota.channel);
    settings.ota.assetTemplate = readString("ota_asset", settings.ota.assetTemplate);
    settings.ota.manifestUrl = readString("ota_manifest", settings.ota.manifestUrl);
    settings.ota.allowInsecureTls = readBool("ota_tls", settings.ota.allowInsecureTls);
    settings.ota.autoCheck = readBool("ota_auto", settings.ota.autoCheck);
    settings.ota.autoUpdate = readBool("ota_upd", settings.ota.autoUpdate);

    settings.battery.dividerR1Ohms = readUInt("bat_r1", settings.battery.dividerR1Ohms);
    settings.battery.dividerR2Ohms = readUInt("bat_r2", settings.battery.dividerR2Ohms);
    settings.battery.dividerMaxVin = readFloat("bat_vmax", settings.battery.dividerMaxVin);
    settings.battery.calibrationMultiplier = readFloat("bat_cal", settings.battery.calibrationMultiplier);
    settings.battery.adcPin = readUInt("bat_pin", settings.battery.adcPin);
    settings.battery.measuredVoltage = readFloat("bat_meas", settings.battery.measuredVoltage);
    settings.battery.chargingSensePin = readUInt("bat_chg", settings.battery.chargingSensePin);
    settings.battery.updateIntervalMs = readUInt("bat_int", settings.battery.updateIntervalMs);
    settings.battery.movingAverageWindowSize = readUInt("bat_win", readUInt("bat_samp", settings.battery.movingAverageWindowSize));

    settings.webAuth.enabled = readBool("web_auth", settings.webAuth.enabled);
    settings.webAuth.username = readString("web_user", settings.webAuth.username);
    settings.webAuth.password = readString("web_pass", settings.webAuth.password);

    settings.audio.enabled = readBool("aud_en", settings.audio.enabled);
    settings.audio.rememberLastPlayed = readBool("aud_rem", settings.audio.rememberLastPlayed);
    settings.audio.equalizerPreset = readString("aud_eq", settings.audio.equalizerPreset);
    settings.audio.equalizerLowDb = readInt("aud_eq_lo", settings.audio.equalizerLowDb);
    settings.audio.equalizerPresenceDb = readInt("aud_eq_mid", settings.audio.equalizerPresenceDb);
    settings.audio.equalizerHighDb = readInt("aud_eq_hi", settings.audio.equalizerHighDb);
    settings.audio.doutPin = readUInt("aud_dout", settings.audio.doutPin);
    settings.audio.wsPin = readUInt("aud_ws", settings.audio.wsPin);
    settings.audio.bclkPin = readUInt("aud_bclk", settings.audio.bclkPin);
    settings.audio.lastPlayback.url = readString("aud_lp_url", settings.audio.lastPlayback.url);
    settings.audio.lastPlayback.label = readString("aud_lp_lbl", settings.audio.lastPlayback.label);
    settings.audio.lastPlayback.type = readString("aud_lp_type", settings.audio.lastPlayback.type);
    settings.audio.lastPlayback.source = readString("aud_lp_src", settings.audio.lastPlayback.source);
    settings.audio.lastPlayback.resumeAfterBoot = readBool("aud_lp_res", settings.audio.lastPlayback.resumeAfterBoot);

    settings.effects.startupFile = readString("eff_start", settings.effects.startupFile);
    settings.effects.startupVolumePercent = readUInt("eff_st_vol", settings.effects.startupVolumePercent);
    settings.effects.alarmFile = readString("eff_alarm", settings.effects.alarmFile);
    settings.effects.alarmVolumePercent = readUInt("eff_al_vol", settings.effects.alarmVolumePercent);
    settings.effects.notificationFile = readString("eff_note", settings.effects.notificationFile);
    settings.effects.notificationVolumePercent = readUInt("eff_no_vol", settings.effects.notificationVolumePercent);
    settings.effects.ambientSoundFile = readString("eff_amb", settings.effects.ambientSoundFile);
    settings.effects.ambientVolumePercent = readUInt("eff_amb_vol", settings.effects.ambientVolumePercent);
    settings.effects.lowBatteryFile = readString("eff_low", settings.effects.lowBatteryFile);
    settings.effects.lowBatteryVolumePercent = readUInt("eff_lo_vol", settings.effects.lowBatteryVolumePercent);
    settings.effects.shutDownFile = readString("eff_down", settings.effects.shutDownFile);
    settings.effects.shutDownVolumePercent = readUInt("eff_sh_vol", settings.effects.shutDownVolumePercent);
    settings.effects.updateAvailableFile = readString("eff_up_av", settings.effects.updateAvailableFile);
    settings.effects.updateAvailableVolumePercent = readUInt("eff_ua_vol", settings.effects.updateAvailableVolumePercent);
    settings.effects.updateSuccessFile = readString("eff_up_ok", settings.effects.updateSuccessFile);
    settings.effects.updateSuccessVolumePercent = readUInt("eff_us_vol", settings.effects.updateSuccessVolumePercent);

    settings.oled.enabled = readBool("oled_en", settings.oled.enabled);
    settings.oled.displayType = readString("oled_mode", settings.oled.displayType);
    settings.oled.driver = readString("oled_drv", settings.oled.driver);
    settings.oled.i2cAddress = readUInt("oled_addr", settings.oled.i2cAddress);
    settings.oled.width = readUInt("oled_w", settings.oled.width);
    settings.oled.height = readUInt("oled_h", settings.oled.height);
    settings.oled.rotation = readUInt("oled_rot", settings.oled.rotation);
    settings.oled.sdaPin = readUInt("oled_sda", settings.oled.sdaPin);
    settings.oled.sclPin = readUInt("oled_scl", settings.oled.sclPin);
    settings.oled.resetPin = readInt("oled_rst", settings.oled.resetPin);
    settings.oled.dimTimeoutSeconds = readUInt("oled_dim", settings.oled.dimTimeoutSeconds);
    settings.oled.wapeTriggerPin = readUInt("oled_wape_pin", settings.oled.wapeTriggerPin);
    settings.oled.wapeTriggerEvent = readString("oled_wape_evt", settings.oled.wapeTriggerEvent);

    settings.sd.enabled = readBool("sd_en", settings.sd.enabled);
    settings.sd.csPin = readUInt("sd_cs", settings.sd.csPin);
    settings.sd.sckPin = readUInt("sd_sck", settings.sd.sckPin);
    settings.sd.mosiPin = readUInt("sd_mosi", settings.sd.mosiPin);
    settings.sd.misoPin = readUInt("sd_miso", settings.sd.misoPin);

    settings.device.deviceName = readString("dev_name", settings.device.deviceName);
    settings.device.friendlyName = readString("dev_friendly", settings.device.friendlyName);
    settings.device.statusLedPin = readUInt("dev_led", settings.device.statusLedPin);
    settings.device.statusLedType = readString("dev_led_type", settings.device.statusLedType);
    settings.device.savedVolumePercent = readUInt("dev_vol", settings.device.savedVolumePercent);
    settings.device.audioMuted = readBool("dev_muted", settings.device.audioMuted);
    settings.device.button1Action = readString("dev_btn1", settings.device.button1Action);
    settings.device.button2Action = readString("dev_btn2", settings.device.button2Action);
    settings.device.lowBatterySleepEnabled = readBool("dev_lbs_en", settings.device.lowBatterySleepEnabled);
    settings.device.powerCycleFactoryResetEnabled = readBool("dev_pcf_reset", settings.device.powerCycleFactoryResetEnabled);
    settings.device.touchHoldFactoryResetEnabled = readBool("dev_thf_reset", settings.device.touchHoldFactoryResetEnabled);
    settings.device.lowBatterySleepThresholdPercent = readUInt("dev_lbs_pct", settings.device.lowBatterySleepThresholdPercent);
    settings.device.lowBatteryWakeIntervalMinutes = readUInt("dev_lbs_wk", settings.device.lowBatteryWakeIntervalMinutes);
    settings.ui.gpioBoardAutodetect = readBool("ui_gpio_auto", settings.ui.gpioBoardAutodetect);
    settings.ui.gpioBoardSelection = readString("ui_gpio_sel", settings.ui.gpioBoardSelection);
    settings.ui.peripheralDiagramLayout = readString("ui_diag", settings.ui.peripheralDiagramLayout);
    settings.ui.peripheralHelperBindings = readString("ui_helpers", settings.ui.peripheralHelperBindings);
    settings.ui.peripheralProfileSelections = readString("ui_profiles", settings.ui.peripheralProfileSelections);
    settings.ui.motorRuntimeConfig = storedMotorRuntimeConfig.isEmpty() ? defaultMotorRuntimeConfig() : storedMotorRuntimeConfig;

    settings = sanitize(settings);
    settings.ui.motorRuntimeConfig = storedMotorRuntimeConfig.isEmpty() ? defaultMotorRuntimeConfig() : storedMotorRuntimeConfig;
    settings.mqtt.clientId = fallbackIfEmpty(settings.mqtt.clientId, settings.device.deviceName);
    settings.usingSavedSettings = true;
    return settings;
}

bool SettingsManager::save(const SettingsBundle& settings) {
    const SettingsBundle sanitized = sanitize(settings);
    const String rawMotorRuntimeConfig = settings.ui.motorRuntimeConfig.isEmpty()
        ? defaultMotorRuntimeConfig()
        : settings.ui.motorRuntimeConfig;
    bool changed = false;
    changed |= writeStringIfChanged("wifi_ssid", sanitized.wifi.ssid);
    changed |= writeStringIfChanged("wifi_pass", sanitized.wifi.password);
    changed |= writeStringIfChanged("wifi_apssid", sanitized.wifi.apSsid);
    changed |= writeStringIfChanged("wifi_appass", sanitized.wifi.apPassword);
    changed |= writeBoolIfChanged("wifi_apfb", sanitized.wifi.apFallbackEnabled);
    changed |= writeBoolIfChanged("wifi_static", sanitized.wifi.useStaticIp);
    changed |= writeFloatIfChanged("wifi_sta_tx", sanitized.wifi.staTxPowerDbm);
    changed |= writeFloatIfChanged("wifi_ap_tx", sanitized.wifi.apTxPowerDbm);
    changed |= writeStringIfChanged("wifi_ip", sanitized.wifi.staticIp);
    changed |= writeStringIfChanged("wifi_gw", sanitized.wifi.gateway);
    changed |= writeStringIfChanged("wifi_sub", sanitized.wifi.subnet);
    changed |= writeStringIfChanged("wifi_dns1", sanitized.wifi.dns1);
    changed |= writeStringIfChanged("wifi_dns2", sanitized.wifi.dns2);

    changed |= writeStringIfChanged("mqtt_host", sanitized.mqtt.host);
    changed |= writeUIntIfChanged("mqtt_port", sanitized.mqtt.port);
    changed |= writeStringIfChanged("mqtt_user", sanitized.mqtt.username);
    changed |= writeStringIfChanged("mqtt_pass", sanitized.mqtt.password);
    changed |= writeStringIfChanged("mqtt_cid", fallbackIfEmpty(sanitized.mqtt.clientId, sanitized.device.deviceName));
    changed |= writeStringIfChanged("mqtt_base", sanitized.mqtt.baseTopic);
    changed |= writeBoolIfChanged("mqtt_disc", sanitized.mqtt.discoveryEnabled);

    changed |= writeStringIfChanged("ota_owner", sanitized.ota.owner);
    changed |= writeStringIfChanged("ota_repo", sanitized.ota.repository);
    changed |= writeStringIfChanged("ota_chan", sanitized.ota.channel);
    changed |= writeStringIfChanged("ota_asset", sanitized.ota.assetTemplate);
    changed |= writeStringIfChanged("ota_manifest", sanitized.ota.manifestUrl);
    changed |= writeBoolIfChanged("ota_tls", sanitized.ota.allowInsecureTls);
    changed |= writeBoolIfChanged("ota_auto", sanitized.ota.autoCheck);
    changed |= writeBoolIfChanged("ota_upd", sanitized.ota.autoUpdate);

    changed |= writeUIntIfChanged("bat_r1", sanitized.battery.dividerR1Ohms);
    changed |= writeUIntIfChanged("bat_r2", sanitized.battery.dividerR2Ohms);
    changed |= writeFloatIfChanged("bat_vmax", sanitized.battery.dividerMaxVin);
    changed |= writeFloatIfChanged("bat_cal", sanitized.battery.calibrationMultiplier);
    changed |= writeUIntIfChanged("bat_pin", sanitized.battery.adcPin);
    changed |= writeFloatIfChanged("bat_meas", sanitized.battery.measuredVoltage);
    changed |= writeUIntIfChanged("bat_chg", sanitized.battery.chargingSensePin);
    changed |= writeUIntIfChanged("bat_int", sanitized.battery.updateIntervalMs);
    changed |= writeUIntIfChanged("bat_win", sanitized.battery.movingAverageWindowSize);

    changed |= writeBoolIfChanged("web_auth", sanitized.webAuth.enabled);
    changed |= writeStringIfChanged("web_user", sanitized.webAuth.username);
    changed |= writeStringIfChanged("web_pass", sanitized.webAuth.password);

    changed |= writeBoolIfChanged("aud_en", sanitized.audio.enabled);
    changed |= writeBoolIfChanged("aud_rem", sanitized.audio.rememberLastPlayed);
    changed |= writeStringIfChanged("aud_eq", sanitized.audio.equalizerPreset);
    changed |= writeIntIfChanged("aud_eq_lo", sanitized.audio.equalizerLowDb);
    changed |= writeIntIfChanged("aud_eq_mid", sanitized.audio.equalizerPresenceDb);
    changed |= writeIntIfChanged("aud_eq_hi", sanitized.audio.equalizerHighDb);
    changed |= writeUIntIfChanged("aud_dout", sanitized.audio.doutPin);
    changed |= writeUIntIfChanged("aud_ws", sanitized.audio.wsPin);
    changed |= writeUIntIfChanged("aud_bclk", sanitized.audio.bclkPin);
    changed |= writeStringIfChanged("aud_lp_url", sanitized.audio.lastPlayback.url);
    changed |= writeStringIfChanged("aud_lp_lbl", sanitized.audio.lastPlayback.label);
    changed |= writeStringIfChanged("aud_lp_type", sanitized.audio.lastPlayback.type);
    changed |= writeStringIfChanged("aud_lp_src", sanitized.audio.lastPlayback.source);
    changed |= writeBoolIfChanged("aud_lp_res", sanitized.audio.lastPlayback.resumeAfterBoot);

    changed |= writeStringIfChanged("eff_start", sanitized.effects.startupFile);
    changed |= writeUIntIfChanged("eff_st_vol", sanitized.effects.startupVolumePercent);
    changed |= writeStringIfChanged("eff_alarm", sanitized.effects.alarmFile);
    changed |= writeUIntIfChanged("eff_al_vol", sanitized.effects.alarmVolumePercent);
    changed |= writeStringIfChanged("eff_note", sanitized.effects.notificationFile);
    changed |= writeUIntIfChanged("eff_no_vol", sanitized.effects.notificationVolumePercent);
    changed |= writeStringIfChanged("eff_amb", sanitized.effects.ambientSoundFile);
    changed |= writeUIntIfChanged("eff_amb_vol", sanitized.effects.ambientVolumePercent);
    changed |= writeStringIfChanged("eff_low", sanitized.effects.lowBatteryFile);
    changed |= writeUIntIfChanged("eff_lo_vol", sanitized.effects.lowBatteryVolumePercent);
    changed |= writeStringIfChanged("eff_down", sanitized.effects.shutDownFile);
    changed |= writeUIntIfChanged("eff_sh_vol", sanitized.effects.shutDownVolumePercent);
    changed |= writeStringIfChanged("eff_up_av", sanitized.effects.updateAvailableFile);
    changed |= writeUIntIfChanged("eff_ua_vol", sanitized.effects.updateAvailableVolumePercent);
    changed |= writeStringIfChanged("eff_up_ok", sanitized.effects.updateSuccessFile);
    changed |= writeUIntIfChanged("eff_us_vol", sanitized.effects.updateSuccessVolumePercent);

    changed |= writeBoolIfChanged("oled_en", sanitized.oled.enabled);
    changed |= writeStringIfChanged("oled_mode", sanitized.oled.displayType);
    changed |= writeStringIfChanged("oled_drv", sanitized.oled.driver);
    changed |= writeUIntIfChanged("oled_addr", sanitized.oled.i2cAddress);
    changed |= writeUIntIfChanged("oled_w", sanitized.oled.width);
    changed |= writeUIntIfChanged("oled_h", sanitized.oled.height);
    changed |= writeUIntIfChanged("oled_rot", sanitized.oled.rotation);
    changed |= writeUIntIfChanged("oled_sda", sanitized.oled.sdaPin);
    changed |= writeUIntIfChanged("oled_scl", sanitized.oled.sclPin);
    changed |= writeIntIfChanged("oled_rst", sanitized.oled.resetPin);
    changed |= writeUIntIfChanged("oled_wape_pin", sanitized.oled.wapeTriggerPin);
    changed |= writeStringIfChanged("oled_wape_evt", sanitized.oled.wapeTriggerEvent);

    changed |= writeBoolIfChanged("sd_en", sanitized.sd.enabled);
    changed |= writeUIntIfChanged("sd_cs", sanitized.sd.csPin);
    changed |= writeUIntIfChanged("sd_sck", sanitized.sd.sckPin);
    changed |= writeUIntIfChanged("sd_mosi", sanitized.sd.mosiPin);
    changed |= writeUIntIfChanged("sd_miso", sanitized.sd.misoPin);

    changed |= writeStringIfChanged("dev_name", sanitized.device.deviceName);
    changed |= writeStringIfChanged("dev_friendly", sanitized.device.friendlyName);
    changed |= writeUIntIfChanged("dev_led", sanitized.device.statusLedPin);
    changed |= writeStringIfChanged("dev_led_type", sanitized.device.statusLedType);
    changed |= writeUIntIfChanged("dev_vol", sanitized.device.savedVolumePercent);
    changed |= writeBoolIfChanged("dev_muted", sanitized.device.audioMuted);
    changed |= writeStringIfChanged("dev_btn1", sanitized.device.button1Action);
    changed |= writeStringIfChanged("dev_btn2", sanitized.device.button2Action);
    changed |= writeBoolIfChanged("dev_lbs_en", sanitized.device.lowBatterySleepEnabled);
    changed |= writeBoolIfChanged("dev_pcf_reset", sanitized.device.powerCycleFactoryResetEnabled);
    changed |= writeBoolIfChanged("dev_thf_reset", sanitized.device.touchHoldFactoryResetEnabled);
    changed |= writeUIntIfChanged("dev_lbs_pct", sanitized.device.lowBatterySleepThresholdPercent);
    changed |= writeUIntIfChanged("dev_lbs_wk", sanitized.device.lowBatteryWakeIntervalMinutes);
    changed |= writeBoolIfChanged("ui_gpio_auto", sanitized.ui.gpioBoardAutodetect);
    changed |= writeStringIfChanged("ui_gpio_sel", sanitized.ui.gpioBoardSelection);
    changed |= writeStringIfChanged("ui_diag", sanitized.ui.peripheralDiagramLayout);
    changed |= writeStringIfChanged("ui_helpers", sanitized.ui.peripheralHelperBindings);
    changed |= writeStringIfChanged("ui_profiles", sanitized.ui.peripheralProfileSelections);
    changed |= writeStringIfChanged("ui_motor", rawMotorRuntimeConfig);
    changed |= writeBoolIfChanged(PREF_MARKER, true);
    return changed;
}

bool SettingsManager::saveAudioEqualizer(const AudioSettings& audio) {
    AudioSettings sanitized = audio;
    normalizeEqualizer(sanitized);
    bool changed = false;
    changed |= writeStringIfChanged("aud_eq", sanitized.equalizerPreset);
    changed |= writeIntIfChanged("aud_eq_lo", sanitized.equalizerLowDb);
    changed |= writeIntIfChanged("aud_eq_mid", sanitized.equalizerPresenceDb);
    changed |= writeIntIfChanged("aud_eq_hi", sanitized.equalizerHighDb);
    changed |= writeBoolIfChanged(PREF_MARKER, true);
    return changed;
}

bool SettingsManager::reset() {
    return preferences_.clear();
}

void SettingsManager::toJson(const SettingsBundle& settings, JsonObject root) const {
    JsonObject wifi = root["wifi"].to<JsonObject>();
    wifi["ssid"] = settings.wifi.ssid;
    wifi["password"] = settings.wifi.password;
    wifi["apSsid"] = settings.wifi.apSsid;
    wifi["apPassword"] = settings.wifi.apPassword;
    wifi["apFallbackEnabled"] = settings.wifi.apFallbackEnabled;
    wifi["useStaticIp"] = settings.wifi.useStaticIp;
    wifi["staTxPowerDbm"] = settings.wifi.staTxPowerDbm;
    wifi["apTxPowerDbm"] = settings.wifi.apTxPowerDbm;
    wifi["staticIp"] = settings.wifi.staticIp;
    wifi["gateway"] = settings.wifi.gateway;
    wifi["subnet"] = settings.wifi.subnet;
    wifi["dns1"] = settings.wifi.dns1;
    wifi["dns2"] = settings.wifi.dns2;

    JsonObject mqtt = root["mqtt"].to<JsonObject>();
    mqtt["host"] = settings.mqtt.host;
    mqtt["port"] = settings.mqtt.port;
    mqtt["username"] = settings.mqtt.username;
    mqtt["password"] = settings.mqtt.password;
    mqtt["clientId"] = settings.mqtt.clientId;
    mqtt["baseTopic"] = settings.mqtt.baseTopic;
    mqtt["discoveryEnabled"] = settings.mqtt.discoveryEnabled;

    JsonObject ota = root["ota"].to<JsonObject>();
    ota["owner"] = settings.ota.owner;
    ota["repository"] = settings.ota.repository;
    ota["channel"] = settings.ota.channel;
    ota["assetTemplate"] = settings.ota.assetTemplate;
    ota["manifestUrl"] = settings.ota.manifestUrl;
    ota["allowInsecureTls"] = settings.ota.allowInsecureTls;
    ota["autoCheck"] = settings.ota.autoCheck;
    ota["autoUpdate"] = settings.ota.autoUpdate;

    JsonObject battery = root["battery"].to<JsonObject>();
    battery["dividerR1Ohms"] = settings.battery.dividerR1Ohms;
    battery["dividerR2Ohms"] = settings.battery.dividerR2Ohms;
    battery["dividerMaxVin"] = settings.battery.dividerMaxVin;
    battery["calibrationMultiplier"] = settings.battery.calibrationMultiplier;
    battery["adcPin"] = settings.battery.adcPin;
    battery["measuredVoltage"] = settings.battery.measuredVoltage;
    battery["chargingSensePin"] = settings.battery.chargingSensePin;
    battery["updateIntervalMs"] = settings.battery.updateIntervalMs;
    battery["movingAverageWindowSize"] = settings.battery.movingAverageWindowSize;

    JsonObject webAuth = root["webAuth"].to<JsonObject>();
    webAuth["enabled"] = settings.webAuth.enabled;
    webAuth["username"] = settings.webAuth.username;
    webAuth["password"] = settings.webAuth.password;

    JsonObject audio = root["audio"].to<JsonObject>();
    audio["enabled"] = settings.audio.enabled;
    audio["rememberLastPlayed"] = settings.audio.rememberLastPlayed;
    audio["equalizerPreset"] = settings.audio.equalizerPreset;
    audio["equalizerLowDb"] = settings.audio.equalizerLowDb;
    audio["equalizerPresenceDb"] = settings.audio.equalizerPresenceDb;
    audio["equalizerHighDb"] = settings.audio.equalizerHighDb;
    audio["doutPin"] = settings.audio.doutPin;
    audio["wsPin"] = settings.audio.wsPin;
    audio["bclkPin"] = settings.audio.bclkPin;
    JsonObject lastPlayback = audio["lastPlayback"].to<JsonObject>();
    lastPlayback["url"] = settings.audio.lastPlayback.url;
    lastPlayback["label"] = settings.audio.lastPlayback.label;
    lastPlayback["type"] = settings.audio.lastPlayback.type;
    lastPlayback["source"] = settings.audio.lastPlayback.source;
    lastPlayback["resumeAfterBoot"] = settings.audio.lastPlayback.resumeAfterBoot;

    JsonObject effects = root["effects"].to<JsonObject>();
    effects["startupFile"] = settings.effects.startupFile;
    effects["startupVolumePercent"] = settings.effects.startupVolumePercent;
    effects["alarmFile"] = settings.effects.alarmFile;
    effects["alarmVolumePercent"] = settings.effects.alarmVolumePercent;
    effects["notificationFile"] = settings.effects.notificationFile;
    effects["notificationVolumePercent"] = settings.effects.notificationVolumePercent;
    effects["ambientSoundFile"] = settings.effects.ambientSoundFile;
    effects["ambientVolumePercent"] = settings.effects.ambientVolumePercent;
    effects["lowBatteryFile"] = settings.effects.lowBatteryFile;
    effects["lowBatteryVolumePercent"] = settings.effects.lowBatteryVolumePercent;
    effects["shutDownFile"] = settings.effects.shutDownFile;
    effects["shutDownVolumePercent"] = settings.effects.shutDownVolumePercent;
    effects["updateAvailableFile"] = settings.effects.updateAvailableFile;
    effects["updateAvailableVolumePercent"] = settings.effects.updateAvailableVolumePercent;
    effects["updateSuccessFile"] = settings.effects.updateSuccessFile;
    effects["updateSuccessVolumePercent"] = settings.effects.updateSuccessVolumePercent;

    JsonObject oled = root["oled"].to<JsonObject>();
    oled["enabled"] = settings.oled.enabled;
    oled["displayType"] = settings.oled.displayType;
    oled["driver"] = settings.oled.driver;
    oled["i2cAddress"] = settings.oled.i2cAddress;
    oled["width"] = settings.oled.width;
    oled["height"] = settings.oled.height;
    oled["rotation"] = settings.oled.rotation;
    oled["sdaPin"] = settings.oled.sdaPin;
    oled["sclPin"] = settings.oled.sclPin;
    oled["resetPin"] = settings.oled.resetPin;
    oled["dimTimeoutSeconds"] = settings.oled.dimTimeoutSeconds;
    oled["wapeTriggerPin"] = settings.oled.wapeTriggerPin;
    oled["wapeTriggerEvent"] = settings.oled.wapeTriggerEvent;

    JsonObject sd = root["sd"].to<JsonObject>();
    sd["enabled"] = settings.sd.enabled;
    sd["csPin"] = settings.sd.csPin;
    sd["sckPin"] = settings.sd.sckPin;
    sd["mosiPin"] = settings.sd.mosiPin;
    sd["misoPin"] = settings.sd.misoPin;

    JsonObject device = root["device"].to<JsonObject>();
    device["deviceName"] = settings.device.deviceName;
    device["friendlyName"] = settings.device.friendlyName;
    device["statusLedPin"] = settings.device.statusLedPin;
    device["statusLedType"] = settings.device.statusLedType;
    device["savedVolumePercent"] = settings.device.savedVolumePercent;
    device["audioMuted"] = settings.device.audioMuted;
    device["button1Action"] = settings.device.button1Action;
    device["button2Action"] = settings.device.button2Action;
    device["lowBatterySleepEnabled"] = settings.device.lowBatterySleepEnabled;
    device["powerCycleFactoryResetEnabled"] = settings.device.powerCycleFactoryResetEnabled;
    device["touchHoldFactoryResetEnabled"] = settings.device.touchHoldFactoryResetEnabled;
    device["lowBatterySleepThresholdPercent"] = settings.device.lowBatterySleepThresholdPercent;
    device["lowBatteryWakeIntervalMinutes"] = settings.device.lowBatteryWakeIntervalMinutes;

    JsonObject ui = root["ui"].to<JsonObject>();
    ui["gpioBoardAutodetect"] = settings.ui.gpioBoardAutodetect;
    ui["gpioBoardSelection"] = settings.ui.gpioBoardSelection;
    ui["peripheralDiagramLayout"] = settings.ui.peripheralDiagramLayout;

    ui["peripheralHelperBindings"] = settings.ui.peripheralHelperBindings;
    ui["peripheralProfiles"] = settings.ui.peripheralProfileSelections;
    ui["motorRuntimeConfig"] = settings.ui.motorRuntimeConfig;

    root["usingSavedSettings"] = settings.usingSavedSettings;
}

bool SettingsManager::updateFromJson(SettingsBundle& settings, JsonVariantConst root, String& error) const {
    if (!root.is<JsonObjectConst>()) {
        error = "Expected JSON object";
        return false;
    }

    JsonObjectConst object = root.as<JsonObjectConst>();
    auto copyString = [](JsonObjectConst section, const char* key, String& target) {
        if (section[key].is<const char*>()) {
            target = section[key].as<const char*>();
        }
    };
    auto copyJsonStringOrObject = [](JsonObjectConst section, const char* key, String& target) {
        JsonVariantConst value = section[key];
        if (value.isNull()) {
            return;
        }

        if (value.is<JsonObjectConst>() || value.is<JsonArrayConst>()) {
            String serialized;
            serializeJson(value, serialized);
            target = serialized;
            return;
        }

        const char* rawValue = value.as<const char*>();
        if (rawValue != nullptr) {
            target = rawValue;
            return;
        }

        String serialized;
        serializeJson(value, serialized);
        if (serialized.length() >= 2 && serialized.charAt(0) == '"' && serialized.charAt(serialized.length() - 1) == '"') {
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
            DynamicJsonDocument decodedValue(serialized.length() * 2U + 64U);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
            if (!deserializeJson(decodedValue, serialized) && decodedValue.is<const char*>()) {
                target = decodedValue.as<const char*>();
                return;
            }
        }

        if (!serialized.isEmpty()) {
            target = serialized;
        }
    };

    JsonObjectConst wifi = object["wifi"];
    if (!wifi.isNull()) {
        copyString(wifi, "ssid", settings.wifi.ssid);
        copyString(wifi, "password", settings.wifi.password);
        copyString(wifi, "apSsid", settings.wifi.apSsid);
        copyString(wifi, "apPassword", settings.wifi.apPassword);
        copyString(wifi, "staticIp", settings.wifi.staticIp);
        copyString(wifi, "gateway", settings.wifi.gateway);
        copyString(wifi, "subnet", settings.wifi.subnet);
        copyString(wifi, "dns1", settings.wifi.dns1);
        copyString(wifi, "dns2", settings.wifi.dns2);
        if (wifi["apFallbackEnabled"].is<bool>()) settings.wifi.apFallbackEnabled = wifi["apFallbackEnabled"].as<bool>();
        if (wifi["useStaticIp"].is<bool>()) settings.wifi.useStaticIp = wifi["useStaticIp"].as<bool>();
        if (wifi["staTxPowerDbm"].is<float>()) settings.wifi.staTxPowerDbm = wifi["staTxPowerDbm"].as<float>();
        if (wifi["apTxPowerDbm"].is<float>()) settings.wifi.apTxPowerDbm = wifi["apTxPowerDbm"].as<float>();
    }

    JsonObjectConst mqtt = object["mqtt"];
    if (!mqtt.isNull()) {
        copyString(mqtt, "host", settings.mqtt.host);
        copyString(mqtt, "username", settings.mqtt.username);
        copyString(mqtt, "password", settings.mqtt.password);
        copyString(mqtt, "clientId", settings.mqtt.clientId);
        copyString(mqtt, "baseTopic", settings.mqtt.baseTopic);
        if (mqtt["port"].is<uint16_t>()) settings.mqtt.port = mqtt["port"].as<uint16_t>();
        if (mqtt["discoveryEnabled"].is<bool>()) settings.mqtt.discoveryEnabled = mqtt["discoveryEnabled"].as<bool>();
    }

    JsonObjectConst ota = object["ota"];
    if (!ota.isNull()) {
        copyString(ota, "owner", settings.ota.owner);
        copyString(ota, "repository", settings.ota.repository);
        copyString(ota, "channel", settings.ota.channel);
        copyString(ota, "assetTemplate", settings.ota.assetTemplate);
        copyString(ota, "manifestUrl", settings.ota.manifestUrl);
        if (ota["allowInsecureTls"].is<bool>()) settings.ota.allowInsecureTls = ota["allowInsecureTls"].as<bool>();
        if (ota["autoCheck"].is<bool>()) settings.ota.autoCheck = ota["autoCheck"].as<bool>();
        if (ota["autoUpdate"].is<bool>()) settings.ota.autoUpdate = ota["autoUpdate"].as<bool>();
    }

    JsonObjectConst battery = object["battery"];
    if (!battery.isNull()) {
        if (battery["dividerR1Ohms"].is<uint32_t>()) settings.battery.dividerR1Ohms = battery["dividerR1Ohms"].as<uint32_t>();
        if (battery["dividerR2Ohms"].is<uint32_t>()) settings.battery.dividerR2Ohms = battery["dividerR2Ohms"].as<uint32_t>();
        if (battery["dividerMaxVin"].is<float>()) settings.battery.dividerMaxVin = battery["dividerMaxVin"].as<float>();
        if (battery["calibrationMultiplier"].is<float>()) settings.battery.calibrationMultiplier = battery["calibrationMultiplier"].as<float>();
        if (battery["adcPin"].is<uint8_t>()) settings.battery.adcPin = battery["adcPin"].as<uint8_t>();
        if (battery["measuredVoltage"].is<float>()) settings.battery.measuredVoltage = battery["measuredVoltage"].as<float>();
        if (battery["chargingSensePin"].is<uint8_t>()) settings.battery.chargingSensePin = battery["chargingSensePin"].as<uint8_t>();
        if (battery["updateIntervalMs"].is<uint32_t>()) settings.battery.updateIntervalMs = battery["updateIntervalMs"].as<uint32_t>();
        if (battery["movingAverageWindowSize"].is<uint16_t>()) settings.battery.movingAverageWindowSize = battery["movingAverageWindowSize"].as<uint16_t>();
        if (battery["sampleCount"].is<uint16_t>()) settings.battery.movingAverageWindowSize = battery["sampleCount"].as<uint16_t>();
    }

    JsonObjectConst webAuth = object["webAuth"];
    if (!webAuth.isNull()) {
        copyString(webAuth, "username", settings.webAuth.username);
        copyString(webAuth, "password", settings.webAuth.password);
        if (webAuth["enabled"].is<bool>()) settings.webAuth.enabled = webAuth["enabled"].as<bool>();
    }

    JsonObjectConst audio = object["audio"];
    if (!audio.isNull()) {
        if (audio["enabled"].is<bool>()) settings.audio.enabled = audio["enabled"].as<bool>();
        if (audio["rememberLastPlayed"].is<bool>()) settings.audio.rememberLastPlayed = audio["rememberLastPlayed"].as<bool>();
        copyString(audio, "equalizerPreset", settings.audio.equalizerPreset);
        if (audio["equalizerLowDb"].is<int8_t>()) settings.audio.equalizerLowDb = audio["equalizerLowDb"].as<int8_t>();
        if (audio["equalizerPresenceDb"].is<int8_t>()) settings.audio.equalizerPresenceDb = audio["equalizerPresenceDb"].as<int8_t>();
        if (audio["equalizerHighDb"].is<int8_t>()) settings.audio.equalizerHighDb = audio["equalizerHighDb"].as<int8_t>();
        if (audio["doutPin"].is<uint8_t>()) settings.audio.doutPin = audio["doutPin"].as<uint8_t>();
        if (audio["wsPin"].is<uint8_t>()) settings.audio.wsPin = audio["wsPin"].as<uint8_t>();
        if (audio["bclkPin"].is<uint8_t>()) settings.audio.bclkPin = audio["bclkPin"].as<uint8_t>();
        JsonObjectConst lastPlayback = audio["lastPlayback"];
        if (!lastPlayback.isNull()) {
            copyString(lastPlayback, "url", settings.audio.lastPlayback.url);
            copyString(lastPlayback, "label", settings.audio.lastPlayback.label);
            copyString(lastPlayback, "type", settings.audio.lastPlayback.type);
            copyString(lastPlayback, "source", settings.audio.lastPlayback.source);
            if (lastPlayback["resumeAfterBoot"].is<bool>()) settings.audio.lastPlayback.resumeAfterBoot = lastPlayback["resumeAfterBoot"].as<bool>();
        }
    }

    JsonObjectConst effects = object["effects"];
    if (!effects.isNull()) {
        copyString(effects, "startupFile", settings.effects.startupFile);
        if (effects["startupVolumePercent"].is<uint8_t>()) settings.effects.startupVolumePercent = effects["startupVolumePercent"].as<uint8_t>();
        copyString(effects, "alarmFile", settings.effects.alarmFile);
        if (effects["alarmVolumePercent"].is<uint8_t>()) settings.effects.alarmVolumePercent = effects["alarmVolumePercent"].as<uint8_t>();
        copyString(effects, "notificationFile", settings.effects.notificationFile);
        if (effects["notificationVolumePercent"].is<uint8_t>()) settings.effects.notificationVolumePercent = effects["notificationVolumePercent"].as<uint8_t>();
        copyString(effects, "ambientSoundFile", settings.effects.ambientSoundFile);
        if (effects["ambientVolumePercent"].is<uint8_t>()) settings.effects.ambientVolumePercent = effects["ambientVolumePercent"].as<uint8_t>();
        copyString(effects, "lowBatteryFile", settings.effects.lowBatteryFile);
        if (effects["lowBatteryVolumePercent"].is<uint8_t>()) settings.effects.lowBatteryVolumePercent = effects["lowBatteryVolumePercent"].as<uint8_t>();
        copyString(effects, "shutDownFile", settings.effects.shutDownFile);
        if (effects["shutDownVolumePercent"].is<uint8_t>()) settings.effects.shutDownVolumePercent = effects["shutDownVolumePercent"].as<uint8_t>();
        copyString(effects, "updateAvailableFile", settings.effects.updateAvailableFile);
        if (effects["updateAvailableVolumePercent"].is<uint8_t>()) settings.effects.updateAvailableVolumePercent = effects["updateAvailableVolumePercent"].as<uint8_t>();
        copyString(effects, "updateSuccessFile", settings.effects.updateSuccessFile);
        if (effects["updateSuccessVolumePercent"].is<uint8_t>()) settings.effects.updateSuccessVolumePercent = effects["updateSuccessVolumePercent"].as<uint8_t>();
    }

    JsonObjectConst oled = object["oled"];
    if (!oled.isNull()) {
        copyString(oled, "displayType", settings.oled.displayType);
        copyString(oled, "driver", settings.oled.driver);
        if (oled["enabled"].is<bool>()) settings.oled.enabled = oled["enabled"].as<bool>();
        if (oled["i2cAddress"].is<uint8_t>()) settings.oled.i2cAddress = oled["i2cAddress"].as<uint8_t>();
        if (oled["width"].is<uint8_t>()) settings.oled.width = oled["width"].as<uint8_t>();
        if (oled["height"].is<uint8_t>()) settings.oled.height = oled["height"].as<uint8_t>();
        if (oled["rotation"].is<uint16_t>()) settings.oled.rotation = oled["rotation"].as<uint16_t>();
        if (oled["sdaPin"].is<uint8_t>()) settings.oled.sdaPin = oled["sdaPin"].as<uint8_t>();
        if (oled["sclPin"].is<uint8_t>()) settings.oled.sclPin = oled["sclPin"].as<uint8_t>();
        if (oled["resetPin"].is<int8_t>()) settings.oled.resetPin = oled["resetPin"].as<int8_t>();
        if (oled["dimTimeoutSeconds"].is<uint16_t>()) settings.oled.dimTimeoutSeconds = oled["dimTimeoutSeconds"].as<uint16_t>();
        if (oled["wapeTriggerPin"].is<uint8_t>()) settings.oled.wapeTriggerPin = oled["wapeTriggerPin"].as<uint8_t>();
        copyString(oled, "wapeTriggerEvent", settings.oled.wapeTriggerEvent);
    }

    JsonObjectConst sd = object["sd"];
    if (!sd.isNull()) {
        if (sd["enabled"].is<bool>()) settings.sd.enabled = sd["enabled"].as<bool>();
        if (sd["csPin"].is<uint8_t>()) settings.sd.csPin = sd["csPin"].as<uint8_t>();
        if (sd["sckPin"].is<uint8_t>()) settings.sd.sckPin = sd["sckPin"].as<uint8_t>();
        if (sd["mosiPin"].is<uint8_t>()) settings.sd.mosiPin = sd["mosiPin"].as<uint8_t>();
        if (sd["misoPin"].is<uint8_t>()) settings.sd.misoPin = sd["misoPin"].as<uint8_t>();
    }

    JsonObjectConst device = object["device"];
    if (!device.isNull()) {
        copyString(device, "deviceName", settings.device.deviceName);
        copyString(device, "friendlyName", settings.device.friendlyName);
        copyString(device, "button1Action", settings.device.button1Action);
        copyString(device, "button2Action", settings.device.button2Action);
        copyString(device, "statusLedType", settings.device.statusLedType);
        if (device["statusLedPin"].is<uint8_t>()) settings.device.statusLedPin = device["statusLedPin"].as<uint8_t>();
        if (device["savedVolumePercent"].is<uint8_t>()) settings.device.savedVolumePercent = device["savedVolumePercent"].as<uint8_t>();
        if (device["audioMuted"].is<bool>()) settings.device.audioMuted = device["audioMuted"].as<bool>();
        if (device["lowBatterySleepEnabled"].is<bool>()) settings.device.lowBatterySleepEnabled = device["lowBatterySleepEnabled"].as<bool>();
        if (device["powerCycleFactoryResetEnabled"].is<bool>()) settings.device.powerCycleFactoryResetEnabled = device["powerCycleFactoryResetEnabled"].as<bool>();
        if (device["touchHoldFactoryResetEnabled"].is<bool>()) settings.device.touchHoldFactoryResetEnabled = device["touchHoldFactoryResetEnabled"].as<bool>();
        if (device["lowBatterySleepThresholdPercent"].is<uint8_t>()) settings.device.lowBatterySleepThresholdPercent = device["lowBatterySleepThresholdPercent"].as<uint8_t>();
        if (device["lowBatteryWakeIntervalMinutes"].is<uint16_t>()) settings.device.lowBatteryWakeIntervalMinutes = device["lowBatteryWakeIntervalMinutes"].as<uint16_t>();
    }

    JsonObjectConst ui = object["ui"];
    if (!ui.isNull()) {
        if (ui["gpioBoardAutodetect"].is<bool>()) settings.ui.gpioBoardAutodetect = ui["gpioBoardAutodetect"].as<bool>();
        copyString(ui, "gpioBoardSelection", settings.ui.gpioBoardSelection);
        copyJsonStringOrObject(ui, "peripheralDiagramLayout", settings.ui.peripheralDiagramLayout);
        if (ui["peripheralDiagramLayout"].isNull() && ui["peripheralDiagramPositions"].is<JsonObjectConst>()) {
            String serializedLayout;
            serializeJson(ui["peripheralDiagramPositions"], serializedLayout);
            settings.ui.peripheralDiagramLayout = serializedLayout;
        }
        copyJsonStringOrObject(ui, "peripheralHelperBindings", settings.ui.peripheralHelperBindings);
        copyJsonStringOrObject(ui, "peripheralProfiles", settings.ui.peripheralProfileSelections);
        copyJsonStringOrObject(ui, "motorRuntimeConfig", settings.ui.motorRuntimeConfig);
    }

    settings = sanitize(settings);
    return true;
}

bool SettingsManager::writeStringIfChanged(const char* key, const String& value) {
    if (preferences_.isKey(key) && preferences_.getString(key, "") == value) {
        return false;
    }
    preferences_.putString(key, value);
    return true;
}

bool SettingsManager::writeBoolIfChanged(const char* key, bool value) {
    if (preferences_.isKey(key) && preferences_.getBool(key, !value) == value) {
        return false;
    }
    preferences_.putBool(key, value);
    return true;
}

bool SettingsManager::writeUIntIfChanged(const char* key, uint32_t value) {
    if (preferences_.isKey(key) && preferences_.getUInt(key, value + 1) == value) {
        return false;
    }
    preferences_.putUInt(key, value);
    return true;
}

bool SettingsManager::writeIntIfChanged(const char* key, int32_t value) {
    if (preferences_.isKey(key) && preferences_.getInt(key, value + 1) == value) {
        return false;
    }
    preferences_.putInt(key, value);
    return true;
}

bool SettingsManager::writeFloatIfChanged(const char* key, float value) {
    if (preferences_.isKey(key) && fabsf(preferences_.getFloat(key, value + 1.0f) - value) < 0.0001f) {
        return false;
    }
    preferences_.putFloat(key, value);
    return true;
}

String SettingsManager::readString(const char* key, const String& fallback) {
    if (!preferences_.isKey(key)) {
        return fallback;
    }
    return preferences_.getString(key, fallback);
}

bool SettingsManager::readBool(const char* key, bool fallback) {
    if (!preferences_.isKey(key)) {
        return fallback;
    }
    return preferences_.getBool(key, fallback);
}

uint32_t SettingsManager::readUInt(const char* key, uint32_t fallback) {
    if (!preferences_.isKey(key)) {
        return fallback;
    }
    return preferences_.getUInt(key, fallback);
}

int32_t SettingsManager::readInt(const char* key, int32_t fallback) {
    if (!preferences_.isKey(key)) {
        return fallback;
    }
    return preferences_.getInt(key, fallback);
}

float SettingsManager::readFloat(const char* key, float fallback) {
    if (!preferences_.isKey(key)) {
        return fallback;
    }
    return preferences_.getFloat(key, fallback);
}
