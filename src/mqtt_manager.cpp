#include "device_log.h"
#include "mqtt_manager.h"

#include "motor_runtime_config.h"
#include "system_metrics.h"
#include "version.h"
#include <esp_heap_caps.h>

namespace {
String payloadToString(char* payload, size_t len) {
    String value;
    value.reserve(len);
    for (size_t index = 0; index < len; ++index) {
        value += payload[index];
    }
    return value;
}

String uniqueBrokerClientId(const String& configured) {
    const String base = configured.isEmpty() ? "elma-device" : configured;
    char suffix[8];
    snprintf(suffix, sizeof(suffix), "-%06llX", static_cast<unsigned long long>(ESP.getEfuseMac() & 0xFFFFFFULL));
    return base + suffix;
}

bool mqttReconnectRequired(const SettingsBundle& current, const SettingsBundle& next) {
    return current.mqtt.host != next.mqtt.host ||
           current.mqtt.port != next.mqtt.port ||
           current.mqtt.username != next.mqtt.username ||
           current.mqtt.password != next.mqtt.password ||
           current.mqtt.clientId != next.mqtt.clientId ||
           current.mqtt.baseTopic != next.mqtt.baseTopic ||
           current.device.deviceName != next.device.deviceName;
}

uint8_t batteryPercentFromVoltage(float voltage) {
    if (!isfinite(voltage) || voltage <= 0.0f) {
        return 0;
    }
    const float clamped = voltage < 3.2f ? 3.2f : (voltage > 4.2f ? 4.2f : voltage);
    return static_cast<uint8_t>(((clamped - 3.2f) / (4.2f - 3.2f) * 100.0f) + 0.5f);
}

const char* mqttBinaryPayload(bool enabled) {
    return enabled ? "ON" : "OFF";
}

String chipTemperatureStateTopic(const SettingsBundle& settings) {
    return settings.mqtt.baseTopic + "/state/cpu_temperature";
}

constexpr uint32_t kDefaultMotorDurationMs = 5000;
constexpr uint32_t kMinimumMotorDurationMs = 100;
constexpr uint32_t kMaximumMotorDurationMs = 600000;

uint32_t clampMotorDurationMs(uint32_t durationMs) {
    if (durationMs < kMinimumMotorDurationMs) {
        return kDefaultMotorDurationMs;
    }
    return durationMs > kMaximumMotorDurationMs ? kMaximumMotorDurationMs : durationMs;
}

const char* motorChannelSuffix(uint8_t channelIndex) {
    return channelIndex == 0 ? "channel_a" : "channel_b";
}

const char* motorChannelLabel(uint8_t channelIndex) {
    return channelIndex == 0 ? "Channel A" : "Channel B";
}

struct MotorDirectionConfig {
    bool forward = true;
    uint32_t durationMs = kDefaultMotorDurationMs;
    int8_t limitInputIndex = -1;
    String movementRole = "none";
};

struct MotorChannelConfig {
    MotorDirectionConfig open;
    MotorDirectionConfig close;
};

struct MqttFeatureFlags {
    bool audio = false;
    bool battery = false;
    bool display = false;
    bool storage = false;
    bool motor = false;
    bool motorChannels[2] = {false, false};
};

String peripheralControlProfileFromUi(const SettingsBundle& settings, size_t index) {
    JsonDocument uiDoc;
    deserializeJson(uiDoc, settings.ui.peripheralProfileSelections.isEmpty() ? String("{}") : settings.ui.peripheralProfileSelections);
    JsonArray controls = uiDoc["controls"].as<JsonArray>();
    if (controls.isNull() || index >= controls.size()) {
        return String("none");
    }
    return String(static_cast<const char*>(controls[index] | "none"));
}

String peripheralHelperValue(const SettingsBundle& settings, const String& slotKey, const char* signalKey) {
    if (signalKey == nullptr || *signalKey == '\0') {
        return String();
    }
    JsonDocument bindingsDoc;
    deserializeJson(bindingsDoc, settings.ui.peripheralHelperBindings.isEmpty() ? String("{}") : settings.ui.peripheralHelperBindings);
    JsonVariant slot = bindingsDoc[slotKey];
    if (slot.isNull() || !slot.is<JsonObjectConst>()) {
        return String();
    }
    return String(static_cast<const char*>(slot[signalKey] | ""));
}

bool helperPinAssigned(const SettingsBundle& settings, size_t controlIndex, const char* signalKey) {
    const String slotKey = String("control:") + String(controlIndex);
    const String rawValue = peripheralHelperValue(settings, slotKey, signalKey);
    if (rawValue.isEmpty()) {
        return false;
    }
    const long numericValue = rawValue.toInt();
    return numericValue >= 0 && numericValue <= 127;
}

void applyPersistedMotorFeatureFlags(const SettingsBundle& settings, MqttFeatureFlags& flags) {
    size_t drv8833ControlIndex = SIZE_MAX;
    for (size_t index = 0; index < 16; ++index) {
        const String profile = peripheralControlProfileFromUi(settings, index);
        if (profile.equalsIgnoreCase("drv8833-dual-motor-driver")) {
            drv8833ControlIndex = index;
            break;
        }
        if (profile == "none" && index > 0) {
            break;
        }
    }

    if (drv8833ControlIndex == SIZE_MAX) {
        return;
    }

    flags.motorChannels[0] = helperPinAssigned(settings, drv8833ControlIndex, "IN1") && helperPinAssigned(settings, drv8833ControlIndex, "IN2");
    flags.motorChannels[1] = helperPinAssigned(settings, drv8833ControlIndex, "IN3") && helperPinAssigned(settings, drv8833ControlIndex, "IN4");
    flags.motor = flags.motorChannels[0] || flags.motorChannels[1];
}

String normalizedProfileName(String value) {
    value.trim();
    value.toLowerCase();
    return value.isEmpty() ? String("none") : value;
}

bool hasConfiguredProfile(JsonVariantConst variant) {
    if (variant.isNull()) {
        return false;
    }
    if (variant.is<JsonArrayConst>()) {
        for (JsonVariantConst entry : variant.as<JsonArrayConst>()) {
            if (hasConfiguredProfile(entry)) {
                return true;
            }
        }
        return false;
    }
    return normalizedProfileName(String(static_cast<const char*>(variant | "none"))) != "none";
}

bool hasBatterySensorProfile(JsonVariantConst variant) {
    if (!variant.is<JsonArrayConst>()) {
        return false;
    }
    for (JsonVariantConst entry : variant.as<JsonArrayConst>()) {
        if (normalizedProfileName(String(static_cast<const char*>(entry | "none"))) == "battery-voltage-divider-220k") {
            return true;
        }
    }
    return false;
}

MqttFeatureFlags configuredMqttFeatures(const SettingsBundle& settings, const MqttManager::MotorStatusAppender& motorStatusAppender) {
    MqttFeatureFlags flags;

    JsonDocument profilesDoc;
    deserializeJson(profilesDoc, settings.ui.peripheralProfileSelections.isEmpty() ? String("{}") : settings.ui.peripheralProfileSelections);
    JsonObjectConst profiles = profilesDoc.as<JsonObjectConst>();

    flags.audio = settings.audio.enabled && (hasConfiguredProfile(profiles["audioProfiles"]) || hasConfiguredProfile(profiles["audioProfile"]));
    flags.display = hasConfiguredProfile(profiles["displayProfiles"]) || hasConfiguredProfile(profiles["displayProfile"]);
    flags.storage = settings.sd.enabled && hasConfiguredProfile(profiles["storage"]);
    flags.battery = settings.battery.adcPin > 0 && hasBatterySensorProfile(profiles["sensors"]);

    (void)motorStatusAppender;
    applyPersistedMotorFeatureFlags(settings, flags);

    return flags;
}

String mqttFeatureLayoutSignature(const SettingsBundle& settings) {
    String signature = settings.ui.peripheralProfileSelections;
    signature += "|";
    signature += settings.ui.peripheralHelperBindings;
    signature += "|";
    signature += settings.audio.enabled ? "1" : "0";
    signature += "|";
    signature += String(settings.battery.adcPin);
    signature += "|";
    signature += settings.sd.enabled ? "1" : "0";
    return signature;
}

MotorChannelConfig readMotorChannelConfig(const SettingsBundle& settings, const char* channelKey) {
    MotorChannelConfig config;
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    DynamicJsonDocument document(MotorRuntimeConfig::jsonCapacity(settings.ui.motorRuntimeConfig));
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    if (!MotorRuntimeConfig::deserializeDocument(settings.ui.motorRuntimeConfig, document)) {
        return config;
    }

    JsonObjectConst channel = document[channelKey].as<JsonObjectConst>();
    if (channel.isNull()) {
        return config;
    }

    auto readDirection = [](JsonObjectConst channelObject, const char* directionKey, bool forwardDirection, MotorDirectionConfig& directionConfig) {
        JsonObjectConst direction = channelObject[directionKey].as<JsonObjectConst>();
        if (direction.isNull()) {
            return;
        }
        directionConfig.forward = forwardDirection;
        directionConfig.durationMs = clampMotorDurationMs(direction["durationMs"] | directionConfig.durationMs);
        if (!direction["limitInputIndex"].isNull()) {
            directionConfig.limitInputIndex = static_cast<int8_t>(direction["limitInputIndex"].as<int>());
        }
        directionConfig.movementRole = MotorRuntimeConfig::normalizeMovementRole(String(static_cast<const char*>(direction["movementRole"] | "none")));
    };

    MotorDirectionConfig forwardConfig;
    MotorDirectionConfig backwardConfig;
    readDirection(channel, "forward", true, forwardConfig);
    readDirection(channel, "backward", false, backwardConfig);

    const auto pickDirection = [&](const String& targetRole, bool fallbackForward) {
        if (forwardConfig.movementRole == targetRole && backwardConfig.movementRole != targetRole) {
            return forwardConfig;
        }
        if (backwardConfig.movementRole == targetRole && forwardConfig.movementRole != targetRole) {
            return backwardConfig;
        }
        return fallbackForward ? forwardConfig : backwardConfig;
    };

    config.open = pickDirection("opening", true);
    config.close = pickDirection("closing", false);
    return config;
}

uint32_t parseMotorDurationPayload(const String& payloadValue) {
    if (payloadValue.startsWith("{")) {
        JsonDocument doc;
        if (deserializeJson(doc, payloadValue) == DeserializationError::Ok) {
            return clampMotorDurationMs(doc["durationMs"] | doc["duration"] | doc["value"] | kDefaultMotorDurationMs);
        }
    }
    return clampMotorDurationMs(static_cast<uint32_t>(payloadValue.toInt()));
}

void appendMotorConfig(JsonObject root, const SettingsBundle& settings) {
    JsonArray channels = root["channels"].as<JsonArray>();
    if (channels.isNull()) {
        return;
    }

    const MotorChannelConfig configs[] = {
        readMotorChannelConfig(settings, "a"),
        readMotorChannelConfig(settings, "b"),
    };
    for (uint8_t channelIndex = 0; channelIndex < 2 && channelIndex < channels.size(); ++channelIndex) {
        JsonObject channel = channels[channelIndex].as<JsonObject>();
        if (channel.isNull()) {
            continue;
        }
        channel["openDurationMs"] = configs[channelIndex].open.durationMs;
        channel["closeDurationMs"] = configs[channelIndex].close.durationMs;
        channel["openLimitInputIndex"] = configs[channelIndex].open.limitInputIndex;
        channel["closeLimitInputIndex"] = configs[channelIndex].close.limitInputIndex;
    }
}

String normalizeMediaType(const String& value, bool announce) {
    String mediaType = value;
    mediaType.trim();
    mediaType.toLowerCase();

    if (announce || mediaType.indexOf("tts") >= 0 || mediaType.indexOf("announce") >= 0 || mediaType.indexOf("speech") >= 0) {
        return "tts";
    }

    if (mediaType.isEmpty()) {
        return "stream";
    }

    if (mediaType == "music" || mediaType == "audio" || mediaType == "stream" || mediaType == "media" || mediaType == "radio") {
        return "stream";
    }

    return mediaType;
}

uint8_t percentFromVolumeLevel(float level) {
    const float clamped = level < 0.0f ? 0.0f : (level > 1.0f ? 1.0f : level);
    return static_cast<uint8_t>((clamped * 100.0f) + 0.5f);
}

#ifdef APP_ENABLE_HACS_MQTT
[[maybe_unused]]
String normalizedHacsPlaybackState(const String& value) {
    String state = value;
    state.trim();
    state.toLowerCase();

    if (state == "buffering") {
        return "playing";
    }

    if (state == "playing" || state == "paused" || state == "idle" || state == "off" || state == "stopped") {
        return state;
    }

    return "idle";
}

[[maybe_unused]]
String normalizedHacsMediaType(const String& value) {
    String mediaType = value;
    mediaType.trim();
    mediaType.toLowerCase();

    if (mediaType == "tts" || mediaType == "speech" || mediaType == "announce") {
        return "music";
    }

    if (mediaType.isEmpty() || mediaType == "idle" || mediaType == "stream" || mediaType == "radio" || mediaType == "audio") {
        return "music";
    }

    return mediaType;
}

[[maybe_unused]]
String hacsVolumePayload(uint8_t volumePercent) {
    char buffer[8];
    snprintf(buffer, sizeof(buffer), "%.2f", static_cast<float>(volumePercent) / 100.0f);
    return String(buffer);
}
#endif
}  // namespace

