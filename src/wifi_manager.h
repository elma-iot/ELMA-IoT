#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <IPAddress.h>
#include <WiFi.h>
#include <esp_wifi_types.h>

#include "app_state.h"
#include "settings_schema.h"

class WiFiManager {
  public:
    struct ScanSnapshot {
        bool active = false;
        bool complete = false;
        bool failed = false;
        uint32_t ageMs = 0;
    };

    void begin(const SettingsBundle& settings, AppState& appState);
    void applySettings(const SettingsBundle& settings);
    void loop();
    void setLowLatencyMode(bool enabled);

    bool isConnected() const;
    bool isApMode() const;
    bool shouldRebootForRecovery() const;
    uint8_t consecutiveFailureCount() const;
    IPAddress localIp() const;
    IPAddress apIp() const;
    String currentSsid() const;
    String apSsid() const;
    int32_t rssi() const;
    ScanSnapshot getScanSnapshot();
    bool startScan();
    void appendScanResultsJson(JsonArray networks);
    void appendTxPowerStatus(JsonObject network) const;
    bool shouldRedirectCaptivePortal(const String& hostHeader) const;
    bool prepareStationHandoff(IPAddress& stationIp, uint32_t& shutdownDelayMs);

  private:
    struct PreferredAccessPoint {
        bool found = false;
        int32_t rssi = INT32_MIN;
        int32_t channel = 0;
        uint8_t bssid[6] = {0, 0, 0, 0, 0, 0};
    };

    static constexpr uint16_t DNS_PORT = 53;
    static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;
    static constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 15000;
    static constexpr uint32_t WIFI_AP_HANDOFF_TIMEOUT_MS = 60000;
    static constexpr uint32_t WIFI_AP_HANDOFF_SHUTDOWN_DELAY_MS = 2500;
    static constexpr uint32_t WIFI_USER_SCAN_TIMEOUT_MS = 20000;
    static constexpr uint32_t WIFI_STATE_PUBLISH_INTERVAL_MS = 1000;
    static constexpr uint8_t WIFI_MAX_CONSECUTIVE_FAILURES = 10;

    SettingsBundle settings_;
    AppState* appState_ = nullptr;
    DNSServer dnsServer_;
    bool initialized_ = false;
    bool dnsStarted_ = false;
    bool apMode_ = false;
    bool lowLatencyMode_ = false;
    bool stationAttemptActive_ = false;
    bool userScanActive_ = false;
    bool resumeStationAfterScan_ = false;
    bool apShutdownPending_ = false;
    bool hadConnection_ = false;
    bool recoveryRebootRecommended_ = false;
    bool skipBssidSelectionForNextConnect_ = false;
    bool lastScanCompleted_ = false;
    bool lastDisconnectReasonValid_ = false;
    uint8_t consecutiveFailureCount_ = 0;
    unsigned long connectAttemptStartedAt_ = 0;
    unsigned long lastConnectAttemptAt_ = 0;
    unsigned long lastScanStartedAt_ = 0;
    unsigned long apShutdownAt_ = 0;
    unsigned long lastStatePublishAt_ = 0;
    wifi_event_id_t disconnectEventId_ = 0;
    wifi_err_reason_t lastDisconnectReason_ = WIFI_REASON_UNSPECIFIED;
    String apSsid_;
    int txPowerApplyError_ = 0;

    void startStation();
    void startAccessPoint();
    void stopAccessPoint();
    void updateAppState(bool rateLimited = false);
    bool hasStaCredentials() const;
    bool isConfiguredNetworkVisible();
    bool isCredentialFailureReason(wifi_err_reason_t reason) const;
    bool isNetworkMissingReason(wifi_err_reason_t reason) const;
    void clearFrontendError();
    void setFrontendError(const String& message);
    void handleDisconnectEvent(arduino_event_info_t info);
    void registerFailedAttempt(const char* reason);
    void finishUserScan();
    PreferredAccessPoint findPreferredAccessPoint();
    void updateRadioModeAndSleep();
    void applyRadioSleep();
    void applyRadioTxPower();
};
