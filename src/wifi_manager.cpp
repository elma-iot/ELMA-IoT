#include "device_log.h"
#include "wifi_manager.h"

#include "default_config.h"
#include "wifi_power_policy.h"
#include <esp_wifi.h>

namespace {
String defaultApName() {
    uint64_t chipId = ESP.getEfuseMac();
    uint32_t shortId = static_cast<uint32_t>(chipId & 0xFFFFFF);
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%s-%06lX", DefaultConfig::WIFI_AP_SSID_PREFIX, static_cast<unsigned long>(shortId));
    return String(buffer);
}

bool wifiRestartRequired(const SettingsBundle& current, const SettingsBundle& next) {
    return current.device.deviceName != next.device.deviceName ||
           current.wifi.ssid != next.wifi.ssid ||
           current.wifi.password != next.wifi.password ||
           current.wifi.apFallbackEnabled != next.wifi.apFallbackEnabled ||
           current.wifi.apSsid != next.wifi.apSsid ||
           current.wifi.apPassword != next.wifi.apPassword ||
           current.wifi.useStaticIp != next.wifi.useStaticIp ||
           current.wifi.staticIp != next.wifi.staticIp ||
           current.wifi.gateway != next.wifi.gateway ||
           current.wifi.subnet != next.wifi.subnet ||
           current.wifi.dns1 != next.wifi.dns1 ||
           current.wifi.dns2 != next.wifi.dns2;
}
}  // namespace

void WiFiManager::updateRadioModeAndSleep() {
    wifi_mode_t targetMode = WIFI_MODE_NULL;
    if (hasStaCredentials()) {
        targetMode = apMode_ ? WIFI_MODE_APSTA : WIFI_MODE_STA;
    } else if (apMode_) {
        targetMode = WIFI_MODE_AP;
    }

    WiFi.mode(targetMode);

    applyRadioSleep();
    applyRadioTxPower();
}

void WiFiManager::applyRadioSleep() {
    // Keep streaming/update latency low, but do not leave the RF receiver
    // continuously powered after playback stops. STA modem sleep keeps the
    // association and MQTT connection alive between beacon intervals.
    WiFi.setSleep(!apMode_ && !lowLatencyMode_);
}

void WiFiManager::setLowLatencyMode(bool enabled) {
    if (lowLatencyMode_ == enabled) return;
    lowLatencyMode_ = enabled;
    if (initialized_) applyRadioSleep();
}

void WiFiManager::applyRadioTxPower() {
    const wifi_mode_t mode = WiFi.getMode();
    if (mode == WIFI_MODE_NULL) return;
    const int8_t requested = WifiPowerPolicy::requestedQuarterDbm(
        settings_.wifi.staTxPowerDbm, settings_.wifi.apTxPowerDbm,
        (mode & WIFI_MODE_STA) != 0, (mode & WIFI_MODE_AP) != 0);
    // The ESP has one radio. AP+STA uses the higher configured limit; do not
    // reconnect or change credentials merely to apply a power-only edit.
    txPowerApplyError_ = esp_wifi_set_max_tx_power(requested);
    if (txPowerApplyError_ != ESP_OK) {
        DebugLog.printf("[wifi] transmit power apply failed: %d\n", txPowerApplyError_);
    }
}

void WiFiManager::appendTxPowerStatus(JsonObject network) const {
    const wifi_mode_t mode = WiFi.getMode();
    wifi_ps_type_t sleepMode = WIFI_PS_NONE;
    if (esp_wifi_get_ps(&sleepMode) == ESP_OK) network["modemSleepEnabled"] = sleepMode != WIFI_PS_NONE;
    int8_t activePower = 0;
    const bool available = mode != WIFI_MODE_NULL && esp_wifi_get_max_tx_power(&activePower) == ESP_OK;
    network["staTxPowerDbm"] = settings_.wifi.staTxPowerDbm;
    network["apTxPowerDbm"] = settings_.wifi.apTxPowerDbm;
    network["txPowerAvailable"] = available;
    if (available) network["txPowerDbm"] = activePower / 4.0f;
    network["txPowerMode"] = mode == WIFI_MODE_APSTA ? "AP+STA" : (mode == WIFI_MODE_AP ? "AP" : (mode == WIFI_MODE_STA ? "STA" : "Off"));
    network["txPowerApplyError"] = txPowerApplyError_;
}