void MqttManager::begin(const SettingsBundle& settings, AppState& appState, WiFiManager& wifiManager, OtaManager& otaManager, CommandHandler commandHandler, MotorStatusAppender motorStatusAppender) {
    appState_ = &appState;
    wifiManager_ = &wifiManager;
    otaManager_ = &otaManager;
    commandHandler_ = commandHandler;
    motorStatusAppender_ = motorStatusAppender;
    wifiWasConnected_ = wifiManager.isConnected();

    client_.onConnect([this](bool sessionPresent) { handleConnected(sessionPresent); });
    client_.onDisconnect([this](AsyncMqttClientDisconnectReason reason) { handleDisconnected(reason); });
    client_.onSubscribe([this](uint16_t, uint8_t) { noteBrokerActivity(); });
    client_.onPublish([this](uint16_t) { releasePublishSlot(); noteBrokerActivity(); });
    client_.onMessage([this](char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
        handleMessage(topic, payload, properties, len, index, total);
    });
    applySettings(settings);
}

void MqttManager::applySettings(const SettingsBundle& settings) {
    const bool discoverySettingsChanged = !configured_ ||
        settings_.mqtt.discoveryEnabled != settings.mqtt.discoveryEnabled ||
        settings_.device.friendlyName != settings.device.friendlyName ||
        settings_.mqtt.baseTopic != settings.mqtt.baseTopic ||
        mqttFeatureLayoutSignature(settings_) != mqttFeatureLayoutSignature(settings);
    const bool needsReconfigure = !configured_ || mqttReconnectRequired(settings_, settings) || !client_.connected();
    settings_ = settings;
    if (settings_.mqtt.host.isEmpty()) {
        consecutiveFailureCount_ = 0;
        recoveryRebootRecommended_ = false;
    }
    if (discoverySettingsChanged) {
        discoveryPublishedForSession_ = false;
    }
    if (needsReconfigure) {
        configureClient();
        configured_ = true;
        return;
    }

    if (client_.connected()) {
        if (settings_.mqtt.discoveryEnabled && (discoverySettingsChanged || !discoveryPublishedForSession_)) {
            publishDiscovery();
        }
        statePublishPending_ = true;
    }
}

void MqttManager::configureClient() {
    client_.disconnect(true);
    // AsyncMqttClient borrows these pointers; neither temporaries nor a
    // SettingsBundle replaced by a later UI save may own their storage.
    clientHost_ = settings_.mqtt.host;
    // MQTT brokers allow only one live connection per client ID. Projects are
    // commonly cloned to several ESPs, so keep the configured ID as the human
    // readable prefix and add this chip's stable hardware suffix.
    clientId_ = uniqueBrokerClientId(settings_.mqtt.clientId.isEmpty() ? settings_.device.deviceName : settings_.mqtt.clientId);
    clientUsername_ = settings_.mqtt.username;
    clientPassword_ = settings_.mqtt.password;
    clientWillTopic_ = HaBridge::availabilityTopic(settings_);
    client_.setServer(clientHost_.c_str(), settings_.mqtt.port);
    client_.setClientId(clientId_.c_str());
    client_.setCredentials(
        clientUsername_.isEmpty() ? nullptr : clientUsername_.c_str(),
        clientUsername_.isEmpty() ? nullptr : clientPassword_.c_str());
    client_.setKeepAlive(MQTT_KEEP_ALIVE_SECONDS);
    client_.setCleanSession(true);
    client_.setWill(clientWillTopic_.c_str(), 1, true, "offline");
    lastConnectAttemptAt_ = 0;
    lastBrokerActivityAt_ = 0;
    if (settings_.mqtt.host.isEmpty()) {
        consecutiveFailureCount_ = 0;
        recoveryRebootRecommended_ = false;
    }

    if (!connectionEnabled_) {
        return;
    }

    if (!settings_.mqtt.host.isEmpty() && wifiManager_ != nullptr && wifiManager_->isConnected()) {
        DebugLog.printf("[mqtt] immediate reconnect attempt %u/%u to %s:%u\n",
                  static_cast<unsigned>(min<uint8_t>(static_cast<uint8_t>(consecutiveFailureCount_ + 1), MQTT_MAX_CONSECUTIVE_FAILURES)),
                      static_cast<unsigned>(MQTT_MAX_CONSECUTIVE_FAILURES),
                      settings_.mqtt.host.c_str(), settings_.mqtt.port);
        lastConnectAttemptAt_ = millis();
        client_.connect();
    }
}

void MqttManager::loop() {
    publisherTask_ = xTaskGetCurrentTaskHandle();
    handleWiFiState();
    connectIfNeeded();
    if (client_.connected()) {
        if (discoveryRestartPending_.exchange(false)) {
            discoveryCursor_ = 0;
            stateCursor_ = 0;
        }
        const bool memoryAvailable = ESP.getFreeHeap() >= 40000 &&
            heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) >= 8192;
        if (memoryAvailable && discoveryPublishPending_ && settings_.mqtt.discoveryEnabled &&
            pendingPublishes_ < 4 && millis() - lastDiscoveryStepAt_ >= 100UL &&
            (otaManager_ == nullptr || !otaManager_->isBusy())) {
            lastDiscoveryStepAt_ = millis();
            publishDiscoveryNow();
        }
        if (memoryAvailable && pendingPublishes_ < 4 && statePublishPending_ && millis() - lastStateAttemptAt_ >= 1000UL) {
            lastStateAttemptAt_ = millis();
            statePublishPending_ = false;
            publishStateNow();
        }
    }
    // AsyncMqttClient's keepalive tracks PINGRESP and disconnects on timeout.
    // Application-data silence is normal while publishing is deferred.
    if (isConnected() && millis() - lastStatePublishAt_ > 30000UL) {
        publishState();
    }
}

void MqttManager::connectIfNeeded() {
    if (!connectionEnabled_ || settings_.mqtt.host.isEmpty() || wifiManager_ == nullptr || !wifiManager_->isConnected() || client_.connected()) {
        return;
    }
    if (millis() - lastConnectAttemptAt_ < MQTT_RETRY_INTERVAL_MS) {
        return;
    }
    configureClient();
}

