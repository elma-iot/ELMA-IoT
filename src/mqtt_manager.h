#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncMqttClient.h>
#include <functional>
#include <atomic>

#include "app_state.h"
#include "ha_bridge.h"
#include "ota_manager.h"
#include "settings_schema.h"
#include "wifi_manager.h"

class MqttManager {
  public:
    using CommandHandler = std::function<void(const PlaybackCommand& command)>;
    using MotorStatusAppender = std::function<void(JsonObject)>;

    void begin(const SettingsBundle& settings, AppState& appState, WiFiManager& wifiManager, OtaManager& otaManager, CommandHandler commandHandler, MotorStatusAppender motorStatusAppender = nullptr);
    void applySettings(const SettingsBundle& settings);
    void loop();
    void publishState();
    void publishBattery(float voltage, float rawAdcVoltage, uint16_t rawAdc, bool charging);
    void publishChipTemperature();
    void publishDiscovery();
    bool publishButtonActionEvent(const String& buttonLabel, uint8_t pin, const String& action);
    bool publishLogicMessage(const String& topic,const String& payload,bool retained,uint8_t qos,String& error);
    bool isConnected() const;
    bool requestConnect(String& error);
    bool requestDisconnect(String& error);
    bool requestRediscovery(String& error);
    bool shouldRebootForRecovery() const;
    uint8_t consecutiveFailureCount() const;

  private:
    static constexpr uint8_t MQTT_MAX_CONSECUTIVE_FAILURES = 10;
    static constexpr uint16_t MQTT_KEEP_ALIVE_SECONDS = 15;
    static constexpr uint32_t MQTT_RETRY_INTERVAL_MS = 5000UL;
    static constexpr uint32_t MQTT_STALE_CONNECTION_MS = 90000UL;

    AsyncMqttClient client_;
    SettingsBundle settings_;
    AppState* appState_ = nullptr;
    WiFiManager* wifiManager_ = nullptr;
    OtaManager* otaManager_ = nullptr;
    CommandHandler commandHandler_;
    MotorStatusAppender motorStatusAppender_;
    bool configured_ = false;
    bool connectionEnabled_ = true;
    bool recoveryRebootRecommended_ = false;
    std::atomic<bool> discoveryPublishPending_{false};
    std::atomic<bool> discoveryRestartPending_{false};
    std::atomic<bool> statePublishPending_{false};
    std::atomic<uint8_t> pendingPublishes_{0};
    String clientHost_, clientId_, clientUsername_, clientPassword_, clientWillTopic_;
    TaskHandle_t publisherTask_ = nullptr;
    size_t discoveryCursor_ = 0;
    size_t stateCursor_ = 0;
    size_t statePassIndex_ = 0;
    bool statePassActive_ = false;
    bool statePassBlocked_ = false;
    unsigned long lastDiscoveryStepAt_ = 0;
    unsigned long lastStateAttemptAt_ = 0;
    bool discoveryPublishedForSession_ = false;
    bool wifiWasConnected_ = false;
    String lastOtaDiscoverySignature_;
    uint8_t consecutiveFailureCount_ = 0;
    unsigned long lastConnectAttemptAt_ = 0;
    unsigned long lastStatePublishAt_ = 0;
    unsigned long lastBrokerActivityAt_ = 0;

    void configureClient();
    void connectIfNeeded();
    void handleWiFiState();
    void handleConnected(bool sessionPresent);
    void handleDisconnected(AsyncMqttClientDisconnectReason reason);
    void handleMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total);
    void publishJson(const String& topic, const JsonDocument& doc, bool retained);
    uint16_t publishPacket(const char* topic, uint8_t qos, bool retained, const char* payload, size_t length = 0);
    void releasePublishSlot();
    bool publishDiscoveryStep(size_t index, const std::function<uint16_t()>& send);
    void publishDiscoveryNow();
    void publishStateNow();
    void publishChipTemperatureNow();
    String currentConfigUrl() const;
    void noteBrokerActivity();
    bool isCredentialFailureReason(AsyncMqttClientDisconnectReason reason) const;
    void clearFrontendError();
    void setFrontendError(const String& message);
    void registerFailedAttempt(AsyncMqttClientDisconnectReason reason);
};