void WiFiManager::begin(const SettingsBundle& settings, AppState& appState) {
    appState_ = &appState;
    WiFi.setAutoReconnect(true);
    if (disconnectEventId_ == 0) {
        disconnectEventId_ = WiFi.onEvent(
            [this](arduino_event_id_t, arduino_event_info_t info) { handleDisconnectEvent(info); },
            ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    }
    applySettings(settings);
}

void WiFiManager::applySettings(const SettingsBundle& settings) {
    const bool needsRestart = !initialized_ || wifiRestartRequired(settings_, settings);
    settings_ = settings;
    if (!hasStaCredentials()) {
        consecutiveFailureCount_ = 0;
        recoveryRebootRecommended_ = false;
        lastDisconnectReasonValid_ = false;
        clearFrontendError();
    }
    if (settings_.device.deviceName.length() > 0) {
        WiFi.setHostname(settings_.device.deviceName.c_str());
    }
    apSsid_ = settings_.wifi.apSsid.isEmpty() ? defaultApName() : settings_.wifi.apSsid;
    if (!needsRestart) {
        applyRadioTxPower();
        updateAppState();
        initialized_ = true;
        return;
    }

    stopAccessPoint();
    startStation();
    initialized_ = true;
}

void WiFiManager::startStation() {
    updateRadioModeAndSleep();
    // Disconnect only the STA interface. Powering the radio down here also
    // destroys the fallback AP and drops every browser using the setup page.
    // Credentials are owned by SettingsManager, so there is no reason to erase
    // the Wi-Fi driver's copy on every retry either.
    WiFi.disconnect(false, false);
    delay(100);
    if (!hasStaCredentials()) {
        stationAttemptActive_ = false;
        consecutiveFailureCount_ = 0;
        recoveryRebootRecommended_ = false;
        lastDisconnectReasonValid_ = false;
        clearFrontendError();
        startAccessPoint();
        updateAppState();
        return;
    }

    if (settings_.wifi.useStaticIp) {
        IPAddress ip;
        IPAddress gateway;
        IPAddress subnet;
        IPAddress dns1;
        IPAddress dns2;
        if (parseIp(settings_.wifi.staticIp, ip) && parseIp(settings_.wifi.gateway, gateway) && parseIp(settings_.wifi.subnet, subnet)) {
            const bool hasDns1 = parseIp(settings_.wifi.dns1, dns1);
            const bool hasDns2 = parseIp(settings_.wifi.dns2, dns2);
            WiFi.config(ip, gateway, subnet, hasDns1 ? dns1 : gateway, hasDns2 ? dns2 : gateway);
        }
    } else {
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
    }

    if (settings_.wifi.apFallbackEnabled) {
        startAccessPoint();
    }

    const bool usePreferredBssid = !skipBssidSelectionForNextConnect_;
    const PreferredAccessPoint preferredAccessPoint = usePreferredBssid ? findPreferredAccessPoint() : PreferredAccessPoint{};
    if (preferredAccessPoint.found && preferredAccessPoint.channel > 0) {
        WiFi.begin(settings_.wifi.ssid.c_str(), settings_.wifi.password.c_str(), preferredAccessPoint.channel,
                   preferredAccessPoint.bssid, true);
        DebugLog.printf("[wifi] selected strongest BSSID for ssid='%s' rssi=%ld channel=%ld bssid=%02X:%02X:%02X:%02X:%02X:%02X\n",
                      settings_.wifi.ssid.c_str(),
                      static_cast<long>(preferredAccessPoint.rssi),
                      static_cast<long>(preferredAccessPoint.channel),
                      preferredAccessPoint.bssid[0], preferredAccessPoint.bssid[1], preferredAccessPoint.bssid[2],
                      preferredAccessPoint.bssid[3], preferredAccessPoint.bssid[4], preferredAccessPoint.bssid[5]);
    } else {
        WiFi.begin(settings_.wifi.ssid.c_str(), settings_.wifi.password.c_str());
        DebugLog.printf("[wifi] %s for ssid='%s', using default station connect\n",
                      usePreferredBssid ? "no matching BSSID scan result" : "skipping BSSID pin after auth failure",
                      settings_.wifi.ssid.c_str());
    }
    skipBssidSelectionForNextConnect_ = false;
    applyRadioTxPower();
    stationAttemptActive_ = true;
    connectAttemptStartedAt_ = millis();
    lastConnectAttemptAt_ = connectAttemptStartedAt_;
    DebugLog.printf("[wifi] connect attempt %u/%u to ssid='%s'\n", static_cast<unsigned>(consecutiveFailureCount_ + 1),
                  static_cast<unsigned>(WIFI_MAX_CONSECUTIVE_FAILURES), settings_.wifi.ssid.c_str());
    updateAppState();
}

void WiFiManager::startAccessPoint() {
    if (!settings_.wifi.apFallbackEnabled && hasStaCredentials()) {
        return;
    }
    if (apMode_) {
        return;
    }
    apMode_ = true;
    updateRadioModeAndSleep();
    WiFi.softAP(apSsid_.c_str(), settings_.wifi.apPassword.c_str());
    applyRadioTxPower();
    dnsServer_.start(DNS_PORT, "*", WiFi.softAPIP());
    dnsStarted_ = true;
    apShutdownPending_ = false;
    apShutdownAt_ = 0;
    DebugLog.printf("[wifi] AP started ssid='%s' ip=%s\n", apSsid_.c_str(), WiFi.softAPIP().toString().c_str());
    updateAppState();
}

void WiFiManager::stopAccessPoint() {
    if (dnsStarted_) {
        dnsServer_.stop();
        dnsStarted_ = false;
    }
    if (apMode_) {
        WiFi.softAPdisconnect(true);
        apMode_ = false;
        updateRadioModeAndSleep();
    }
    apShutdownPending_ = false;
    apShutdownAt_ = 0;
}

void WiFiManager::loop() {
    if (dnsStarted_) {
        dnsServer_.processNextRequest();
    }

    const bool connected = WiFi.status() == WL_CONNECTED;
    if (connected) {
        if (!hadConnection_) {
            applyRadioTxPower();
            if (consecutiveFailureCount_ > 0) {
                DebugLog.printf("[wifi] connected after %u failed attempt(s)\n", static_cast<unsigned>(consecutiveFailureCount_));
            }
            DebugLog.printf("[wifi] station connected ssid='%s' ip=%s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
            if (apMode_) {
                apShutdownPending_ = true;
                apShutdownAt_ = millis() + WIFI_AP_HANDOFF_TIMEOUT_MS;
                DebugLog.printf("[wifi] station connected, keeping AP alive for up to %lu ms for browser IP handoff\n",
                              static_cast<unsigned long>(WIFI_AP_HANDOFF_TIMEOUT_MS));
            }
        }

        if (apShutdownPending_ && static_cast<long>(millis() - apShutdownAt_) >= 0) {
            DebugLog.println("[wifi] AP grace period elapsed, stopping AP");
            stopAccessPoint();
        }

        hadConnection_ = true;
        stationAttemptActive_ = false;
        consecutiveFailureCount_ = 0;
        recoveryRebootRecommended_ = false;
        lastDisconnectReasonValid_ = false;
        clearFrontendError();
        updateAppState(true);
        return;
    }

    if (hadConnection_) {
        hadConnection_ = false;
        apShutdownPending_ = false;
        apShutdownAt_ = 0;
        if (settings_.wifi.apFallbackEnabled) {
            startAccessPoint();
        }
    }

    const unsigned long now = millis();
    if (userScanActive_) {
        const int scanState = WiFi.scanComplete();
        const bool scanTimedOut = lastScanStartedAt_ != 0 && now - lastScanStartedAt_ > WIFI_USER_SCAN_TIMEOUT_MS;
        if (scanState == WIFI_SCAN_RUNNING && !scanTimedOut) {
            updateAppState(true);
            return;
        }
        if (scanState >= 0) {
            // Keep the result buffer intact until the web request consumes it.
            updateAppState(true);
            return;
        }
        if (scanState == WIFI_SCAN_FAILED && !scanTimedOut &&
            lastScanStartedAt_ != 0 && now - lastScanStartedAt_ < 3000UL) {
            // Give the browser a chance to receive the failed state before a
            // station retry starts another driver scan.
            updateAppState(true);
            return;
        }
        if (scanTimedOut) {
            WiFi.scanDelete();
        }
        finishUserScan();
    }

    if (stationAttemptActive_ && now - connectAttemptStartedAt_ > WIFI_CONNECT_TIMEOUT_MS) {
        stationAttemptActive_ = false;
        // The retry interval begins after a failed attempt, not when it began.
        // This gives AP clients a stable quiet window between STA handshakes.
        lastConnectAttemptAt_ = now;
        registerFailedAttempt("timeout");
        startAccessPoint();
    }

    if (!stationAttemptActive_ && hasStaCredentials() && now - lastConnectAttemptAt_ > WIFI_RETRY_INTERVAL_MS) {
        startStation();
    }

    updateAppState(true);
}

bool WiFiManager::shouldRebootForRecovery() const {
    return recoveryRebootRecommended_;
}

bool WiFiManager::prepareStationHandoff(IPAddress& stationIp, uint32_t& shutdownDelayMs) {
    stationIp = IPAddress();
    shutdownDelayMs = 0;
    if (!isConnected()) {
        return false;
    }

    stationIp = WiFi.localIP();
    if (stationIp == IPAddress() || stationIp == WiFi.softAPIP()) {
        return false;
    }

    if (apMode_) {
        shutdownDelayMs = WIFI_AP_HANDOFF_SHUTDOWN_DELAY_MS;
        apShutdownPending_ = true;
        apShutdownAt_ = millis() + shutdownDelayMs;
        DebugLog.printf("[wifi] browser acknowledged station IP %s; stopping AP in %lu ms\n",
                      stationIp.toString().c_str(), static_cast<unsigned long>(shutdownDelayMs));
    }
    return true;
}

uint8_t WiFiManager::consecutiveFailureCount() const {
    return consecutiveFailureCount_;
}

bool WiFiManager::hasStaCredentials() const {
    return !settings_.wifi.ssid.isEmpty();
}

void WiFiManager::finishUserScan() {
    userScanActive_ = false;
    if (resumeStationAfterScan_ && hasStaCredentials()) {
        // Make the normal retry path eligible on the next loop pass, after the
        // scan result has already been delivered to the browser.
        lastConnectAttemptAt_ = millis() - WIFI_RETRY_INTERVAL_MS - 1UL;
    }
    resumeStationAfterScan_ = false;
}

void WiFiManager::registerFailedAttempt(const char* reason) {
    if (!hasStaCredentials()) {
        consecutiveFailureCount_ = 0;
        recoveryRebootRecommended_ = false;
        lastDisconnectReasonValid_ = false;
        return;
    }

    if (consecutiveFailureCount_ < WIFI_MAX_CONSECUTIVE_FAILURES) {
        ++consecutiveFailureCount_;
    }

    const char* disconnectReasonName = lastDisconnectReasonValid_ ? WiFi.disconnectReasonName(lastDisconnectReason_) : "unknown";
    DebugLog.printf("[wifi] connect failed trigger=%s wifi_reason=%s(%d) count=%u/%u\n", reason,
                  disconnectReasonName,
                  static_cast<int>(lastDisconnectReason_),
                  static_cast<unsigned>(consecutiveFailureCount_),
                  static_cast<unsigned>(WIFI_MAX_CONSECUTIVE_FAILURES));

    if (lastDisconnectReasonValid_ && isCredentialFailureReason(lastDisconnectReason_)) {
        skipBssidSelectionForNextConnect_ = true;
        setFrontendError("Wi-Fi credentials were rejected for saved network '" + settings_.wifi.ssid + "'.");
        recoveryRebootRecommended_ = false;
        return;
    }

    if (lastDisconnectReasonValid_ && isNetworkMissingReason(lastDisconnectReason_)) {
        setFrontendError("Saved Wi-Fi network '" + settings_.wifi.ssid + "' is not available.");
        recoveryRebootRecommended_ = false;
        return;
    }

    if (consecutiveFailureCount_ >= WIFI_MAX_CONSECUTIVE_FAILURES) {
        if (isConfiguredNetworkVisible()) {
            setFrontendError("Wi-Fi reconnect is failing for saved network '" + settings_.wifi.ssid + "'. Keeping AP/retry mode active instead of rebooting.");
            recoveryRebootRecommended_ = false;
            DebugLog.println("[wifi] max consecutive failures reached with saved network visible, continuing retries without recovery reboot");
        } else {
            setFrontendError("Saved Wi-Fi network '" + settings_.wifi.ssid + "' is not visible. Reboot skipped.");
            recoveryRebootRecommended_ = false;
            DebugLog.println("[wifi] max consecutive failures reached but saved network is not visible, reboot skipped");
        }
    }
}

bool WiFiManager::isConfiguredNetworkVisible() {
    if (!hasStaCredentials()) {
        return false;
    }

    const int existingScanState = WiFi.scanComplete();
    if (existingScanState == WIFI_SCAN_RUNNING) {
        WiFi.scanDelete();
    }

    WiFi.enableSTA(true);
    const int networkCount = WiFi.scanNetworks(false, true);
    if (networkCount <= 0) {
        WiFi.scanDelete();
        return false;
    }

    bool visible = false;
    for (int index = 0; index < networkCount; ++index) {
        if (WiFi.SSID(index) == settings_.wifi.ssid) {
            visible = true;
            break;
        }
    }

    WiFi.scanDelete();
    return visible;
}

WiFiManager::PreferredAccessPoint WiFiManager::findPreferredAccessPoint() {
    PreferredAccessPoint preferred;
    if (!hasStaCredentials()) {
        return preferred;
    }

    const int existingScanState = WiFi.scanComplete();
    if (existingScanState == WIFI_SCAN_RUNNING) {
        WiFi.scanDelete();
    }

    WiFi.enableSTA(true);
    const int networkCount = WiFi.scanNetworks(false, true);
    if (networkCount <= 0) {
        WiFi.scanDelete();
        return preferred;
    }

    for (int index = 0; index < networkCount; ++index) {
        if (WiFi.SSID(index) != settings_.wifi.ssid) {
            continue;
        }

        const int32_t candidateRssi = WiFi.RSSI(index);
        if (!preferred.found || candidateRssi > preferred.rssi) {
            preferred.found = true;
            preferred.rssi = candidateRssi;
            preferred.channel = WiFi.channel(index);
            const uint8_t* candidateBssid = WiFi.BSSID(index);
            if (candidateBssid != nullptr) {
                memcpy(preferred.bssid, candidateBssid, sizeof(preferred.bssid));
            }
        }
    }

    WiFi.scanDelete();
    return preferred;
}

bool WiFiManager::isCredentialFailureReason(wifi_err_reason_t reason) const {
    switch (reason) {
        case WIFI_REASON_AUTH_EXPIRE:
        case WIFI_REASON_AUTH_LEAVE:
        case WIFI_REASON_NOT_AUTHED:
        case WIFI_REASON_ASSOC_NOT_AUTHED:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_802_1X_AUTH_FAILED:
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
            return true;
        default:
            return false;
    }
}

bool WiFiManager::isNetworkMissingReason(wifi_err_reason_t reason) const {
    return reason == WIFI_REASON_NO_AP_FOUND;
}

void WiFiManager::clearFrontendError() {
    if (appState_ != nullptr) {
        appState_->setLastError("");
    }
}

void WiFiManager::setFrontendError(const String& message) {
    if (appState_ != nullptr) {
        appState_->setLastError(message);
    }
}

void WiFiManager::handleDisconnectEvent(arduino_event_info_t info) {
    lastDisconnectReason_ = static_cast<wifi_err_reason_t>(info.wifi_sta_disconnected.reason);
    lastDisconnectReasonValid_ = true;
    DebugLog.printf("[wifi] disconnect event reason=%s(%d)\n", WiFi.disconnectReasonName(lastDisconnectReason_),
                  static_cast<int>(lastDisconnectReason_));
}

void WiFiManager::updateAppState(bool rateLimited) {
    if (appState_ == nullptr) {
        return;
    }
    const unsigned long now = millis();
    if (rateLimited && lastStatePublishAt_ != 0 && now - lastStatePublishAt_ < WIFI_STATE_PUBLISH_INTERVAL_MS) {
        return;
    }
    lastStatePublishAt_ = now;
    const bool connected = isConnected();
    appState_->setWiFiStatus(
        connected,
        apMode_,
        connected ? WiFi.SSID() : settings_.wifi.ssid,
        connected ? WiFi.localIP() : (apMode_ ? WiFi.softAPIP() : IPAddress()),
        connected ? WiFi.RSSI() : 0,
        apSsid_);
}

bool WiFiManager::isConnected() const {
    return WiFi.status() == WL_CONNECTED;
}

bool WiFiManager::isApMode() const {
    return apMode_;
}

IPAddress WiFiManager::localIp() const {
    return WiFi.localIP();
}

IPAddress WiFiManager::apIp() const {
    return WiFi.softAPIP();
}

String WiFiManager::currentSsid() const {
    return isConnected() ? WiFi.SSID() : settings_.wifi.ssid;
}

String WiFiManager::apSsid() const {
    return apSsid_;
}

int32_t WiFiManager::rssi() const {
    return isConnected() ? WiFi.RSSI() : 0;
}

WiFiManager::ScanSnapshot WiFiManager::getScanSnapshot() {
    const int scanState = WiFi.scanComplete();
    ScanSnapshot snapshot;
    snapshot.active = scanState == WIFI_SCAN_RUNNING;
    snapshot.complete = scanState >= 0 || (lastScanCompleted_ && lastScanStartedAt_ != 0);
    snapshot.failed = lastScanStartedAt_ != 0 && !lastScanCompleted_ && scanState == WIFI_SCAN_FAILED;
    snapshot.ageMs = lastScanStartedAt_ == 0 ? 0 : millis() - lastScanStartedAt_;
    if (snapshot.failed && userScanActive_) {
        finishUserScan();
    }
    return snapshot;
}

bool WiFiManager::startScan() {
    const int scanState = WiFi.scanComplete();
    if (scanState == WIFI_SCAN_RUNNING) {
        return true;
    }
    if (scanState >= 0 || scanState == WIFI_SCAN_FAILED) {
        WiFi.scanDelete();
    }

    resumeStationAfterScan_ = stationAttemptActive_ && !isConnected();
    if (resumeStationAfterScan_) {
        // Async scans are rejected while the STA driver is in an association
        // handshake. Pause that retry without erasing credentials; AP service
        // remains active throughout the user-requested scan.
        WiFi.disconnect(false, false);
        stationAttemptActive_ = false;
        lastConnectAttemptAt_ = millis();
        delay(100);
    }

    lastScanCompleted_ = false;
    WiFi.enableSTA(true);
    const int scanResult = WiFi.scanNetworks(true, true);
    if (scanResult == WIFI_SCAN_FAILED) {
        WiFi.scanDelete();
        delay(100);
        const int retryResult = WiFi.scanNetworks(true, true);
        if (retryResult == WIFI_SCAN_FAILED) {
            lastScanStartedAt_ = 0;
            finishUserScan();
            return false;
        }
    }
    userScanActive_ = true;
    lastScanStartedAt_ = millis();
    return true;
}

void WiFiManager::appendScanResultsJson(JsonArray networks) {
    const int networkCount = WiFi.scanComplete();
    if (networkCount <= 0) {
        lastScanCompleted_ = networkCount == 0;
        WiFi.scanDelete();
        finishUserScan();
        return;
    }

    lastScanCompleted_ = true;

    for (int index = 0; index < networkCount; ++index) {
        const String ssid = WiFi.SSID(index);
        if (ssid.isEmpty()) {
            continue;
        }

        JsonObject network = networks.add<JsonObject>();
        network["ssid"] = ssid;
        network["rssi"] = WiFi.RSSI(index);
        network["encrypted"] = WiFi.encryptionType(index) != WIFI_AUTH_OPEN;
    }

    WiFi.scanDelete();
    finishUserScan();
}

bool WiFiManager::shouldRedirectCaptivePortal(const String& hostHeader) const {
    if (!apMode_) {
        return false;
    }
    if (hostHeader.length() == 0) {
        return false;
    }
    if (hostHeader == WiFi.softAPIP().toString()) {
        return false;
    }
    if (hostHeader == WiFi.localIP().toString()) {
        return false;
    }
    if (hostHeader.indexOf('.') < 0) {
        return false;
    }
    return true;
}