void MqttManager::handleWiFiState() {
    if (wifiManager_ == nullptr) {
        return;
    }

    const bool wifiConnected = wifiManager_->isConnected();
    if (wifiConnected == wifiWasConnected_) {
        return;
    }

    wifiWasConnected_ = wifiConnected;
    if (!wifiConnected) {
        discoveryPublishedForSession_ = false;
        lastOtaDiscoverySignature_ = "";
        lastBrokerActivityAt_ = 0;
        lastConnectAttemptAt_ = millis();
        if (client_.connected()) {
            DebugLog.println("[mqtt] Wi-Fi dropped, forcing MQTT disconnect");
            client_.disconnect(true);
        }
        return;
    }

    DebugLog.println("[mqtt] Wi-Fi restored, resetting MQTT session");
    discoveryPublishedForSession_ = false;
    lastOtaDiscoverySignature_ = "";
    lastConnectAttemptAt_ = 0;
    lastBrokerActivityAt_ = 0;
    client_.disconnect(true);
}

void MqttManager::handleConnected(bool sessionPresent) {
    (void)sessionPresent;
    DebugLog.printf("[mqtt] connected host=%s port=%u\n", settings_.mqtt.host.c_str(), settings_.mqtt.port);
    consecutiveFailureCount_ = 0;
    recoveryRebootRecommended_ = false;
    wifiWasConnected_ = wifiManager_ != nullptr && wifiManager_->isConnected();
    noteBrokerActivity();
    clearFrontendError();
    if (appState_ != nullptr) {
        appState_->setMqttConnected(true);
    }
    const MqttFeatureFlags featureFlags = configuredMqttFeatures(settings_, motorStatusAppender_);
    publishPacket(HaBridge::availabilityTopic(settings_).c_str(), 1, true, "online");
    if (featureFlags.audio) {
        client_.subscribe(HaBridge::commandTopic(settings_, "play").c_str(), 1);
        client_.subscribe(HaBridge::commandTopic(settings_, "tts").c_str(), 1);
        client_.subscribe(HaBridge::commandTopic(settings_, "stop").c_str(), 1);
        client_.subscribe(HaBridge::commandTopic(settings_, "volume").c_str(), 1);
        client_.subscribe(HaBridge::commandTopic(settings_, "alarm").c_str(), 1);
        client_.subscribe(HaBridge::commandTopic(settings_, "notify").c_str(), 1);
    }
    if (featureFlags.display) {
        client_.subscribe(HaBridge::commandTopic(settings_, "display_trigger").c_str(), 1);
    }
    client_.subscribe(HaBridge::commandTopic(settings_, "web_ui").c_str(), 1);
    client_.subscribe(HaBridge::commandTopic(settings_, "reboot").c_str(), 1);
    if (featureFlags.storage) {
        client_.subscribe(HaBridge::commandTopic(settings_, "storage/sd_remount").c_str(), 1);
    }
    client_.subscribe(HaBridge::commandTopic(settings_, "ota/check").c_str(), 1);
    client_.subscribe(HaBridge::commandTopic(settings_, "ota/auto_update").c_str(), 1);
    client_.subscribe(HaBridge::commandTopic(settings_, "ota/install").c_str(), 1);
    client_.subscribe(HaBridge::commandTopic(settings_, "ota/select_version").c_str(), 1);
    client_.subscribe(HaBridge::commandTopic(settings_, "ota/install_version").c_str(), 1);
    if (featureFlags.motorChannels[0]) {
        client_.subscribe(HaBridge::commandTopic(settings_, "motor/channel_a/open").c_str(), 1);
        client_.subscribe(HaBridge::commandTopic(settings_, "motor/channel_a/close").c_str(), 1);
        client_.subscribe(HaBridge::commandTopic(settings_, "motor/channel_a/switch").c_str(), 1);
        client_.subscribe(HaBridge::commandTopic(settings_, "motor/channel_a/open/duration").c_str(), 1);
        client_.subscribe(HaBridge::commandTopic(settings_, "motor/channel_a/close/duration").c_str(), 1);
    }
    if (featureFlags.motorChannels[1]) {
        client_.subscribe(HaBridge::commandTopic(settings_, "motor/channel_b/open").c_str(), 1);
        client_.subscribe(HaBridge::commandTopic(settings_, "motor/channel_b/close").c_str(), 1);
        client_.subscribe(HaBridge::commandTopic(settings_, "motor/channel_b/switch").c_str(), 1);
        client_.subscribe(HaBridge::commandTopic(settings_, "motor/channel_b/open/duration").c_str(), 1);
        client_.subscribe(HaBridge::commandTopic(settings_, "motor/channel_b/close/duration").c_str(), 1);
    }
#ifdef APP_ENABLE_HACS_MQTT
    if (featureFlags.audio) {
        client_.subscribe(HaBridge::hacsMediaPlayerCommandTopic(settings_, "play").c_str(), 1);
        client_.subscribe(HaBridge::hacsMediaPlayerCommandTopic(settings_, "pause").c_str(), 1);
        client_.subscribe(HaBridge::hacsMediaPlayerCommandTopic(settings_, "playpause").c_str(), 1);
        client_.subscribe(HaBridge::hacsMediaPlayerCommandTopic(settings_, "next").c_str(), 1);
        client_.subscribe(HaBridge::hacsMediaPlayerCommandTopic(settings_, "previous").c_str(), 1);
        client_.subscribe(HaBridge::hacsMediaPlayerCommandTopic(settings_, "stop").c_str(), 1);
        client_.subscribe(HaBridge::hacsMediaPlayerCommandTopic(settings_, "volume").c_str(), 1);
        client_.subscribe(HaBridge::hacsMediaPlayerCommandTopic(settings_, "playmedia").c_str(), 1);
    }
#endif
    if (settings_.mqtt.discoveryEnabled && !discoveryPublishedForSession_) {
        discoveryPublishPending_ = true;
    }
    statePublishPending_ = true;
}

void MqttManager::handleDisconnected(AsyncMqttClientDisconnectReason reason) {
    pendingPublishes_ = 0;
    discoveryRestartPending_ = true;
    DebugLog.printf("[mqtt] disconnected reason=%d\n", static_cast<int>(reason));
    discoveryPublishedForSession_ = false;
    lastOtaDiscoverySignature_ = "";
    lastBrokerActivityAt_ = 0;
    if (appState_ != nullptr) {
        appState_->setMqttConnected(false);
    }
    registerFailedAttempt(reason);
}

void MqttManager::handleMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
    (void)properties;
    if (index != 0 || total != len || commandHandler_ == nullptr) {
        return;
    }

    noteBrokerActivity();

    const String topicValue = topic;
    const String payloadValue = payloadToString(payload, len);
    const MqttFeatureFlags featureFlags = configuredMqttFeatures(settings_, motorStatusAppender_);
    PlaybackCommand command;
#ifdef APP_ENABLE_HACS_MQTT
    if (!featureFlags.audio && topicValue.startsWith(settings_.mqtt.baseTopic + "/hacs/cmd/")) {
        return;
    }
    if (topicValue == HaBridge::hacsMediaPlayerCommandTopic(settings_, "stop")) {
        command.action = "stop";
        commandHandler_(command);
        return;
    }

    if (topicValue == HaBridge::hacsMediaPlayerCommandTopic(settings_, "pause")) {
        command.action = "pause";
        commandHandler_(command);
        return;
    }

    if (topicValue == HaBridge::hacsMediaPlayerCommandTopic(settings_, "play") ||
        topicValue == HaBridge::hacsMediaPlayerCommandTopic(settings_, "playpause") ||
        topicValue == HaBridge::hacsMediaPlayerCommandTopic(settings_, "next") ||
        topicValue == HaBridge::hacsMediaPlayerCommandTopic(settings_, "previous")) {
        command.action = topicValue.substring(topicValue.lastIndexOf('/') + 1);
        commandHandler_(command);
        return;
    }

    if (topicValue == HaBridge::hacsMediaPlayerCommandTopic(settings_, "volume")) {
        command.action = "volume";
        command.volumePercent = payloadValue.indexOf('.') >= 0
            ? percentFromVolumeLevel(payloadValue.toFloat())
            : payloadValue.toInt();
        commandHandler_(command);
        return;
    }

    if (topicValue == HaBridge::hacsMediaPlayerCommandTopic(settings_, "playmedia")) {
        command.action = "play";
    }
