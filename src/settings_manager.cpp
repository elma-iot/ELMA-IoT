#include "i2c_validation.h"
#include "can_contract.h"
#include <driver/gpio.h>
#include "settings_manager.h"
#include "panel_geometry.h"
#include "generated_project_defaults.h"

#include <ctype.h>
#include <math.h>
#include <memory>
#include <new>

#include "default_config.h"
#include "motor_runtime_config.h"
#include "wifi_power_policy.h"
#include "gpio_pin_policy.h"

namespace {
#if defined(CONFIG_IDF_TARGET_ESP32S3)
constexpr auto kPinChip = GpioPinPolicy::Chip::S3;
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
constexpr auto kPinChip = GpioPinPolicy::Chip::S2;
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
constexpr auto kPinChip = GpioPinPolicy::Chip::C6;
#elif defined(CONFIG_IDF_TARGET_ESP32C2)
constexpr auto kPinChip = GpioPinPolicy::Chip::C2;
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
constexpr auto kPinChip = GpioPinPolicy::Chip::C3;
#else
constexpr auto kPinChip = GpioPinPolicy::Chip::Esp32;
#endif
bool safeOutputPin(int pin) {
#if APP_COMPILED_BOARD_PROFILE_ID == 7
    if (pin == 16 || pin == 17) return false;
#elif APP_COMPILED_BOARD_PROFILE_ID >= 3 && APP_COMPILED_BOARD_PROFILE_ID <= 6 && APP_COMPILED_BOARD_PROFILE_ID != 4
    if (pin >= 33 && pin <= 37) return false;
#endif
    return GpioPinPolicy::output(kPinChip, pin);
}
constexpr char PREF_NAMESPACE[] = "notifier";
constexpr char PREF_MARKER[] = "saved";
constexpr float kLegacyEsp32BatteryCalibration = 3.866f;

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
    if (pin == 255) return true; // Disabled in the native designer.
    return safeOutputPin(pin);
}

bool statusLedUsesPin(const DeviceSettings& device, int pin) {
    return device.statusLedPin != 255 && (device.statusLedPin == pin || (device.statusLedType == "rgb" && (device.statusLedGreenPin == pin || device.statusLedBluePin == pin)));
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
    if(audio.bclkPin==255 && audio.wsPin==254)return pin==26;
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
#if APP_HAS_CAMERA
    if (sd.sdmmc) return pin == 14 || pin == 15 || pin == 2;
#endif
    return sd.csPin == pin || sd.sckPin == pin || sd.mosiPin == pin || sd.misoPin == pin || (sd.sdmmc && (pin == 15 || pin == 18));
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
           statusLedUsesPin(device, battery.chargingSensePin) || sdUsesPin(sd, battery.chargingSensePin);
}

bool configuredInputControlUsesPin(const UiSettings& ui, int pin) {
    JsonDocument bindings;
    JsonDocument profiles;
    if (deserializeJson(bindings, ui.peripheralHelperBindings) ||
        deserializeJson(profiles, ui.peripheralProfileSelections)) return false;
    for (JsonPair entry : bindings.as<JsonObject>()) {
        const String slot = entry.key().c_str();
        const bool input = slot.startsWith("input:");
        if (!input && !slot.startsWith("control:")) continue;
        const int index = slot.substring(slot.indexOf(':') + 1).toInt();
        const String profile = profiles[input ? "inputs" : "controls"][index] | "none";
        if (profile == "none") continue;
        for (JsonPair binding : entry.value().as<JsonObject>()) {
            const String signal = binding.key().c_str();
            if (signal.startsWith("LED_") || signal == "MAIN_CONTROL" || signal == "SENSITIVITY" ||
                signal == "CONTACT" || signal == "SOURCE") continue;
            const String value = binding.value().as<String>();
            bool numeric = value.length() > 0;
            for (size_t i = 0; i < value.length(); ++i) numeric &= isDigit(value[i]);
            if (numeric && value.toInt() == pin) return true;
        }
    }
    return false;
}

