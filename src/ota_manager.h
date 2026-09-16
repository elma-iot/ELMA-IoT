#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "app_state.h"
#include "settings_schema.h"

class OtaManager {
  public:
    using RestartHandler = void (*)(const String& reason);

    void begin(const SettingsBundle& settings, AppState& appState);
    void applySettings(const SettingsBundle& settings);
    void setProgressCallback(void (*callback)());
    void setRestartHandler(RestartHandler handler);
    void setRollbackState(bool pendingVerify, const String& pendingVersion, const String& rolledBackVersion, const String& rollbackReason);
    String pendingInstallVersion() const;
    bool isBusy() const;
    bool isFirmwareTransferActive() const;
    void loop();
    bool triggerCheck(bool applyAfterCheck);
    bool triggerReleaseRefresh(String& error);
    bool beginLocalUpload(const String& filename, size_t totalSize, String& error, const String& sessionId = String());
    bool writeLocalUploadChunk(const uint8_t* data, size_t len, String& error);
    bool writeLocalUploadChunkAt(const String& sessionId, size_t offset, const uint8_t* data, size_t len, String& error);
    bool finishLocalUpload(String& error, const String& sessionId = String());
    bool cancelLocalUpload(const String& sessionId, String& error);
    void abortLocalUpload(const String& error);
    void appendLocalUploadStatus(JsonObject root) const;
    void appendStatusJson(JsonObject root) const;
    void appendFirmwareInfoJson(JsonObject root, bool refresh, String& error);
    void reportError(const String& error);
    bool selectReleaseOption(const String& optionLabel, String& error);
    bool triggerInstallSelected(String& error);
    bool triggerInstallVersion(const String& version, String& error);
    bool triggerInstallVersion(const String& version, const String& assetName, String& error);

  private:
    struct CheckResult {
        bool updateAvailable = false;
        bool success = false;
        String latestVersion;
        String assetUrl;
        String assetName;
        String checksumSha256;
        String message;
    };

      struct ReleaseInfo {
        String tag;
        String name;
        String publishedAt;
        String assetUrl;
        String assetName;
        String variantLabel;
        String chipFamily;
        bool prerelease = false;
        bool isInstalled = false;
        bool isLatest = false;
        bool isNew = false;
    };

    SettingsBundle settings_;
    AppState* appState_ = nullptr;
    volatile bool pendingCheck_ = false;
    volatile bool pendingApply_ = false;
    volatile bool pendingReleaseRefresh_ = false;
    String pendingInstallVersion_;
    String pendingInstallAssetName_;
    String pendingInstallAssetUrl_;
    uint8_t releaseRefreshAttemptsRemaining_ = 0;
    uint8_t releaseRefreshAttemptsStarted_ = 0;
    unsigned long releaseRefreshNextAttemptAtMs_ = 0;
    bool busy_ = false;
    bool releaseRefreshInProgress_ = false;
    TaskHandle_t releaseRefreshTaskHandle_ = nullptr;
    TaskHandle_t checkTaskHandle_ = nullptr;
    TaskHandle_t installTaskHandle_ = nullptr;
    bool checkTaskApplyAfterCheck_ = false;
    String releaseRefreshError_;
    String lastMessage_ = "idle";
    String latestVersion_;
    bool updateAvailable_ = false;
    bool rollbackPendingVerify_ = false;
    String rollbackPendingVersion_;
    String rolledBackVersion_;
    String rollbackReason_;
    String selectedVersion_;
    String selectedAssetName_;
    String updatePhase_;
    size_t progressBytes_ = 0;
    size_t progressTotalBytes_ = 0;
    uint8_t progressPercent_ = 0;
    bool recoveryRebootRequested_ = false;
    bool localUploadStarted_ = false;
    bool localUploadHadData_ = false;
    bool localUploadOk_ = false;
    bool localUploadHeaderValidated_ = false;
    size_t localUploadHeaderBytes_ = 0;
    String localUploadSessionId_;
    String localUploadFilename_;
    bool rebootPending_ = false;
    unsigned long rebootAtMs_ = 0;
    std::vector<ReleaseInfo> releaseCache_;
    unsigned long releasesFetchedAtMs_ = 0;
    void (*progressCallback_)() = nullptr;
    RestartHandler restartHandler_ = nullptr;
    uint8_t localUploadHeader_[24] = {0};

    void runTask(bool applyAfterCheck);
    void runReleaseRefreshTask();
    static void releaseRefreshTaskEntry(void* context);
    static void checkTaskEntry(void* context);
    static void installTaskEntry(void* context);
    void runVersionTask(const String& version, const String& assetName, const String& assetUrl);
    CheckResult checkNow();
    bool fetchAvailableReleases(bool refresh, String& error);
    bool resolveVersionResult(const String& version, const String& assetName, CheckResult& result, String& error);
    bool installNow(const CheckResult& result, String& message);
    int compareVersions(const String& left, const String& right) const;
    String normalizeVersion(const String& value) const;
    bool hasBinExtension(const String& filename) const;
    void resetProgress();
    void scheduleReboot(unsigned long delayMs);
    void syncAppState(const String& lastResult, const String& lastError = "");
    void pumpProgressCallback();
    String releaseOptionLabel(const ReleaseInfo& release) const;
    const ReleaseInfo* findReleaseForOption(const String& optionLabel) const;
    void ensureSelectedReleaseStillValid();
};