#endif

    if (topicValue == HaBridge::commandTopic(settings_, "stop")) {
        if (!featureFlags.audio) {
            return;
        }
        command.action = "stop";
        commandHandler_(command);
        return;
    }

    if (topicValue == HaBridge::commandTopic(settings_, "volume")) {
        if (!featureFlags.audio) {
            return;
        }
        command.action = "volume";
        if (payloadValue.startsWith("{")) {
            JsonDocument doc;
            if (deserializeJson(doc, payloadValue) == DeserializationError::Ok) {
                if (!doc["volumePercent"].isNull() || !doc["volume"].isNull()) {
                    command.volumePercent = doc["volumePercent"] | doc["volume"] | 0;
                } else if (!doc["volume_level"].isNull()) {
                    command.volumePercent = percentFromVolumeLevel(doc["volume_level"] | 0.0f);
                }
            }
        } else {
            command.volumePercent = payloadValue.indexOf('.') >= 0
                ? percentFromVolumeLevel(payloadValue.toFloat())
                : payloadValue.toInt();
        }
        commandHandler_(command);
        return;
    }

    if (topicValue == HaBridge::commandTopic(settings_, "display_trigger")) {
        if (!featureFlags.display) {
            return;
        }
        command.action = "display_trigger";
        command.payload = payloadValue;
        commandHandler_(command);
        return;
    }

    if (topicValue == HaBridge::commandTopic(settings_, "alarm")) {
        if (!featureFlags.audio) {
            return;
        }
        String action = payloadValue;
        action.trim();
        action.toLowerCase();
        command.action = (action == "stop" || action == "off" || action == "0") ? "alarm_stop" : "alarm_start";
        commandHandler_(command);
        return;
    }

    if (topicValue == HaBridge::commandTopic(settings_, "notify")) {
        if (!featureFlags.audio) {
            return;
        }
        command.action = "notify";
        command.payload = payloadValue;
        commandHandler_(command);
        return;
    }

    if (topicValue == HaBridge::commandTopic(settings_, "web_ui")) {
        String action = payloadValue;
        action.trim();
        action.toLowerCase();
        if (action == "unlock" || action == "on" || action == "start" || action == "enable") {
            command.action = "web_ui_unlock";
        } else {
            command.action = "web_ui_lock";
        }
        command.payload = payloadValue;
        commandHandler_(command);
        return;
    }

    if (topicValue == HaBridge::commandTopic(settings_, "reboot")) {
        command.action = "reboot";
        command.payload = payloadValue;
        commandHandler_(command);
        return;
    }

    if (topicValue == HaBridge::commandTopic(settings_, "storage/sd_remount")) {
        if (!featureFlags.storage) {
            return;
        }
        command.action = "sd_remount";
        command.payload = payloadValue;
        commandHandler_(command);
        return;
    }

    if (topicValue == HaBridge::commandTopic(settings_, "ota/check")) {
        command.action = "ota_check";
        commandHandler_(command);
        return;
    }

    if (topicValue == HaBridge::commandTopic(settings_, "ota/auto_update")) {
        command.action = "ota_auto_update";
        command.payload = payloadValue;
        commandHandler_(command);
        return;
    }

    if (topicValue == HaBridge::commandTopic(settings_, "ota/install")) {
        if (payloadValue.isEmpty() || payloadValue.equalsIgnoreCase("install")) {
            command.action = "ota_install_selected";
        } else if (payloadValue.equalsIgnoreCase("latest")) {
            command.action = "ota_install_latest";
        } else if (payloadValue.startsWith("{")) {
            JsonDocument doc;
            if (deserializeJson(doc, payloadValue) == DeserializationError::Ok) {
                command.version = String(static_cast<const char*>(doc["version"] | doc["tag"] | ""));
                command.assetName = String(static_cast<const char*>(doc["assetName"] | doc["asset"] | ""));
            }
            command.action = command.version.isEmpty() ? "ota_install_selected" : "ota_install";
        } else {
            command.action = "ota_install";
            command.version = payloadValue;
        }
        commandHandler_(command);
        return;
    }

    if (topicValue == HaBridge::commandTopic(settings_, "ota/select_version")) {
        command.action = "ota_select_version";
        if (payloadValue.startsWith("{")) {
            JsonDocument doc;
            if (deserializeJson(doc, payloadValue) == DeserializationError::Ok) {
                command.label = String(static_cast<const char*>(doc["option"] | doc["label"] | ""));
            }
        } else {
            command.label = payloadValue;
        }
        commandHandler_(command);
        return;
    }

    if (topicValue == HaBridge::commandTopic(settings_, "ota/install_version")) {
        command.action = "ota_install";
        if (payloadValue.startsWith("{")) {
            JsonDocument doc;
            if (deserializeJson(doc, payloadValue) == DeserializationError::Ok) {
                command.version = String(static_cast<const char*>(doc["version"] | doc["tag"] | ""));
                command.assetName = String(static_cast<const char*>(doc["assetName"] | doc["asset"] | ""));
            }
        } else {
            command.version = payloadValue;
        }
        commandHandler_(command);
        return;
    }

    for (uint8_t channelIndex = 0; channelIndex < 2; ++channelIndex) {
        if (!featureFlags.motorChannels[channelIndex]) {
            continue;
        }
        const String channelSuffix = motorChannelSuffix(channelIndex);
        const MotorChannelConfig channelConfig = readMotorChannelConfig(settings_, channelIndex == 0 ? "a" : "b");
        const String openTopic = HaBridge::commandTopic(settings_, (String("motor/") + channelSuffix + "/open").c_str());
        const String closeTopic = HaBridge::commandTopic(settings_, (String("motor/") + channelSuffix + "/close").c_str());
        const String switchTopic = HaBridge::commandTopic(settings_, (String("motor/") + channelSuffix + "/switch").c_str());
        const String openDurationTopic = HaBridge::commandTopic(settings_, (String("motor/") + channelSuffix + "/open/duration").c_str());
        const String closeDurationTopic = HaBridge::commandTopic(settings_, (String("motor/") + channelSuffix + "/close/duration").c_str());

        if (topicValue == openTopic || topicValue == closeTopic) {
            command.action = "motor_run";
            command.motorChannelIndex = static_cast<int8_t>(channelIndex);
            const MotorDirectionConfig& directionConfig = topicValue == openTopic ? channelConfig.open : channelConfig.close;
            command.motorForward = directionConfig.forward;
            command.motorDurationMs = directionConfig.durationMs;
            command.motorLimitInputIndex = directionConfig.limitInputIndex;
            commandHandler_(command);
            return;
        }

        if (topicValue == switchTopic) {
            String normalizedPayload = payloadValue;
            normalizedPayload.trim();
            normalizedPayload.toUpperCase();
            if (normalizedPayload == "ON" || normalizedPayload == "OPEN") {
                command.action = "motor_run";
                command.motorChannelIndex = static_cast<int8_t>(channelIndex);
                command.motorForward = channelConfig.open.forward;
                command.motorDurationMs = channelConfig.open.durationMs;
                command.motorLimitInputIndex = channelConfig.open.limitInputIndex;
                commandHandler_(command);
            } else if (normalizedPayload == "OFF" || normalizedPayload == "CLOSE") {
                command.action = "motor_run";
                command.motorChannelIndex = static_cast<int8_t>(channelIndex);
                command.motorForward = channelConfig.close.forward;
                command.motorDurationMs = channelConfig.close.durationMs;
                command.motorLimitInputIndex = channelConfig.close.limitInputIndex;
                commandHandler_(command);
            }
            return;
        }

        if (topicValue == openDurationTopic || topicValue == closeDurationTopic) {
            command.action = "motor_set_duration";
            command.motorChannelIndex = static_cast<int8_t>(channelIndex);
            command.motorForward = topicValue == openDurationTopic;
            command.motorDurationMs = parseMotorDurationPayload(payloadValue);
            commandHandler_(command);
            return;
        }
    }

    if (command.action.isEmpty()) {
        if (!featureFlags.audio) {
            return;
        }
        command.action = topicValue.endsWith("/tts") ? "tts" : "play";
    }
    if (payloadValue.startsWith("{")) {
        JsonDocument doc;
        if (deserializeJson(doc, payloadValue) == DeserializationError::Ok) {
            const bool announce = doc["announce"] | false;
            const String mediaContentType = String(static_cast<const char*>(doc["media_content_type"] | doc["media_type"] | ""));
            const String explicitType = String(static_cast<const char*>(doc["type"] | ""));
            command.url = String(static_cast<const char*>(doc["url"] | doc["media_content_id"] | doc["media_id"] | doc["mediaId"] | ""));
            command.label = String(static_cast<const char*>(doc["label"] | doc["title"] | doc["media_title"] | doc["extra"]["title"] | ""));
            command.source = String(static_cast<const char*>(doc["source"] | ""));
            command.mediaType = normalizeMediaType(
                explicitType.isEmpty() ? mediaContentType : explicitType,
                command.action == "tts" || announce);

            if (command.action=="tts" && doc["text"].is<const char*>()) {command.url=doc["text"].as<String>();command.mediaType="tts-offline";command.source="offline-tts";}
            if (command.label.isEmpty() && !command.url.isEmpty()) {
                command.label = command.url;
            }
        }
    } else {
        command.url = payloadValue;
        command.mediaType = command.action == "tts" ? (payloadValue.startsWith("http://") || payloadValue.startsWith("https://") ? "tts" : "tts-offline") : "stream";
    }
    commandHandler_(command);
}

void MqttManager::publishJson(const String& topic, const JsonDocument& doc, bool retained) {
    if (!client_.connected()) {
        return;
    }
    String payload;
    serializeJson(doc, payload);
    publishPacket(topic.c_str(), 1, retained, doc.overflowed() ? nullptr : payload.c_str(), payload.length());
    noteBrokerActivity();
}

String MqttManager::currentConfigUrl() const {
    if (appState_ != nullptr) {
        const AppStateSnapshot snapshot = appState_->snapshot();
        if (!snapshot.network.ip.isEmpty()) {
            return "http://" + snapshot.network.ip + "/";
        }
    }

    if (wifiManager_ != nullptr) {
        const String localIp = wifiManager_->localIp().toString();
        if (wifiManager_->isConnected() && localIp != "0.0.0.0") {
            return "http://" + localIp + "/";
        }

        const String apIp = wifiManager_->apIp().toString();
        if (wifiManager_->isApMode() && apIp != "0.0.0.0") {
            return "http://" + apIp + "/";
        }
    }

    return String();
}

void MqttManager::releasePublishSlot() {
    uint8_t count = pendingPublishes_.load();
    while (count && !pendingPublishes_.compare_exchange_weak(count, count - 1)) {}
}

uint16_t MqttManager::publishPacket(const char* topic, uint8_t qos, bool retained, const char* payload, size_t length) {
    const bool statePass = xTaskGetCurrentTaskHandle() == publisherTask_ && statePassActive_;
    if (statePass && statePassIndex_++ < stateCursor_) return 1;
    if (statePass && statePassBlocked_) return 0;
    auto defer = [&]() -> uint16_t {
        if (statePass) statePassBlocked_ = true;
        statePublishPending_ = true;
        return 0;
    };
    if (!client_.connected() || !topic || !*topic || !payload) return defer();
    if (!length) length = strlen(payload);
    const size_t bytes = strlen(topic) + length + 64;
    // The library allocates a vector and dereferences topic without checking
    // allocation failure. Reserve heap for audio/TLS and bound QoS1 backlog.
    if (ESP.getFreeHeap() < 24000 + bytes * 2 ||
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < max(size_t(4096), bytes * 2)) return defer();
    uint8_t count = pendingPublishes_.load();
    do {
        if (count >= 4) return defer();
    } while (!pendingPublishes_.compare_exchange_weak(count, count + 1));
    const uint16_t id = client_.publish(topic, qos, retained, payload, length);
    if (!id) { releasePublishSlot(); return defer(); }
    if (statePass) ++stateCursor_;
    return id;
}