bool oledPinConflicts(const OledSettings& oled, const AudioSettings& audio, const BatterySettings& battery, const DeviceSettings& device, const SdSettings& sd, const UiSettings& ui) {
    String displayType = oled.displayType;
    displayType.trim();
    displayType.toLowerCase();
    if (!oled.enabled || displayType == "wape" || displayType == "panel") {
        return false;
    }

    const auto conflictsWithReservedPins = [&](int pin) {
        if (pin < 0) {
            return false;
        }

                return audioUsesPin(audio, pin) ||
               pin == battery.adcPin || pin == battery.chargingSensePin ||
               statusLedUsesPin(device, pin) || configuredInputControlUsesPin(ui, pin) ||
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
           oled.wapeTriggerPin == battery.adcPin || statusLedUsesPin(device, oled.wapeTriggerPin) || sdUsesPin(sd, oled.wapeTriggerPin)
#if defined(CONFIG_IDF_TARGET_ESP32S3)
           || oled.wapeTriggerPin == 21
#endif
        ;
}

bool sdPinConflictsWithRequiredFunctions(const SdSettings& sd, const AudioSettings& audio, const BatterySettings& battery, const DeviceSettings& device) {
    if (!sd.enabled) {
        return false;
    }

    return (sd.sdmmc && (audioUsesPin(audio, 15) || audioUsesPin(audio, 18))) || audioUsesPin(audio, sd.csPin) || audioUsesPin(audio, sd.sckPin) || audioUsesPin(audio, sd.mosiPin) || audioUsesPin(audio, sd.misoPin) ||
        sdUsesPin(sd, battery.adcPin) || sdUsesPin(sd, battery.chargingSensePin) || sdUsesPin(sd, device.statusLedPin) ||
        (device.statusLedPin != 255 && device.statusLedType == "rgb" && (sdUsesPin(sd, device.statusLedGreenPin) || sdUsesPin(sd, device.statusLedBluePin)));
}

String normalizeDisplayType(String value) {
    value.trim();
    value.toLowerCase();
    #if APP_HAS_ONBOARD_PANEL
    if (value == "panel") return value;
#endif
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
#if APP_COMPILED_BOARD_PROFILE_ID > 0
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    const char* chip="esp32s3";
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
    const char* chip="esp32c3";
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
    const char* chip="esp32c6";
#elif defined(CONFIG_IDF_TARGET_ESP32C2)
    const char* chip="esp32c2";
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
    const char* chip="esp32s2";
#else
    const char* chip="esp32";
#endif
    return String("elma-")+chip+"-board"+String(APP_COMPILED_BOARD_PROFILE_ID)+"-${version}.bin";
#endif
#if APP_SUNTON_PANEL == 1
    return "sunton-2432s028r-${version}.bin";
#elif APP_SUNTON_PANEL == 2
    return "sunton-2432s028c-${version}.bin";
#elif APP_SUNTON_PANEL == 3
    return "sunton-3248s035c-${version}.bin";
#elif defined(APP_SPK_BOARD)
    return "esp32-s3-spk-n16r8-${version}.bin";
#elif APP_HAS_CAMERA
    return "esp32-cam-${version}.bin";
#elif APP_HAS_ONBOARD_PANEL
    #if defined(BOARD_VIEWE_UEDX32480035E_WB_A)
    return "viewe-uedx32480035e-${version}.bin";
    #else
    return "viewe-uedx24320028e-${version}.bin";
    #endif
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
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

bool isSafeOutputPinForBoard(uint8_t pin) {return safeOutputPin(pin);}

bool SettingsManager::begin() {
    if (!preferences_.begin(PREF_NAMESPACE, false)) return false;
    // A failed first provision can leave many keys behind without the final
    // marker. load() ignores those keys, so reclaim only this incomplete
    // namespace; valid saved settings and other NVS users stay intact.
    if (!preferences_.getBool(PREF_MARKER, false) &&
        (preferences_.isKey("wifi_ssid") || preferences_.isKey("ui_motor") || preferences_.isKey("dev_name"))) {
        return preferences_.clear();
    }
    return true;
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
#if APP_SUNTON_PANEL
    settings.audio.enabled = false;
    settings.audio.bclkPin = 255;
    settings.audio.wsPin = 254;
    settings.audio.doutPin = 26;
#elif defined(APP_SPK_BOARD)
    settings.audio.bclkPin = 10;
    settings.audio.wsPin = 45;
    settings.audio.doutPin = 9;
#elif APP_HAS_CAMERA
    settings.audio.enabled = false;
#endif

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
#if APP_HAS_CAMERA
    settings.sd.sdmmc = true;
    settings.sd.csPin = 13;
    settings.sd.sckPin = 14;
    settings.sd.mosiPin = 15;
    settings.sd.misoPin = 2;
    settings.oled.enabled = false;
#endif
#if APP_HAS_ONBOARD_PANEL
    settings.oled.enabled = true;
    settings.oled.displayType = "panel";
    settings.oled.width = kPanelWidth;
    settings.oled.height = kPanelHeight;
#if APP_SUNTON_PANEL
    settings.sd.csPin=5;settings.sd.sckPin=18;settings.sd.mosiPin=23;settings.sd.misoPin=19;
#else
    settings.sd.csPin = 21;
    settings.sd.sckPin = 14;
    settings.sd.mosiPin = 17;
    settings.sd.misoPin = 16;
#endif
#endif

    settings.device.deviceName = uniqueDeviceName;
    settings.device.friendlyName = uniqueFriendlyName;
    settings.device.statusLedPin = DefaultConfig::STATUS_LED_PIN;
    settings.device.statusLedGreenPin = DefaultConfig::STATUS_LED_PIN;
    settings.device.statusLedBluePin = DefaultConfig::STATUS_LED_PIN;
    settings.device.statusLedType = DefaultConfig::STATUS_LED_TYPE;
#if APP_SUNTON_PANEL
    settings.device.statusLedGreenPin = 16;
    settings.device.statusLedBluePin = 17;
    settings.device.statusLedType = "rgb";
#endif
    settings.device.savedVolumePercent = DefaultConfig::DEFAULT_VOLUME_PERCENT;
    settings.device.audioMuted = DefaultConfig::DEFAULT_AUDIO_MUTED;
    settings.device.button1Action = DefaultConfig::BUTTON1_DEFAULT_ACTION;
    settings.device.button2Action = DefaultConfig::BUTTON2_DEFAULT_ACTION;
    settings.device.lowBatterySleepEnabled = DefaultConfig::LOW_BATTERY_SLEEP_ENABLED;
    settings.device.powerCycleFactoryResetEnabled = DefaultConfig::POWER_CYCLE_FACTORY_RESET_ENABLED;
    settings.device.touchHoldFactoryResetEnabled = DefaultConfig::TOUCH_HOLD_FACTORY_RESET_ENABLED;
    settings.device.lowBatterySleepThresholdPercent = DefaultConfig::LOW_BATTERY_SLEEP_THRESHOLD_PERCENT;
    settings.device.lowBatteryWakeIntervalMinutes = DefaultConfig::LOW_BATTERY_WAKE_INTERVAL_MINUTES;
    settings.ui.gpioSafetyOverride = false;
    settings.ui.gpioBoardAutodetect = true;
    settings.ui.language = APP_COMPILED_LANGUAGE_CODE;
    settings.ui.theme = APP_COMPILED_THEME_CODE;
    settings.ui.gpioBoardSelection = "";
    settings.ui.peripheralDiagramLayout = "{}";
    settings.ui.peripheralHelperBindings = defaultPeripheralHelperBindings();
    settings.ui.peripheralProfileSelections = defaultPeripheralProfileSelections();
    settings.ui.motorRuntimeConfig = defaultMotorRuntimeConfig();
    settings.usingSavedSettings = false;
    JsonDocument compiled;String error;
    if (deserializeJson(compiled,ELMA_COMPILED_PROJECT_DEFAULTS)==DeserializationError::Ok && compiled.is<JsonObject>()) updateFromJson(settings,compiled.as<JsonVariantConst>(),error);
    return settings;
}

void SettingsManager::sanitizeInPlace(SettingsBundle& settings) const {
    if(settings.ota.owner=="elma-iot" && settings.ota.repository=="ELMA-IoT" && settings.ota.manifestUrl.isEmpty())
        settings.ota.repository="ELMA-IoT-Firmware";
#if defined(APP_SPK_BOARD) || APP_SUNTON_PANEL
    // These are physically fitted chips, not optional external peripherals.
    settings.audio.enabled=true;settings.sd.enabled=true;settings.sd.sdmmc=false;
    JsonDocument profiles,bindings;
    deserializeJson(profiles,settings.ui.peripheralProfileSelections);
    deserializeJson(bindings,settings.ui.peripheralHelperBindings);
#if defined(APP_SPK_BOARD)
    settings.audio.bclkPin=10;settings.audio.wsPin=45;settings.audio.doutPin=9;
    settings.sd.csPin=2;settings.sd.sckPin=11;settings.sd.mosiPin=3;settings.sd.misoPin=12;
    settings.device.statusLedPin=21;settings.device.statusLedType="neopixel";
    profiles["audioProfiles"][0]="spk-ns4168";profiles["audioProfile"]="spk-ns4168";
    profiles["audioInProfiles"][0]="spk-dual-mic";profiles["audioInProfile"]="spk-dual-mic";
    bindings["audioIn:0"]["SCK"]=39;bindings["audioIn:0"]["WS"]=40;bindings["audioIn:0"]["SD"]=38;
#else
    settings.audio.bclkPin=255;settings.audio.wsPin=254;settings.audio.doutPin=26;
    settings.sd.csPin=5;settings.sd.sckPin=18;settings.sd.mosiPin=23;settings.sd.misoPin=19;
    profiles["audioProfiles"][0]="sunton-speaker";profiles["audioProfile"]="sunton-speaker";
    settings.oled.enabled=true;settings.oled.displayType="panel";
    settings.oled.width=kPanelWidth;settings.oled.height=kPanelHeight;
    profiles["displayProfiles"][0]="viewe-onboard-lcd";profiles["displayProfile"]="viewe-onboard-lcd";
    JsonArray sensors=profiles["sensors"].is<JsonArray>() ? profiles["sensors"].as<JsonArray>() : profiles["sensors"].to<JsonArray>();
    int ldrIndex=-1;
    for(size_t i=0;i<sensors.size();++i) if(sensors[i].as<String>()=="ldr") {ldrIndex=i;break;}
    if(ldrIndex<0){
      if(sensors.size() && sensors[0].as<String>()=="none") {sensors[0]="ldr";ldrIndex=0;}
      else {ldrIndex=sensors.size();sensors.add("ldr");}
    }
    bindings[String("sensor:")+ldrIndex]["SIG"]=34;

#endif
    profiles["storage"][0]="microsd-spi";
    bindings["storage:0"]["CS"]=settings.sd.csPin;bindings["storage:0"]["SCK"]=settings.sd.sckPin;
    bindings["storage:0"]["MOSI"]=settings.sd.mosiPin;bindings["storage:0"]["MISO"]=settings.sd.misoPin;
    settings.ui.peripheralProfileSelections="";serializeJson(profiles,settings.ui.peripheralProfileSelections);
    settings.ui.peripheralHelperBindings="";serializeJson(bindings,settings.ui.peripheralHelperBindings);
#endif

#if APP_HAS_ONBOARD_PANEL && !APP_SUNTON_PANEL
    // The VIEWE slot is soldered to this board; migrate old optional/disabled settings.
    settings.sd.enabled=true;settings.sd.sdmmc=true;
    settings.sd.csPin=21;settings.sd.sckPin=14;settings.sd.mosiPin=17;settings.sd.misoPin=16;
    JsonDocument panelProfiles;
    deserializeJson(panelProfiles,settings.ui.peripheralProfileSelections);
    panelProfiles["storage"][0]="viewe-sdmmc";
    panelProfiles["communication"][0]="viewe-ms1285";
    String audioProfile=panelProfiles["audioProfiles"][0]|panelProfiles["audioProfile"]|"none";
    if(audioProfile=="none"||audioProfile.isEmpty()||audioProfile.indexOf("buzzer")>=0||audioProfile.indexOf("bluetooth")>=0){
        settings.audio.enabled=false;
        if(audioProfile=="none"||audioProfile.isEmpty()){panelProfiles["audioProfiles"][0]="none";panelProfiles["audioProfile"]="none";}
    }
    settings.ui.peripheralProfileSelections="";serializeJson(panelProfiles,settings.ui.peripheralProfileSelections);
#endif
#if APP_HAS_CAMERA && !defined(APP_SPK_BOARD)
    settings.sd.enabled=true;settings.sd.sdmmc=true;
    settings.sd.csPin=13;settings.sd.sckPin=14;settings.sd.mosiPin=15;settings.sd.misoPin=2;
    JsonDocument cameraProfiles;
    deserializeJson(cameraProfiles,settings.ui.peripheralProfileSelections);
    cameraProfiles["storage"][0]="camera-sdmmc";
    settings.ui.peripheralProfileSelections="";serializeJson(cameraProfiles,settings.ui.peripheralProfileSelections);
#endif
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
    settings.ui.language.trim();
    settings.ui.language.toLowerCase();
    settings.ui.theme.trim();
    settings.ui.theme.toLowerCase();
    const String supportedLanguages = "|en|es|zh|hi|ar|pt|bn|ru|ja|de|fr|ko|tr|it|id|pl|uk|vi|th|fa|";
    if (settings.ui.language.isEmpty() || supportedLanguages.indexOf("|" + settings.ui.language + "|") < 0) {
        settings.ui.language = "en";
    }
    if (settings.ui.theme != "automatic" && settings.ui.theme != "light" && settings.ui.theme != "dark") {
        settings.ui.theme = "automatic";
    }
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
    if(settings.ota.owner=="elma-iot" && settings.ota.repository=="ELMA-IoT-Firmware")settings.ota.assetTemplate=defaultOtaAssetTemplate();
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
    if (settings.device.statusLedType != "regular" && settings.device.statusLedType != "rgb" && settings.device.statusLedType != "neopixel") {
        settings.device.statusLedType = DefaultConfig::STATUS_LED_TYPE;
    }
    if (!isValidStatusLedPin(settings.device.statusLedGreenPin)) settings.device.statusLedGreenPin = DefaultConfig::STATUS_LED_PIN;
    if (!isValidStatusLedPin(settings.device.statusLedBluePin)) settings.device.statusLedBluePin = DefaultConfig::STATUS_LED_PIN;
    if (settings.device.statusLedPin != 255 && settings.device.statusLedType == "rgb" && (settings.device.statusLedGreenPin == 255 || settings.device.statusLedBluePin == 255 || settings.device.statusLedPin == settings.device.statusLedGreenPin || settings.device.statusLedPin == settings.device.statusLedBluePin || settings.device.statusLedGreenPin == settings.device.statusLedBluePin)) settings.device.statusLedType = "regular";
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
#if APP_SUNTON_PANEL
    const bool onboardDac=settings.audio.bclkPin==255 && settings.audio.wsPin==254 && settings.audio.doutPin==26;
#else
    const bool onboardDac=false;
#endif
    if(!onboardDac){
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
#if !APP_HAS_ONBOARD_PANEL && !APP_HAS_CAMERA
    settings.sd.sdmmc = false;
#endif
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
    if (settings.device.statusLedPin != 255 && (sdUsesPin(settings.sd, settings.device.statusLedPin) || (settings.device.statusLedType == "rgb" && (sdUsesPin(settings.sd, settings.device.statusLedGreenPin) || sdUsesPin(settings.sd, settings.device.statusLedBluePin))))) {
        settings.device.statusLedPin = DefaultConfig::STATUS_LED_PIN;
        settings.device.statusLedType = DefaultConfig::STATUS_LED_TYPE;
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
    const bool panel = settings.oled.displayType == "panel";
    if(settings.oled.interfaceMode!="lvgl" && settings.oled.interfaceMode!="text")settings.oled.interfaceMode="lvgl";
    settings.oled.brightness = min<uint8_t>(settings.oled.brightness, 100);
    settings.oled.width = clampValue<uint16_t>(settings.oled.width, static_cast<uint8_t>(64), static_cast<uint8_t>(128));
    settings.oled.height = clampValue<uint16_t>(settings.oled.height, static_cast<uint8_t>(32), static_cast<uint8_t>(64));
    if (panel) { settings.oled.width = kPanelWidth; settings.oled.height = kPanelHeight; }
    if (settings.oled.rotation != 0 && settings.oled.rotation != 90 && settings.oled.rotation != 180 && settings.oled.rotation != 270) {
        settings.oled.rotation = 0;
    }
    settings.oled.dimTimeoutSeconds = clampValue<uint16_t>(settings.oled.dimTimeoutSeconds, static_cast<uint16_t>(0), static_cast<uint16_t>(3600));
    if (!panel && (!safeOutputPin(settings.oled.sdaPin) || !safeOutputPin(settings.oled.sclPin) ||
        (settings.oled.resetPin >= 0 && !safeOutputPin(settings.oled.resetPin)) ||
        oledPinConflicts(settings.oled, settings.audio, settings.battery, settings.device, settings.sd, settings.ui))) {
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

}

SettingsBundle SettingsManager::load() {
    SettingsBundle settings = defaults();
    settings.usingSavedSettings = readBool(PREF_MARKER, false);
    if (!settings.usingSavedSettings) {
        settings.mqtt.clientId = settings.device.deviceName;
        sanitizeInPlace(settings);return settings;
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

    settings.oled.brightness = readUInt("lcd_light", settings.oled.brightness);
    settings.oled.interfaceMode = readString("lcd_ui", settings.oled.interfaceMode);
    settings.oled.touchEnabled = readBool("lcd_touch", settings.oled.touchEnabled);
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

    settings.sd.sdmmc = readBool("sd_mmc", settings.sd.sdmmc);
    settings.sd.enabled = readBool("sd_en", settings.sd.enabled);
    settings.sd.csPin = readUInt("sd_cs", settings.sd.csPin);
    settings.sd.sckPin = readUInt("sd_sck", settings.sd.sckPin);
    settings.sd.mosiPin = readUInt("sd_mosi", settings.sd.mosiPin);
    settings.sd.misoPin = readUInt("sd_miso", settings.sd.misoPin);

    settings.device.deviceName = readString("dev_name", settings.device.deviceName);
    settings.device.friendlyName = readString("dev_friendly", settings.device.friendlyName);
    settings.device.statusLedPin = readUInt("dev_led", settings.device.statusLedPin);
    settings.device.statusLedGreenPin = readUInt("dev_led_g", settings.device.statusLedGreenPin);
    settings.device.statusLedBluePin = readUInt("dev_led_b", settings.device.statusLedBluePin);
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
    settings.ui.gpioSafetyOverride = readBool("ui_gpio_ovr", settings.ui.gpioSafetyOverride);
    settings.ui.gpioBoardAutodetect = readBool("ui_gpio_auto", settings.ui.gpioBoardAutodetect);
    // A newly compiled APK locale becomes the default once. Preserve later
    // explicit English/selected-language choices across ordinary restarts.
    const String compiledLanguage = APP_COMPILED_LANGUAGE_CODE;
    if (readString("ui_build_lang", "") != compiledLanguage) {
        settings.ui.language = compiledLanguage;
        writeStringIfChanged("ui_lang", compiledLanguage);
        writeStringIfChanged("ui_build_lang", compiledLanguage);
    } else {
        settings.ui.language = readString("ui_lang", settings.ui.language);
    }
    const String compiledTheme = APP_COMPILED_THEME_CODE;
    if (readString("ui_build_theme", "") != compiledTheme) {
        settings.ui.theme = compiledTheme;
        writeStringIfChanged("ui_theme", compiledTheme);
        writeStringIfChanged("ui_build_theme", compiledTheme);
    } else {
        settings.ui.theme = readString("ui_theme", settings.ui.theme);
    }
    settings.ui.gpioBoardSelection = readString("ui_gpio_sel", settings.ui.gpioBoardSelection);
    settings.ui.peripheralDiagramLayout = readString("ui_diag", settings.ui.peripheralDiagramLayout);
    settings.ui.peripheralHelperBindings = readString("ui_helpers", settings.ui.peripheralHelperBindings);
    settings.ui.peripheralProfileSelections = readString("ui_profiles", settings.ui.peripheralProfileSelections);
    settings.ui.motorRuntimeConfig = storedMotorRuntimeConfig.isEmpty() ? defaultMotorRuntimeConfig() : storedMotorRuntimeConfig;

    sanitizeInPlace(settings);
    settings.ui.motorRuntimeConfig = storedMotorRuntimeConfig.isEmpty() ? defaultMotorRuntimeConfig() : storedMotorRuntimeConfig;
    settings.mqtt.clientId = fallbackIfEmpty(settings.mqtt.clientId, settings.device.deviceName);
    settings.usingSavedSettings = true;
    return settings;
}

bool SettingsManager::save(const SettingsBundle& settings) {
    writeFailed_ = false;
    // Also recover when provisioning is retried without a device reboot.
    if (!preferences_.getBool(PREF_MARKER, false) &&
        (preferences_.isKey("wifi_ssid") || preferences_.isKey("ui_motor") || preferences_.isKey("dev_name")) &&
        !preferences_.clear()) {
        return false;
    }
    const std::unique_ptr<SettingsBundle> sanitizedStorage(new (std::nothrow) SettingsBundle(settings));
    if (!sanitizedStorage) return false;
    sanitizeInPlace(*sanitizedStorage);
    const SettingsBundle& sanitized = *sanitizedStorage;
    const String rawMotorRuntimeConfig = settings.ui.motorRuntimeConfig.isEmpty()
        ? defaultMotorRuntimeConfig()
        : settings.ui.motorRuntimeConfig;
    // SettingsBundle is large on the ESP32-C3 loop task stack. Keep the
    // comparison copy on the heap while provisioning.
    const std::unique_ptr<SettingsBundle> baselineStorage(new (std::nothrow) SettingsBundle(defaults()));
    if (!baselineStorage) return false;
    const SettingsBundle& baseline = *baselineStorage;
    bool changed = false;
    bool removingDefaults = true;
    auto removeDefault = [&](const char* key) {
        if (writeFailed_ || !preferences_.isKey(key)) return false;
        if (!preferences_.remove(key)) { writeFailed_ = true; return false; }
        return true;
    };
    auto storeString = [&](const char* key, const String& value, const String& initial) {
        return value == initial ? removeDefault(key) :
            removingDefaults ? false : writeStringIfChanged(key, value);
    };
    auto storeBool = [&](const char* key, bool value, bool initial) {
        return value == initial ? removeDefault(key) :
            removingDefaults ? false : writeBoolIfChanged(key, value);
    };
    auto storeUInt = [&](const char* key, uint32_t value, uint32_t initial) {
        return value == initial ? removeDefault(key) :
            removingDefaults ? false : writeUIntIfChanged(key, value);
    };
    auto storeInt = [&](const char* key, int32_t value, int32_t initial) {
        return value == initial ? removeDefault(key) :
            removingDefaults ? false : writeIntIfChanged(key, value);
    };
    auto storeFloat = [&](const char* key, float value, float initial) {
        return value == initial ? removeDefault(key) :
            removingDefaults ? false : writeFloatIfChanged(key, value);
    };
    auto writeFields = [&]() {
        changed |= storeString("wifi_ssid", sanitized.wifi.ssid, baseline.wifi.ssid);
        changed |= storeString("wifi_pass", sanitized.wifi.password, baseline.wifi.password);
        changed |= storeString("wifi_apssid", sanitized.wifi.apSsid, baseline.wifi.apSsid);
        changed |= storeString("wifi_appass", sanitized.wifi.apPassword, baseline.wifi.apPassword);
        changed |= storeBool("wifi_apfb", sanitized.wifi.apFallbackEnabled, baseline.wifi.apFallbackEnabled);
        changed |= storeBool("wifi_static", sanitized.wifi.useStaticIp, baseline.wifi.useStaticIp);
        changed |= storeFloat("wifi_sta_tx", sanitized.wifi.staTxPowerDbm, baseline.wifi.staTxPowerDbm);
        changed |= storeFloat("wifi_ap_tx", sanitized.wifi.apTxPowerDbm, baseline.wifi.apTxPowerDbm);
        changed |= storeString("wifi_ip", sanitized.wifi.staticIp, baseline.wifi.staticIp);
        changed |= storeString("wifi_gw", sanitized.wifi.gateway, baseline.wifi.gateway);
        changed |= storeString("wifi_sub", sanitized.wifi.subnet, baseline.wifi.subnet);
        changed |= storeString("wifi_dns1", sanitized.wifi.dns1, baseline.wifi.dns1);
        changed |= storeString("wifi_dns2", sanitized.wifi.dns2, baseline.wifi.dns2);

        changed |= storeString("mqtt_host", sanitized.mqtt.host, baseline.mqtt.host);
        changed |= storeUInt("mqtt_port", sanitized.mqtt.port, baseline.mqtt.port);
        changed |= storeString("mqtt_user", sanitized.mqtt.username, baseline.mqtt.username);
        changed |= storeString("mqtt_pass", sanitized.mqtt.password, baseline.mqtt.password);
        changed |= storeString("mqtt_cid", fallbackIfEmpty(sanitized.mqtt.clientId, sanitized.device.deviceName), fallbackIfEmpty(baseline.mqtt.clientId, baseline.device.deviceName));
        changed |= storeString("mqtt_base", sanitized.mqtt.baseTopic, baseline.mqtt.baseTopic);
        changed |= storeBool("mqtt_disc", sanitized.mqtt.discoveryEnabled, baseline.mqtt.discoveryEnabled);

        changed |= storeString("ota_owner", sanitized.ota.owner, baseline.ota.owner);
        changed |= storeString("ota_repo", sanitized.ota.repository, baseline.ota.repository);
        changed |= storeString("ota_chan", sanitized.ota.channel, baseline.ota.channel);
        changed |= storeString("ota_asset", sanitized.ota.assetTemplate, baseline.ota.assetTemplate);
        changed |= storeString("ota_manifest", sanitized.ota.manifestUrl, baseline.ota.manifestUrl);
        changed |= storeBool("ota_tls", sanitized.ota.allowInsecureTls, baseline.ota.allowInsecureTls);
        changed |= storeBool("ota_auto", sanitized.ota.autoCheck, baseline.ota.autoCheck);
        changed |= storeBool("ota_upd", sanitized.ota.autoUpdate, baseline.ota.autoUpdate);

        changed |= storeUInt("bat_r1", sanitized.battery.dividerR1Ohms, baseline.battery.dividerR1Ohms);
        changed |= storeUInt("bat_r2", sanitized.battery.dividerR2Ohms, baseline.battery.dividerR2Ohms);
        changed |= storeFloat("bat_vmax", sanitized.battery.dividerMaxVin, baseline.battery.dividerMaxVin);
        changed |= storeFloat("bat_cal", sanitized.battery.calibrationMultiplier, baseline.battery.calibrationMultiplier);
        changed |= storeUInt("bat_pin", sanitized.battery.adcPin, baseline.battery.adcPin);
        changed |= storeFloat("bat_meas", sanitized.battery.measuredVoltage, baseline.battery.measuredVoltage);
        changed |= storeUInt("bat_chg", sanitized.battery.chargingSensePin, baseline.battery.chargingSensePin);
        changed |= storeUInt("bat_int", sanitized.battery.updateIntervalMs, baseline.battery.updateIntervalMs);
        changed |= storeUInt("bat_win", sanitized.battery.movingAverageWindowSize, baseline.battery.movingAverageWindowSize);

        changed |= storeBool("web_auth", sanitized.webAuth.enabled, baseline.webAuth.enabled);
        changed |= storeString("web_user", sanitized.webAuth.username, baseline.webAuth.username);
        changed |= storeString("web_pass", sanitized.webAuth.password, baseline.webAuth.password);

        changed |= storeBool("aud_en", sanitized.audio.enabled, baseline.audio.enabled);
        changed |= storeBool("aud_rem", sanitized.audio.rememberLastPlayed, baseline.audio.rememberLastPlayed);
        changed |= storeString("aud_eq", sanitized.audio.equalizerPreset, baseline.audio.equalizerPreset);
        changed |= storeInt("aud_eq_lo", sanitized.audio.equalizerLowDb, baseline.audio.equalizerLowDb);
        changed |= storeInt("aud_eq_mid", sanitized.audio.equalizerPresenceDb, baseline.audio.equalizerPresenceDb);
        changed |= storeInt("aud_eq_hi", sanitized.audio.equalizerHighDb, baseline.audio.equalizerHighDb);
        changed |= storeUInt("aud_dout", sanitized.audio.doutPin, baseline.audio.doutPin);
        changed |= storeUInt("aud_ws", sanitized.audio.wsPin, baseline.audio.wsPin);
        changed |= storeUInt("aud_bclk", sanitized.audio.bclkPin, baseline.audio.bclkPin);
        changed |= storeString("aud_lp_url", sanitized.audio.lastPlayback.url, baseline.audio.lastPlayback.url);
        changed |= storeString("aud_lp_lbl", sanitized.audio.lastPlayback.label, baseline.audio.lastPlayback.label);
        changed |= storeString("aud_lp_type", sanitized.audio.lastPlayback.type, baseline.audio.lastPlayback.type);
        changed |= storeString("aud_lp_src", sanitized.audio.lastPlayback.source, baseline.audio.lastPlayback.source);
        changed |= storeBool("aud_lp_res", sanitized.audio.lastPlayback.resumeAfterBoot, baseline.audio.lastPlayback.resumeAfterBoot);

        changed |= storeString("eff_start", sanitized.effects.startupFile, baseline.effects.startupFile);
        changed |= storeUInt("eff_st_vol", sanitized.effects.startupVolumePercent, baseline.effects.startupVolumePercent);
        changed |= storeString("eff_alarm", sanitized.effects.alarmFile, baseline.effects.alarmFile);
        changed |= storeUInt("eff_al_vol", sanitized.effects.alarmVolumePercent, baseline.effects.alarmVolumePercent);
        changed |= storeString("eff_note", sanitized.effects.notificationFile, baseline.effects.notificationFile);
        changed |= storeUInt("eff_no_vol", sanitized.effects.notificationVolumePercent, baseline.effects.notificationVolumePercent);
        changed |= storeString("eff_amb", sanitized.effects.ambientSoundFile, baseline.effects.ambientSoundFile);
        changed |= storeUInt("eff_amb_vol", sanitized.effects.ambientVolumePercent, baseline.effects.ambientVolumePercent);
        changed |= storeString("eff_low", sanitized.effects.lowBatteryFile, baseline.effects.lowBatteryFile);
        changed |= storeUInt("eff_lo_vol", sanitized.effects.lowBatteryVolumePercent, baseline.effects.lowBatteryVolumePercent);
        changed |= storeString("eff_down", sanitized.effects.shutDownFile, baseline.effects.shutDownFile);
        changed |= storeUInt("eff_sh_vol", sanitized.effects.shutDownVolumePercent, baseline.effects.shutDownVolumePercent);
        changed |= storeString("eff_up_av", sanitized.effects.updateAvailableFile, baseline.effects.updateAvailableFile);
        changed |= storeUInt("eff_ua_vol", sanitized.effects.updateAvailableVolumePercent, baseline.effects.updateAvailableVolumePercent);
        changed |= storeString("eff_up_ok", sanitized.effects.updateSuccessFile, baseline.effects.updateSuccessFile);
        changed |= storeUInt("eff_us_vol", sanitized.effects.updateSuccessVolumePercent, baseline.effects.updateSuccessVolumePercent);

        changed |= storeUInt("lcd_light", sanitized.oled.brightness, baseline.oled.brightness);
        changed |= storeString("lcd_ui", sanitized.oled.interfaceMode, baseline.oled.interfaceMode);
        changed |= storeBool("lcd_touch", sanitized.oled.touchEnabled, baseline.oled.touchEnabled);
        changed |= storeBool("oled_en", sanitized.oled.enabled, baseline.oled.enabled);
        changed |= storeString("oled_mode", sanitized.oled.displayType, baseline.oled.displayType);
        changed |= storeString("oled_drv", sanitized.oled.driver, baseline.oled.driver);
        changed |= storeUInt("oled_addr", sanitized.oled.i2cAddress, baseline.oled.i2cAddress);
        changed |= storeUInt("oled_w", sanitized.oled.width, baseline.oled.width);
        changed |= storeUInt("oled_h", sanitized.oled.height, baseline.oled.height);
        changed |= storeUInt("oled_rot", sanitized.oled.rotation, baseline.oled.rotation);
        changed |= storeUInt("oled_sda", sanitized.oled.sdaPin, baseline.oled.sdaPin);
        changed |= storeUInt("oled_scl", sanitized.oled.sclPin, baseline.oled.sclPin);
        changed |= storeInt("oled_rst", sanitized.oled.resetPin, baseline.oled.resetPin);
        changed |= storeUInt("oled_wape_pin", sanitized.oled.wapeTriggerPin, baseline.oled.wapeTriggerPin);
        changed |= storeString("oled_wape_evt", sanitized.oled.wapeTriggerEvent, baseline.oled.wapeTriggerEvent);

        changed |= storeBool("sd_mmc", sanitized.sd.sdmmc, baseline.sd.sdmmc);
        changed |= storeBool("sd_en", sanitized.sd.enabled, baseline.sd.enabled);
        changed |= storeUInt("sd_cs", sanitized.sd.csPin, baseline.sd.csPin);
        changed |= storeUInt("sd_sck", sanitized.sd.sckPin, baseline.sd.sckPin);
        changed |= storeUInt("sd_mosi", sanitized.sd.mosiPin, baseline.sd.mosiPin);
        changed |= storeUInt("sd_miso", sanitized.sd.misoPin, baseline.sd.misoPin);

        changed |= storeString("dev_name", sanitized.device.deviceName, baseline.device.deviceName);
        changed |= storeString("dev_friendly", sanitized.device.friendlyName, baseline.device.friendlyName);
        changed |= storeUInt("dev_led", sanitized.device.statusLedPin, baseline.device.statusLedPin);
        changed |= storeUInt("dev_led_g", sanitized.device.statusLedGreenPin, baseline.device.statusLedGreenPin);
        changed |= storeUInt("dev_led_b", sanitized.device.statusLedBluePin, baseline.device.statusLedBluePin);
        changed |= storeString("dev_led_type", sanitized.device.statusLedType, baseline.device.statusLedType);
        changed |= storeUInt("dev_vol", sanitized.device.savedVolumePercent, baseline.device.savedVolumePercent);
        changed |= storeBool("dev_muted", sanitized.device.audioMuted, baseline.device.audioMuted);
        changed |= storeString("dev_btn1", sanitized.device.button1Action, baseline.device.button1Action);
        changed |= storeString("dev_btn2", sanitized.device.button2Action, baseline.device.button2Action);
        changed |= storeBool("dev_lbs_en", sanitized.device.lowBatterySleepEnabled, baseline.device.lowBatterySleepEnabled);
        changed |= storeBool("dev_pcf_reset", sanitized.device.powerCycleFactoryResetEnabled, baseline.device.powerCycleFactoryResetEnabled);
        changed |= storeBool("dev_thf_reset", sanitized.device.touchHoldFactoryResetEnabled, baseline.device.touchHoldFactoryResetEnabled);
        changed |= storeUInt("dev_lbs_pct", sanitized.device.lowBatterySleepThresholdPercent, baseline.device.lowBatterySleepThresholdPercent);
        changed |= storeUInt("dev_lbs_wk", sanitized.device.lowBatteryWakeIntervalMinutes, baseline.device.lowBatteryWakeIntervalMinutes);
        changed |= storeBool("ui_gpio_ovr", sanitized.ui.gpioSafetyOverride, baseline.ui.gpioSafetyOverride);
        changed |= storeBool("ui_gpio_auto", sanitized.ui.gpioBoardAutodetect, baseline.ui.gpioBoardAutodetect);
        changed |= storeString("ui_lang", sanitized.ui.language, baseline.ui.language);
        if (!removingDefaults) changed |= writeStringIfChanged("ui_build_lang", APP_COMPILED_LANGUAGE_CODE);
        changed |= storeString("ui_theme", sanitized.ui.theme, baseline.ui.theme);
        changed |= storeString("ui_gpio_sel", sanitized.ui.gpioBoardSelection, baseline.ui.gpioBoardSelection);
        changed |= storeString("ui_diag", sanitized.ui.peripheralDiagramLayout, baseline.ui.peripheralDiagramLayout);
        changed |= storeString("ui_helpers", sanitized.ui.peripheralHelperBindings, baseline.ui.peripheralHelperBindings);
        changed |= storeString("ui_profiles", sanitized.ui.peripheralProfileSelections, baseline.ui.peripheralProfileSelections);
        changed |= storeString("ui_motor", rawMotorRuntimeConfig, defaultMotorRuntimeConfig());
    };
    writeFields(); // Free keys whose values are already supplied by defaults().
    removingDefaults = false;
    if (!writeFailed_) writeFields();
    if (!writeFailed_) changed |= writeBoolIfChanged(PREF_MARKER, true);
    const bool saved = !writeFailed_;
    writeFailed_ = false;
    return saved;
}

bool SettingsManager::saveAudioEqualizer(const AudioSettings& audio) {
    writeFailed_ = false;
    AudioSettings sanitized = audio;
    normalizeEqualizer(sanitized);
    bool changed = false;
    changed |= writeStringIfChanged("aud_eq", sanitized.equalizerPreset);
    changed |= writeIntIfChanged("aud_eq_lo", sanitized.equalizerLowDb);
    changed |= writeIntIfChanged("aud_eq_mid", sanitized.equalizerPresenceDb);
    changed |= writeIntIfChanged("aud_eq_hi", sanitized.equalizerHighDb);
    if (!writeFailed_) changed |= writeBoolIfChanged(PREF_MARKER, true);
    const bool saved = !writeFailed_;
    writeFailed_ = false;
    return saved;
}

bool SettingsManager::reset() {
    return preferences_.clear();
}

void SettingsManager::toJson(const SettingsBundle& settings, JsonObject root, const char* section, bool editorState) const {
    auto writeJsonValue = [](JsonObject parent, const char* key, const String& serialized) {
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
        DynamicJsonDocument parsed(serialized.length() * 2U + 128U);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        if (!deserializeJson(parsed, serialized)) {
            parent[key].set(parsed.as<JsonVariantConst>());
        } else {
            parent[key] = serialized;
        }
    };
    if(!section || strcmp(section,"wifi") == 0){
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

    }
    if(!section || strcmp(section,"mqtt") == 0){
    JsonObject mqtt = root["mqtt"].to<JsonObject>();
    mqtt["host"] = settings.mqtt.host;
    mqtt["port"] = settings.mqtt.port;
    mqtt["username"] = settings.mqtt.username;
    mqtt["password"] = settings.mqtt.password;
    mqtt["clientId"] = settings.mqtt.clientId;
    mqtt["baseTopic"] = settings.mqtt.baseTopic;
    mqtt["discoveryEnabled"] = settings.mqtt.discoveryEnabled;

    }
    if(!section || strcmp(section,"ota") == 0){
    JsonObject ota = root["ota"].to<JsonObject>();
    ota["owner"] = settings.ota.owner;
    ota["repository"] = settings.ota.repository;
    ota["channel"] = settings.ota.channel;
    ota["assetTemplate"] = settings.ota.assetTemplate;
    ota["manifestUrl"] = settings.ota.manifestUrl;
    ota["allowInsecureTls"] = settings.ota.allowInsecureTls;
    ota["autoCheck"] = settings.ota.autoCheck;
    ota["autoUpdate"] = settings.ota.autoUpdate;

    }
    if(!section || strcmp(section,"battery") == 0){
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

    }
    if(!section || strcmp(section,"webAuth") == 0){
    JsonObject webAuth = root["webAuth"].to<JsonObject>();
    webAuth["enabled"] = settings.webAuth.enabled;
    webAuth["username"] = settings.webAuth.username;
    webAuth["password"] = settings.webAuth.password;

    }
    if(!section || strcmp(section,"audio") == 0){
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

    }
    if(!section || strcmp(section,"effects") == 0){
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

    }
    if(!section || strcmp(section,"oled") == 0){
    JsonObject oled = root["oled"].to<JsonObject>();
    oled["enabled"] = settings.oled.enabled;
    oled["interfaceMode"] = settings.oled.interfaceMode;
    oled["touchEnabled"] = settings.oled.touchEnabled;
    oled["brightness"] = settings.oled.brightness;
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

    }
    if(!section || strcmp(section,"sd") == 0){
    JsonObject sd = root["sd"].to<JsonObject>();
    sd["enabled"] = settings.sd.enabled;
    sd["sdmmc"] = settings.sd.sdmmc;
    sd["csPin"] = settings.sd.csPin;
    sd["sckPin"] = settings.sd.sckPin;
    sd["mosiPin"] = settings.sd.mosiPin;
    sd["misoPin"] = settings.sd.misoPin;

    }
    if(!section || strcmp(section,"device") == 0){
    JsonObject device = root["device"].to<JsonObject>();
    device["deviceName"] = settings.device.deviceName;
    device["friendlyName"] = settings.device.friendlyName;
    device["statusLedPin"] = settings.device.statusLedPin == 255 ? -1 : static_cast<int>(settings.device.statusLedPin);
    device["statusLedGreenPin"] = settings.device.statusLedGreenPin;
    device["statusLedBluePin"] = settings.device.statusLedBluePin;
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

    }
    if(!section || strcmp(section,"ui") == 0){
    JsonObject ui = root["ui"].to<JsonObject>();
    ui["language"] = settings.ui.language;
    ui["theme"] = settings.ui.theme;
    ui["gpioSafetyOverride"] = settings.ui.gpioSafetyOverride;
    ui["gpioBoardAutodetect"] = settings.ui.gpioBoardAutodetect;
    ui["gpioBoardSelection"] = settings.ui.gpioBoardSelection;
    if(editorState)writeJsonValue(ui, "peripheralDiagramLayout", settings.ui.peripheralDiagramLayout);

    writeJsonValue(ui, "peripheralHelperBindings", settings.ui.peripheralHelperBindings);
    writeJsonValue(ui, "peripheralProfiles", settings.ui.peripheralProfileSelections);
    if(editorState)writeJsonValue(ui,"recordedMelodies",settings.ui.recordedMelodies);
    if(editorState)writeJsonValue(ui, "motorRuntimeConfig", settings.ui.motorRuntimeConfig);

    }
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
        copyString(oled, "interfaceMode", settings.oled.interfaceMode);
        copyString(oled, "displayType", settings.oled.displayType);
        copyString(oled, "driver", settings.oled.driver);
        if (oled["brightness"].is<uint8_t>()) settings.oled.brightness = oled["brightness"].as<uint8_t>();
        if (oled["touchEnabled"].is<bool>()) settings.oled.touchEnabled = oled["touchEnabled"].as<bool>();
        if (oled["enabled"].is<bool>()) settings.oled.enabled = oled["enabled"].as<bool>();
        if (oled["i2cAddress"].is<uint8_t>()) settings.oled.i2cAddress = oled["i2cAddress"].as<uint8_t>();
        if (oled["width"].is<uint16_t>()) settings.oled.width = oled["width"].as<uint16_t>();
        if (oled["height"].is<uint16_t>()) settings.oled.height = oled["height"].as<uint16_t>();
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
        if (sd["sdmmc"].is<bool>()) settings.sd.sdmmc = sd["sdmmc"].as<bool>();
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
        if (device["statusLedPin"].is<int>() && device["statusLedPin"].as<int>() == -1) settings.device.statusLedPin = 255;
        else if (device["statusLedPin"].is<uint8_t>()) settings.device.statusLedPin = device["statusLedPin"].as<uint8_t>();
        if (device["statusLedGreenPin"].is<int>() && device["statusLedGreenPin"].as<int>() == -1) settings.device.statusLedGreenPin = 255;
        else if (device["statusLedGreenPin"].is<uint8_t>()) settings.device.statusLedGreenPin = device["statusLedGreenPin"].as<uint8_t>();
        if (device["statusLedBluePin"].is<int>() && device["statusLedBluePin"].as<int>() == -1) settings.device.statusLedBluePin = 255;
        else if (device["statusLedBluePin"].is<uint8_t>()) settings.device.statusLedBluePin = device["statusLedBluePin"].as<uint8_t>();
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
        copyString(ui, "language", settings.ui.language);
        copyString(ui, "theme", settings.ui.theme);
        if (ui["gpioSafetyOverride"].is<bool>()) settings.ui.gpioSafetyOverride = ui["gpioSafetyOverride"].as<bool>();
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
        copyJsonStringOrObject(ui,"recordedMelodies",settings.ui.recordedMelodies);
        copyJsonStringOrObject(ui, "motorRuntimeConfig", settings.ui.motorRuntimeConfig);
    }

    sanitizeInPlace(settings);
#if APP_HAS_ONBOARD_PANEL && !APP_SUNTON_PANEL
    // GPIO38 is physically tied to BZ1. Safety override cannot repurpose it.
    bool buzzerConflict=settings.device.statusLedPin==38||settings.device.statusLedGreenPin==38||settings.device.statusLedBluePin==38;
    if(settings.audio.enabled)buzzerConflict|=settings.audio.wsPin==38||settings.audio.bclkPin==38||settings.audio.doutPin==38;
    JsonDocument profiles,bindings;deserializeJson(profiles,settings.ui.peripheralProfileSelections);deserializeJson(bindings,settings.ui.peripheralHelperBindings);
    for(JsonPairConst slot:bindings.as<JsonObjectConst>()){
        String key=slot.key().c_str();int split=key.indexOf(':');if(split<0)continue;String group=key.substring(0,split);int index=key.substring(split+1).toInt();
        const char* list=group=="control"?"controls":group=="audio"?"audioProfiles":group=="audioIn"?"audioInProfiles":group=="sensor"?"sensors":group=="input"?"inputs":group=="display"?"displayProfiles":group=="expansion"?"expansions":group.c_str();
        String profile=profiles[list][index]|"none";if(profile=="none"||((group=="audio"||group=="control")&&profile=="buzzer"))continue;
        for(JsonPairConst pin:slot.value().as<JsonObjectConst>()){
            String signal=pin.key().c_str();if(signal.startsWith("LED_")||signal=="I2C_ADDRESS")continue;
            if(pin.value()==38||(pin.value().is<const char*>()&&pin.value().as<String>()=="38"))buzzerConflict=true;
        }
    }
    if(buzzerConflict){error="GPIO38 is dedicated to the BZ1 buzzer connector";return false;}
#endif
    {
        JsonDocument canProfiles,canBindings;deserializeJson(canProfiles,settings.ui.peripheralProfileSelections);deserializeJson(canBindings,settings.ui.peripheralHelperBindings);
        int index=0,count=0;for(JsonVariantConst profile:canProfiles["communication"].as<JsonArrayConst>()){
            if(profile=="mcp2551"){
                if(++count>1){error="Only one MCP2551 CAN transceiver is supported";return false;}
                auto pins=canBindings["communication:"+String(index)];int tx=CanContract::pin(pins["CTX"]),rx=CanContract::pin(pins["CRX"]);
                if(tx==rx||!GPIO_IS_VALID_OUTPUT_GPIO(tx)||!GPIO_IS_VALID_GPIO(rx)){error="Assign separate valid CTX output and CRX input GPIOs for CAN";return false;}
            }++index;
        }
    }
    if(!validateI2cConfiguration(settings,error))return false;
    return true;
}

bool SettingsManager::writeStringIfChanged(const char* key, const String& value) {
    if (writeFailed_) return false;
    if (preferences_.isKey(key) && preferences_.getString(key, "") == value) {
        return false;
    }
    const size_t written = preferences_.putString(key, value);
    if (written != value.length() || (value.isEmpty() && (!preferences_.isKey(key) || preferences_.getString(key, "#failed") != value))) {
        writeFailed_ = true;
        return false;
    }
    return true;
}

bool SettingsManager::writeBoolIfChanged(const char* key, bool value) {
    if (writeFailed_) return false;
    if (preferences_.isKey(key) && preferences_.getBool(key, !value) == value) {
        return false;
    }
    if (preferences_.putBool(key, value) != 1) { writeFailed_ = true; return false; }
    return true;
}

bool SettingsManager::writeUIntIfChanged(const char* key, uint32_t value) {
    if (writeFailed_) return false;
    if (preferences_.isKey(key) && preferences_.getUInt(key, value + 1) == value) {
        return false;
    }
    if (preferences_.putUInt(key, value) != sizeof(value)) { writeFailed_ = true; return false; }
    return true;
}

bool SettingsManager::writeIntIfChanged(const char* key, int32_t value) {
    if (writeFailed_) return false;
    if (preferences_.isKey(key) && preferences_.getInt(key, value + 1) == value) {
        return false;
    }
    if (preferences_.putInt(key, value) != sizeof(value)) { writeFailed_ = true; return false; }
    return true;
}

bool SettingsManager::writeFloatIfChanged(const char* key, float value) {
    if (writeFailed_) return false;
    if (preferences_.isKey(key) && fabsf(preferences_.getFloat(key, value + 1.0f) - value) < 0.0001f) {
        return false;
    }
    if (preferences_.putFloat(key, value) != sizeof(value)) { writeFailed_ = true; return false; }
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