bool MqttManager::publishDiscoveryStep(size_t index, const std::function<uint16_t()>& send) {
    if (index != discoveryCursor_) return false;
    if (send()) ++discoveryCursor_;
    return true;
}

void MqttManager::publishState() { statePublishPending_ = true; }
void MqttManager::publishChipTemperature() { statePublishPending_ = true; }
void MqttManager::publishDiscovery() {
    discoveryRestartPending_ = true;
    discoveryPublishPending_ = true;
}

void MqttManager::publishStateNow() {
    if (!client_.connected() || appState_ == nullptr) {
        return;
    }
    statePassActive_ = true;
    statePassBlocked_ = false;
    statePassIndex_ = 0;
    lastStatePublishAt_ = millis();
    const AppStateSnapshot snapshot = appState_->snapshot();
    const MqttFeatureFlags featureFlags = configuredMqttFeatures(settings_, motorStatusAppender_);

    auto clearRetainedTopic = [this](const String& topic) {
        publishPacket(topic.c_str(), 1, true, "");
    };

    if (featureFlags.audio) {
        JsonDocument playback;
        playback["state"] = snapshot.playback.state;
        playback["type"] = snapshot.playback.type;
        playback["title"] = snapshot.playback.title;
        playback["url"] = snapshot.playback.url;
        playback["source"] = snapshot.playback.source;
        playback["volumePercent"] = snapshot.playback.volumePercent;
        publishJson(HaBridge::playbackStateTopic(settings_), playback, true);
    } else {
        clearRetainedTopic(HaBridge::playbackStateTopic(settings_));
        clearRetainedTopic(settings_.mqtt.baseTopic + "/state/volume");
#ifdef APP_ENABLE_HACS_MQTT
        clearRetainedTopic(HaBridge::hacsMediaPlayerStateTopic(settings_, "state"));
        clearRetainedTopic(HaBridge::hacsMediaPlayerStateTopic(settings_, "title"));
        clearRetainedTopic(HaBridge::hacsMediaPlayerStateTopic(settings_, "mediatype"));
        clearRetainedTopic(HaBridge::hacsMediaPlayerStateTopic(settings_, "volume"));
#endif
    }

    JsonDocument network;
    network["wifiConnected"] = snapshot.network.wifiConnected;
    network["apMode"] = snapshot.network.apMode;
    network["ip"] = snapshot.network.ip;
    network["ssid"] = snapshot.network.ssid;
    network["wifiRssi"] = snapshot.network.wifiRssi;
    network["mqttConnected"] = snapshot.network.mqttConnected;
    publishJson(HaBridge::networkStateTopic(settings_), network, true);

    publishChipTemperatureNow();

    if (featureFlags.battery) {
        JsonDocument battery;
        battery["voltage"] = snapshot.battery.voltage;
        battery["percent"] = batteryPercentFromVoltage(snapshot.battery.voltage);
        battery["rawAdcVoltage"] = snapshot.battery.rawAdcVoltage;
        battery["rawAdc"] = snapshot.battery.rawAdc;
        battery["charging"] = snapshot.battery.charging;
        publishJson(HaBridge::batteryStateTopic(settings_), battery, true);
    } else {
        clearRetainedTopic(HaBridge::batteryStateTopic(settings_));
        clearRetainedTopic(settings_.mqtt.baseTopic + "/state/battery_voltage");
        clearRetainedTopic(settings_.mqtt.baseTopic + "/state/battery_percent");
        clearRetainedTopic(settings_.mqtt.baseTopic + "/state/battery_charging");
    }

    JsonDocument ota;
    if (otaManager_ != nullptr) {
        String error;
        otaManager_->appendFirmwareInfoJson(ota.to<JsonObject>(), false, error);
        if (!error.isEmpty()) {
            ota["error"] = error;
        }
    }
    ota["busy"] = snapshot.ota.busy;
    ota["updateAvailable"] = snapshot.ota.updateAvailable;
    ota["latestVersion"] = snapshot.ota.latestVersion;
    ota["lastResult"] = snapshot.ota.lastResult;
    ota["lastError"] = snapshot.ota.lastError;
    ota["phase"] = snapshot.ota.phase;
    ota["progressPercent"] = snapshot.ota.progressPercent;
    ota["currentVersion"] = APP_VERSION;
    ota["configUrl"] = currentConfigUrl();
    String otaDiscoverySignature = currentConfigUrl();
    otaDiscoverySignature += "|";
    otaDiscoverySignature += String(static_cast<const char*>(ota["selectedVersion"] | ""));
    otaDiscoverySignature += "|";
    otaDiscoverySignature += String(static_cast<const char*>(ota["selectedAssetName"] | ""));
    otaDiscoverySignature += "|";
    otaDiscoverySignature += String(static_cast<const char*>(ota["selectedOption"] | ""));
    otaDiscoverySignature += "|";
    otaDiscoverySignature += String(ota["compatibleReleaseCount"] | 0);
    if (ota["releaseOptions"].is<JsonArray>()) {
        for (JsonVariantConst option : ota["releaseOptions"].as<JsonArrayConst>()) {
            otaDiscoverySignature += "|";
            otaDiscoverySignature += String(static_cast<const char*>(option | ""));
        }
    }
    publishJson(HaBridge::otaStateTopic(settings_), ota, true);

    if (featureFlags.motor && motorStatusAppender_ != nullptr) {
        JsonDocument motor;
        motorStatusAppender_(motor.to<JsonObject>());
        if (!motor.isNull()) {
            appendMotorConfig(motor.as<JsonObject>(), settings_);
            publishJson(HaBridge::motorStateTopic(settings_), motor, true);
        }
    } else {
        clearRetainedTopic(HaBridge::motorStateTopic(settings_));
    }

    if (settings_.mqtt.discoveryEnabled && discoveryPublishedForSession_ && otaDiscoverySignature != lastOtaDiscoverySignature_ && !discoveryPublishPending_) {
        publishDiscovery();
    }

    if (featureFlags.audio) {
        publishPacket((settings_.mqtt.baseTopic + "/state/volume").c_str(), 1, true, String(snapshot.playback.volumePercent).c_str());
    }
    if (featureFlags.battery) {
        publishPacket((settings_.mqtt.baseTopic + "/state/battery_voltage").c_str(), 1, true, String(snapshot.battery.voltage, 3).c_str());
        publishPacket((settings_.mqtt.baseTopic + "/state/battery_percent").c_str(), 1, true, String(batteryPercentFromVoltage(snapshot.battery.voltage)).c_str());
        publishPacket((settings_.mqtt.baseTopic + "/state/battery_charging").c_str(), 1, true, mqttBinaryPayload(snapshot.battery.charging));
    }
#ifdef APP_ENABLE_HACS_MQTT
    if (featureFlags.audio) {
        publishPacket(HaBridge::hacsMediaPlayerStateTopic(settings_, "state").c_str(), 1, true, normalizedHacsPlaybackState(snapshot.playback.state).c_str());
        publishPacket(HaBridge::hacsMediaPlayerStateTopic(settings_, "title").c_str(), 1, true, snapshot.playback.title.c_str());
        publishPacket(HaBridge::hacsMediaPlayerStateTopic(settings_, "mediatype").c_str(), 1, true, normalizedHacsMediaType(snapshot.playback.type).c_str());
        publishPacket(HaBridge::hacsMediaPlayerStateTopic(settings_, "volume").c_str(), 1, true, hacsVolumePayload(snapshot.playback.volumePercent).c_str());
    }
#endif
    statePassActive_ = false;
    if (statePassBlocked_) statePublishPending_ = true;
    else stateCursor_ = 0;
}

void MqttManager::publishChipTemperatureNow() {
    if (!client_.connected()) {
        return;
    }

    const SystemMetricsSnapshot metrics = getSystemMetricsSnapshot();
    const String topic = chipTemperatureStateTopic(settings_);
    if (!metrics.chipTemperatureAvailable) {
        publishPacket(topic.c_str(), 1, true, "");
        noteBrokerActivity();
        return;
    }

    publishPacket(topic.c_str(), 1, true, String(metrics.chipTemperatureC, 1).c_str());
    noteBrokerActivity();
}

void MqttManager::publishBattery(float voltage, float rawAdcVoltage, uint16_t rawAdc, bool charging) {
    (void)voltage; (void)rawAdcVoltage; (void)rawAdc; (void)charging;
    publishState();
}

void MqttManager::publishDiscoveryNow() {
    if (!client_.connected() || !settings_.mqtt.discoveryEnabled) {
        return;
    }
    size_t packetIndex = 0;
    const String configurationUrl = currentConfigUrl();
    const MqttFeatureFlags featureFlags = configuredMqttFeatures(settings_, motorStatusAppender_);
    std::vector<String> firmwareOptions;
    String otaDiscoverySignature = configurationUrl;
    auto clearDiscoveryTopic = [this, &packetIndex](const char* component, const char* objectId) {
        return publishDiscoveryStep(packetIndex++, [&]() {
            return publishPacket(HaBridge::discoveryTopic(settings_, component, objectId).c_str(), 1, true, "");
        });
    };
    if (otaManager_ != nullptr) {
        JsonDocument otaInfo;
        String ignoredError;
        otaManager_->appendFirmwareInfoJson(otaInfo.to<JsonObject>(), false, ignoredError);
        otaDiscoverySignature += "|";
        otaDiscoverySignature += String(static_cast<const char*>(otaInfo["selectedVersion"] | ""));
        otaDiscoverySignature += "|";
        otaDiscoverySignature += String(static_cast<const char*>(otaInfo["selectedAssetName"] | ""));
        otaDiscoverySignature += "|";
        otaDiscoverySignature += String(static_cast<const char*>(otaInfo["selectedOption"] | ""));
        otaDiscoverySignature += "|";
        otaDiscoverySignature += String(otaInfo["compatibleReleaseCount"] | 0);
        if (otaInfo["releaseOptions"].is<JsonArray>()) {
            for (JsonVariantConst option : otaInfo["releaseOptions"].as<JsonArrayConst>()) {
                const String optionValue = String(static_cast<const char*>(option | ""));
                firmwareOptions.push_back(optionValue);
                otaDiscoverySignature += "|";
                otaDiscoverySignature += optionValue;
            }
        }
    }
    if (featureFlags.battery) {
        if (publishDiscoveryStep(packetIndex++, [&]() { return publishPacket(
            HaBridge::discoveryTopic(settings_, "sensor", "battery_voltage").c_str(), 1, true,
            HaBridge::discoveryPayloadSensor(settings_, "battery_voltage", "Battery Voltage", HaBridge::batteryStateTopic(settings_).c_str(), "{{ value_json.voltage | float(0) | round(2) }}", "V", "voltage", "measurement", "mdi:battery", 2, configurationUrl).c_str()); })) return;
        if (publishDiscoveryStep(packetIndex++, [&]() { return publishPacket(
            HaBridge::discoveryTopic(settings_, "sensor", "battery_percent").c_str(), 1, true,
            HaBridge::discoveryPayloadSensor(settings_, "battery_percent", "Battery", HaBridge::batteryStateTopic(settings_).c_str(), "{{ value_json.percent | int(0) }}", "%", "battery", "measurement", "mdi:battery-medium", 0, configurationUrl).c_str()); })) return;
        if (publishDiscoveryStep(packetIndex++, [&]() { return publishPacket(
            HaBridge::discoveryTopic(settings_, "binary_sensor", "battery_charging").c_str(), 1, true,
            HaBridge::discoveryPayloadBinarySensor(settings_, "battery_charging", "Battery Charging", HaBridge::batteryStateTopic(settings_).c_str(), "{{ 'ON' if value_json.charging else 'OFF' }}", "battery_charging", "ON", "OFF", "mdi:battery-charging", configurationUrl).c_str()); })) return;
    } else {
        if (clearDiscoveryTopic("sensor", "battery_voltage")) return;
        if (clearDiscoveryTopic("sensor", "battery_percent")) return;
        if (clearDiscoveryTopic("binary_sensor", "battery_charging")) return;
    }
    if (publishDiscoveryStep(packetIndex++, [&]() { return publishPacket(
        HaBridge::discoveryTopic(settings_, "sensor", "wifi_rssi").c_str(), 1, true,
        HaBridge::discoveryPayloadSensor(settings_, "wifi_rssi", "Wi-Fi RSSI", HaBridge::networkStateTopic(settings_).c_str(), "{{ value_json.wifiRssi }}", "dBm", "signal_strength", "measurement", "mdi:wifi", -1, configurationUrl).c_str()); })) return;
    if (publishDiscoveryStep(packetIndex++, [&]() { return publishPacket(
        HaBridge::discoveryTopic(settings_, "sensor", "connected_ip").c_str(), 1, true,
        HaBridge::discoveryPayloadSensor(settings_, "connected_ip", "Connected IP", HaBridge::networkStateTopic(settings_).c_str(), "{{ value_json.ip if value_json.wifiConnected and value_json.ip else 'offline' }}", nullptr, nullptr, nullptr, "mdi:ip-network-outline", -1, configurationUrl).c_str()); })) return;
    if (getSystemMetricsSnapshot().chipTemperatureAvailable) {
        if (publishDiscoveryStep(packetIndex++, [&]() { return publishPacket(
            HaBridge::discoveryTopic(settings_, "sensor", "cpu_temperature").c_str(), 1, true,
            HaBridge::discoveryPayloadSensor(settings_, "cpu_temperature", "CPU Temperature", chipTemperatureStateTopic(settings_).c_str(), "{{ value | float(0) | round(1) }}", "°C", "temperature", "measurement", "mdi:thermometer", 1, configurationUrl).c_str()); })) return;
    } else {
        if (clearDiscoveryTopic("sensor", "cpu_temperature")) return;
    }
    if (featureFlags.audio) {
        if (publishDiscoveryStep(packetIndex++, [&]() { return publishPacket(
            HaBridge::discoveryTopic(settings_, "sensor", "playback_state").c_str(), 1, true,
            HaBridge::discoveryPayloadSensor(settings_, "playback_state", "Playback State", HaBridge::playbackStateTopic(settings_).c_str(), "{{ value_json.state }}", nullptr, nullptr, nullptr, "mdi:speaker-wireless", -1, configurationUrl).c_str()); })) return;
    } else {
        if (clearDiscoveryTopic("sensor", "playback_state")) return;
    }
    if (publishDiscoveryStep(packetIndex++, [&]() { return publishPacket(
        HaBridge::discoveryTopic(settings_, "sensor", "firmware_ota_status").c_str(), 1, true,
        HaBridge::discoveryPayloadSensor(settings_, "firmware_ota_status", "Firmware OTA Status", HaBridge::otaStateTopic(settings_).c_str(), "{{ value_json.updateStatus if value_json.busy and value_json.updateStatus else (value_json.phase if value_json.busy else (value_json.lastError if value_json.lastError else (value_json.lastResult if value_json.lastResult else value_json.updateStatus))) }}", nullptr, nullptr, nullptr, "mdi:update", -1, configurationUrl).c_str()); })) return;
    if (publishDiscoveryStep(packetIndex++, [&]() { return publishPacket(
        HaBridge::discoveryTopic(settings_, "sensor", "firmware_installed_version").c_str(), 1, true,
        HaBridge::discoveryPayloadSensor(settings_, "firmware_installed_version", "Installed Firmware", HaBridge::otaStateTopic(settings_).c_str(), "{{ value_json.currentVersion if value_json.currentVersion else 'unknown' }}", nullptr, nullptr, nullptr, "mdi:chip", -1, configurationUrl).c_str()); })) return;
    if (publishDiscoveryStep(packetIndex++, [&]() { return publishPacket(
        HaBridge::discoveryTopic(settings_, "sensor", "firmware_latest_version").c_str(), 1, true,
        HaBridge::discoveryPayloadSensor(settings_, "firmware_latest_version", "Latest Compatible Firmware", HaBridge::otaStateTopic(settings_).c_str(), "{{ value_json.latestVersion if value_json.latestVersion else (value_json.currentVersion if value_json.currentVersion else 'unknown') }}", nullptr, nullptr, nullptr, "mdi:source-branch", -1, configurationUrl).c_str()); })) return;
    if (publishDiscoveryStep(packetIndex++, [&]() { return publishPacket(
        HaBridge::discoveryTopic(settings_, "sensor", "firmware_available_builds").c_str(), 1, true,
        HaBridge::discoveryPayloadSensor(settings_, "firmware_available_builds", "Compatible Firmware Builds", HaBridge::otaStateTopic(settings_).c_str(), "{{ value_json.latestAssetsSummary if value_json.latestAssetsSummary else (value_json.compatibleVersionsSummary if value_json.compatibleVersionsSummary else 'Run Check Firmware Releases') }}", nullptr, nullptr, nullptr, "mdi:format-list-bulleted", -1, configurationUrl).c_str()); })) return;
    if (publishDiscoveryStep(packetIndex++, [&]() { return publishPacket(
        HaBridge::discoveryTopic(settings_, "sensor", "firmware_ota_progress").c_str(), 1, true,
        HaBridge::discoveryPayloadSensor(settings_, "firmware_ota_progress", "Firmware OTA Progress", HaBridge::otaStateTopic(settings_).c_str(), "{{ value_json.progressPercent | int(0) }}", "%", nullptr, nullptr, "mdi:progress-download", 0, configurationUrl).c_str()); })) return;
    if (publishDiscoveryStep(packetIndex++, [&]() { return publishPacket(
        HaBridge::discoveryTopic(settings_, "sensor", "firmware_last_rollback_version").c_str(), 1, true,
        HaBridge::discoveryPayloadSensor(settings_, "firmware_last_rollback_version", "Last Rolled Back Firmware", HaBridge::otaStateTopic(settings_).c_str(), "{{ value_json.rolledBackVersion if value_json.rolledBackVersion else '' }}", nullptr, nullptr, nullptr, "mdi:history", -1, configurationUrl).c_str()); })) return;
    if (publishDiscoveryStep(packetIndex++, [&]() { return publishPacket(
        HaBridge::discoveryTopic(settings_, "sensor", "firmware_last_rollback_reason").c_str(), 1, true,
        HaBridge::discoveryPayloadSensor(settings_, "firmware_last_rollback_reason", "Last Rollback Reason", HaBridge::otaStateTopic(settings_).c_str(), "{{ value_json.rollbackReason if value_json.rollbackReason else '' }}", nullptr, nullptr, nullptr, "mdi:alert-circle-outline", -1, configurationUrl).c_str()); })) return;
    if (featureFlags.audio) {
        if (publishDiscoveryStep(packetIndex++, [&]() { return publishPacket(
            HaBridge::discoveryTopic(settings_, "number", "volume").c_str(), 1, true,
            HaBridge::discoveryPayloadNumber(settings_, "volume", "Notifier Volume", (settings_.mqtt.baseTopic + "/state/volume").c_str(), HaBridge::commandTopic(settings_, "volume").c_str(), 0, 100, 1, "%", "mdi:volume-high", configurationUrl).c_str()); })) return;
        if (publishDiscoveryStep(packetIndex++, [&]() { return publishPacket(
            HaBridge::discoveryTopic(settings_, "button", "alarm_trigger").c_str(), 1, true,
            HaBridge::discoveryPayloadButton(settings_, "alarm_trigger", "Alarm Trigger", HaBridge::commandTopic(settings_, "alarm").c_str(), "trigger", "mdi:alarm-bell", configurationUrl).c_str()); })) return;
        if (publishDiscoveryStep(packetIndex++, [&]() { return publishPacket(
            HaBridge::discoveryTopic(settings_, "button", "alarm_stop").c_str(), 1, true,
            HaBridge::discoveryPayloadButton(settings_, "alarm_stop", "Alarm Stop", HaBridge::commandTopic(settings_, "alarm").c_str(), "stop", "mdi:alarm-off", configurationUrl).c_str()); })) return;
        if (publishDiscoveryStep(packetIndex++, [&]() { return publishPacket(
            HaBridge::discoveryTopic(settings_, "button", "notify").c_str(), 1, true,
            HaBridge::discoveryPayloadButton(settings_, "notify", "Play Notification Cue", HaBridge::commandTopic(settings_, "notify").c_str(), "notify", "mdi:message-badge", configurationUrl).c_str()); })) return;
    } else {
        if (clearDiscoveryTopic("number", "volume")) return;
        if (clearDiscoveryTopic("button", "alarm_trigger")) return;
        if (clearDiscoveryTopic("button", "alarm_stop")) return;
        if (clearDiscoveryTopic("button", "notify")) return;
    }
    if (featureFlags.display) {
        if (publishDiscoveryStep(packetIndex++, [&]() { return publishPacket(
            HaBridge::discoveryTopic(settings_, "button", "display_trigger").c_str(), 1, true,
            HaBridge::discoveryPayloadButton(settings_, "display_trigger", "Display Trigger", HaBridge::commandTopic(settings_, "display_trigger").c_str(), "trigger", "mdi:gesture-tap-button", configurationUrl).c_str()); })) return;
    } else {
        if (clearDiscoveryTopic("button", "display_trigger")) return;
    }
    if (publishDiscoveryStep(packetIndex++, [&]() { return publishPacket(
        HaBridge::discoveryTopic(settings_, "button", "reboot").c_str(), 1, true,
        HaBridge::discoveryPayloadButton(settings_, "reboot", "Reboot Device", HaBridge::commandTopic(settings_, "reboot").c_str(), "reboot", "mdi:restart", configurationUrl).c_str()); })) return;
    if (featureFlags.storage) {
        if (publishDiscoveryStep(packetIndex++, [&]() { return publishPacket(
            HaBridge::discoveryTopic(settings_, "button", "storage_sd_remount").c_str(), 1, true,
            HaBridge::discoveryPayloadButton(settings_, "storage_sd_remount", "Remount SD Card", HaBridge::commandTopic(settings_, "storage/sd_remount").c_str(), "remount", "mdi:sd", configurationUrl).c_str()); })) return;
    } else {
        if (clearDiscoveryTopic("button", "storage_sd_remount")) return;
    }
    if (publishDiscoveryStep(packetIndex++, [&]() { return publishPacket(
        HaBridge::discoveryTopic(settings_, "button", "web_ui_lock").c_str(), 1, true,
        HaBridge::discoveryPayloadButton(settings_, "web_ui_lock", "Lock Web UI", HaBridge::commandTopic(settings_, "web_ui").c_str(), "lock", "mdi:web-off", configurationUrl).c_str()); })) return;
    if (publishDiscoveryStep(packetIndex++, [&]() { return publishPacket(
        HaBridge::discoveryTopic(settings_, "button", "web_ui_unlock").c_str(), 1, true,
        HaBridge::discoveryPayloadButton(settings_, "web_ui_unlock", "Unlock Web UI", HaBridge::commandTopic(settings_, "web_ui").c_str(), "unlock", "mdi:web-check", configurationUrl).c_str()); })) return;
    if (publishDiscoveryStep(packetIndex++, [&]() { return publishPacket(
        HaBridge::discoveryTopic(settings_, "select", "firmware_version_select").c_str(), 1, true,
        HaBridge::discoveryPayloadSelect(settings_, "firmware_version_select", "Firmware Version", HaBridge::otaStateTopic(settings_).c_str(), HaBridge::commandTopic(settings_, "ota/select_version").c_str(), firmwareOptions, "mdi:format-list-bulleted-square", "{{ value_json.selectedOption if value_json.selectedOption else '' }}", configurationUrl).c_str()); })) return;
    if (featureFlags.audio) {
        if (publishDiscoveryStep(packetIndex++, [&]() { return publishPacket(
            HaBridge::discoveryTopic(settings_, "button", "stop").c_str(), 1, true,
            HaBridge::discoveryPayloadButton(settings_, "stop", "Stop Playback", HaBridge::commandTopic(settings_, "stop").c_str(), "stop", "mdi:stop", configurationUrl).c_str()); })) return;
    } else {
        if (clearDiscoveryTopic("button", "stop")) return;
    }
    if (publishDiscoveryStep(packetIndex++, [&]() { return publishPacket(
        HaBridge::discoveryTopic(settings_, "button", "firmware_check").c_str(), 1, true,
        HaBridge::discoveryPayloadButton(settings_, "firmware_check", "Check Firmware Releases", HaBridge::commandTopic(settings_, "ota/check").c_str(), "check", "mdi:update", configurationUrl).c_str()); })) return;
    if (publishDiscoveryStep(packetIndex++, [&]() { return publishPacket(
        HaBridge::discoveryTopic(settings_, "switch", "firmware_auto_update").c_str(), 1, true,
        HaBridge::discoveryPayloadSwitch(settings_, "firmware_auto_update", "Firmware Auto Update", HaBridge::otaStateTopic(settings_).c_str(), HaBridge::commandTopic(settings_, "ota/auto_update").c_str(), "{{ 'ON' if value_json.autoUpdate else 'OFF' }}", "mdi:auto-upload", configurationUrl).c_str()); })) return;
    if (publishDiscoveryStep(packetIndex++, [&]() { return publishPacket(
        HaBridge::discoveryTopic(settings_, "button", "firmware_install").c_str(), 1, true,
        HaBridge::discoveryPayloadButton(settings_, "firmware_install", "Install Firmware", HaBridge::commandTopic(settings_, "ota/install").c_str(), "install", "mdi:package-up", configurationUrl).c_str()); })) return;
    if (featureFlags.audio) {
        if (publishDiscoveryStep(packetIndex++, [&]() { return publishPacket(
            HaBridge::discoveryTopic(settings_, "text", "play_url").c_str(), 1, true,
            HaBridge::discoveryPayloadText(settings_, "play_url", "Play URL", HaBridge::commandTopic(settings_, "play").c_str(), "mdi:link", HaBridge::playbackStateTopic(settings_).c_str(), "{{ value_json.url if value_json.url else '' }}", configurationUrl).c_str()); })) return;
    } else {
        if (clearDiscoveryTopic("text", "play_url")) return;
    }
    for (uint8_t channelIndex = 0; channelIndex < 2; ++channelIndex) {
        if (!featureFlags.motorChannels[channelIndex]) {
            continue;
        }

        const String channelSuffix = motorChannelSuffix(channelIndex);
        const String channelName = motorChannelLabel(channelIndex);
        const char* channelStatusTemplate = channelIndex == 0
            ? "{{ value_json.channels[0].statusText if value_json.channels and value_json.channels|count > 0 else 'idle' }}"
            : "{{ value_json.channels[1].statusText if value_json.channels and value_json.channels|count > 1 else 'idle' }}";
        const char* channelSwitchTemplate = channelIndex == 0
            ? "{{ 'OPEN' if value_json.channels and value_json.channels|count > 0 and value_json.channels[0].positionState in ['open', 'opening'] else 'CLOSE' }}"
            : "{{ 'OPEN' if value_json.channels and value_json.channels|count > 1 and value_json.channels[1].positionState in ['open', 'opening'] else 'CLOSE' }}";
        const char* openDurationTemplate = channelIndex == 0
            ? "{{ value_json.channels[0].openDurationMs | int(5000) if value_json.channels and value_json.channels|count > 0 else 5000 }}"
            : "{{ value_json.channels[1].openDurationMs | int(5000) if value_json.channels and value_json.channels|count > 1 else 5000 }}";
        const char* closeDurationTemplate = channelIndex == 0
            ? "{{ value_json.channels[0].closeDurationMs | int(5000) if value_json.channels and value_json.channels|count > 0 else 5000 }}"
            : "{{ value_json.channels[1].closeDurationMs | int(5000) if value_json.channels and value_json.channels|count > 1 else 5000 }}";

        if (publishDiscoveryStep(packetIndex++, [&]() { return publishPacket(
            HaBridge::discoveryTopic(settings_, "sensor", (channelSuffix + String("_status")).c_str()).c_str(), 1, true,
            HaBridge::discoveryPayloadSensor(settings_, (channelSuffix + String("_status")).c_str(), (channelName + " Status").c_str(), HaBridge::motorStateTopic(settings_).c_str(), channelStatusTemplate, nullptr, nullptr, nullptr, "mdi:garage-variant", -1, configurationUrl).c_str()); })) return;
        if (publishDiscoveryStep(packetIndex++, [&]() { return publishPacket(
            HaBridge::discoveryTopic(settings_, "switch", (channelSuffix + String("_valve")).c_str()).c_str(), 1, true,
            HaBridge::discoveryPayloadSwitch(settings_, (channelSuffix + String("_valve")).c_str(), (channelName + " Valve").c_str(), HaBridge::motorStateTopic(settings_).c_str(), HaBridge::commandTopic(settings_, (String("motor/") + channelSuffix + "/switch").c_str()).c_str(), channelSwitchTemplate, "mdi:valve", configurationUrl, "OPEN", "CLOSE", false).c_str()); })) return;
        if (publishDiscoveryStep(packetIndex++, [&]() { return publishPacket(
            HaBridge::discoveryTopic(settings_, "button", (channelSuffix + String("_open")).c_str()).c_str(), 1, true,
            HaBridge::discoveryPayloadButton(settings_, (channelSuffix + String("_open")).c_str(), (channelName + " Open").c_str(), HaBridge::commandTopic(settings_, (String("motor/") + channelSuffix + "/open").c_str()).c_str(), "OPEN", "mdi:arrow-expand-horizontal", configurationUrl).c_str()); })) return;
        if (publishDiscoveryStep(packetIndex++, [&]() { return publishPacket(
            HaBridge::discoveryTopic(settings_, "button", (channelSuffix + String("_close")).c_str()).c_str(), 1, true,
            HaBridge::discoveryPayloadButton(settings_, (channelSuffix + String("_close")).c_str(), (channelName + " Close").c_str(), HaBridge::commandTopic(settings_, (String("motor/") + channelSuffix + "/close").c_str()).c_str(), "CLOSE", "mdi:arrow-collapse-horizontal", configurationUrl).c_str()); })) return;
        if (publishDiscoveryStep(packetIndex++, [&]() { return publishPacket(
            HaBridge::discoveryTopic(settings_, "number", (channelSuffix + String("_open_duration")).c_str()).c_str(), 1, true,
            HaBridge::discoveryPayloadNumber(settings_, (channelSuffix + String("_open_duration")).c_str(), (channelName + " Open Duration").c_str(), HaBridge::motorStateTopic(settings_).c_str(), HaBridge::commandTopic(settings_, (String("motor/") + channelSuffix + "/open/duration").c_str()).c_str(), 100, 600000, 100, "ms", "mdi:timer-play-outline", configurationUrl, openDurationTemplate).c_str()); })) return;
        if (publishDiscoveryStep(packetIndex++, [&]() { return publishPacket(
            HaBridge::discoveryTopic(settings_, "number", (channelSuffix + String("_close_duration")).c_str()).c_str(), 1, true,
            HaBridge::discoveryPayloadNumber(settings_, (channelSuffix + String("_close_duration")).c_str(), (channelName + " Close Duration").c_str(), HaBridge::motorStateTopic(settings_).c_str(), HaBridge::commandTopic(settings_, (String("motor/") + channelSuffix + "/close/duration").c_str()).c_str(), 100, 600000, 100, "ms", "mdi:timer-stop-outline", configurationUrl, closeDurationTemplate).c_str()); })) return;
    }
    for (uint8_t channelIndex = 0; channelIndex < 2; ++channelIndex) {
        if (featureFlags.motorChannels[channelIndex]) {
            continue;
        }
        const String channelSuffix = motorChannelSuffix(channelIndex);
        if (clearDiscoveryTopic("sensor", (channelSuffix + String("_status")).c_str())) return;
        if (clearDiscoveryTopic("switch", (channelSuffix + String("_valve")).c_str())) return;
        if (clearDiscoveryTopic("button", (channelSuffix + String("_open")).c_str())) return;
        if (clearDiscoveryTopic("button", (channelSuffix + String("_close")).c_str())) return;
        if (clearDiscoveryTopic("number", (channelSuffix + String("_open_duration")).c_str())) return;
        if (clearDiscoveryTopic("number", (channelSuffix + String("_close_duration")).c_str())) return;
    }
    if (publishDiscoveryStep(packetIndex++, [&]() { return publishPacket(
        HaBridge::discoveryTopic(settings_, "button", "firmware_install_latest").c_str(), 1, true,
        ""); })) return;
    if (publishDiscoveryStep(packetIndex++, [&]() { return publishPacket(
        HaBridge::discoveryTopic(settings_, "text", "firmware_install_version").c_str(), 1, true,
        ""); })) return;
#ifdef APP_ENABLE_HACS_MQTT
    if (featureFlags.audio) {
        if (publishDiscoveryStep(packetIndex++, [&]() { return publishPacket(
            HaBridge::hacsMediaPlayerDiscoveryTopic(settings_).c_str(), 1, true,
            HaBridge::discoveryPayloadHacsMediaPlayer(settings_).c_str()); })) return;
    } else {
        if (publishDiscoveryStep(packetIndex++, [&]() { return publishPacket(HaBridge::hacsMediaPlayerDiscoveryTopic(settings_).c_str(), 1, true, ""); })) return;
    }
    if (publishDiscoveryStep(packetIndex++, [&]() { return publishPacket(
        HaBridge::discoveryTopic(settings_, "media_player", "hacs_player").c_str(), 1, true,
        ""); })) return;
#endif
    lastOtaDiscoverySignature_ = otaDiscoverySignature;
    discoveryCursor_ = 0;
    discoveryPublishPending_ = false;
    discoveryPublishedForSession_ = true;
}

bool MqttManager::publishButtonActionEvent(const String& buttonLabel, uint8_t pin, const String& action) {
    if (!client_.connected()) {
        return false;
    }

    JsonDocument payload;
    payload["action"] = action;
    payload["button"] = buttonLabel;
    payload["pin"] = pin;
    payload["source"] = "touch";
    publishJson(settings_.mqtt.baseTopic + "/event/button_action", payload, false);
    return true;
}

bool MqttManager::isConnected() const {
    return client_.connected();
}

bool MqttManager::shouldRebootForRecovery() const {
    return recoveryRebootRecommended_;
}

uint8_t MqttManager::consecutiveFailureCount() const {
    return consecutiveFailureCount_;
}

bool MqttManager::requestConnect(String& error) {
    if (settings_.mqtt.host.isEmpty()) {
        error = "Enter an MQTT host first.";
        return false;
    }

    error = "";
    connectionEnabled_ = true;
    if (client_.connected()) {
        if (settings_.mqtt.discoveryEnabled && !discoveryPublishedForSession_) {
            discoveryPublishPending_ = true;
        }
        statePublishPending_ = true;
        return true;
    }

    if (lastConnectAttemptAt_ != 0 && millis() - lastConnectAttemptAt_ < MQTT_RETRY_INTERVAL_MS) {
        return true;
    }

    configureClient();
    return true;
}

bool MqttManager::requestDisconnect(String& error) {
    error = "";
    connectionEnabled_ = false;
    consecutiveFailureCount_ = 0;
    recoveryRebootRecommended_ = false;
    lastConnectAttemptAt_ = millis();
    lastBrokerActivityAt_ = 0;
    client_.disconnect(true);
    if (appState_ != nullptr) {
        appState_->setMqttConnected(false);
    }
    return true;
}

bool MqttManager::requestRediscovery(String& error) {
    if (!settings_.mqtt.discoveryEnabled) {
        error = "Home Assistant discovery is disabled.";
        return false;
    }
    if (!client_.connected()) {
        error = "MQTT must be connected before discovery can be republished.";
        return false;
    }

    publishDiscovery();
    statePublishPending_ = true;
    error = "";
    return true;
}

void MqttManager::registerFailedAttempt(AsyncMqttClientDisconnectReason reason) {
    if (!connectionEnabled_ || settings_.mqtt.host.isEmpty() || client_.connected()) {
        return;
    }

    if (wifiManager_ == nullptr || !wifiManager_->isConnected()) {
        return;
    }

    if (consecutiveFailureCount_ < MQTT_MAX_CONSECUTIVE_FAILURES) {
        ++consecutiveFailureCount_;
    }

    DebugLog.printf("[mqtt] connect failed reason=%d count=%u/%u\n", static_cast<int>(reason),
                  static_cast<unsigned>(consecutiveFailureCount_),
                  static_cast<unsigned>(MQTT_MAX_CONSECUTIVE_FAILURES));

    if (isCredentialFailureReason(reason)) {
        setFrontendError("MQTT broker rejected the configured client ID or credentials.");
        recoveryRebootRecommended_ = false;
        return;
    }

    if (consecutiveFailureCount_ >= MQTT_MAX_CONSECUTIVE_FAILURES) {
        setFrontendError("MQTT broker unreachable. The device will keep retrying without rebooting.");
        recoveryRebootRecommended_ = false;
        DebugLog.println("[mqtt] max consecutive failures reached, continuing retries without recovery reboot");
    }
}

bool MqttManager::isCredentialFailureReason(AsyncMqttClientDisconnectReason reason) const {
    switch (reason) {
        case AsyncMqttClientDisconnectReason::MQTT_UNACCEPTABLE_PROTOCOL_VERSION:
        case AsyncMqttClientDisconnectReason::MQTT_IDENTIFIER_REJECTED:
        case AsyncMqttClientDisconnectReason::MQTT_MALFORMED_CREDENTIALS:
        case AsyncMqttClientDisconnectReason::MQTT_NOT_AUTHORIZED:
            return true;
        default:
            return false;
    }
}

void MqttManager::noteBrokerActivity() {
    lastBrokerActivityAt_ = millis();
}

void MqttManager::clearFrontendError() {
    if (appState_ != nullptr) {
        appState_->setLastError("");
    }
}

void MqttManager::setFrontendError(const String& message) {
    if (appState_ != nullptr) {
        appState_->setLastError(message);
    }
}

bool MqttManager::publishLogicMessage(const String& topic,const String& payload,bool retained,uint8_t qos,String& error) {
    if(topic.isEmpty()||topic.length()>192||topic.indexOf('+')>=0||topic.indexOf('#')>=0||qos!=1||payload.length()>1024){error="Invalid MQTT topic, payload or QoS (must be 1)";return false;}
    for(size_t i=0;i<topic.length();++i)if(uint8_t(topic[i])<32){error="MQTT topic contains a control character";return false;}
    if(!isConnected()){error="MQTT broker is not connected";return false;}
    if(!publishPacket(topic.c_str(),qos,retained,payload.c_str(),payload.length())){error="MQTT publish queue full or insufficient memory";return false;}
    error="";return true;
}
