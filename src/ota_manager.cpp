#include "device_log.h"
#include "ota_manager.h"

#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <esp_app_format.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>

#include "github_release_client.h"
#include "version.h"

namespace {
constexpr unsigned long RELEASE_CACHE_TTL_MS = 5UL * 60UL * 1000UL;
constexpr uint8_t RELEASE_REFRESH_MAX_ATTEMPTS = 1;
constexpr unsigned long RELEASE_REFRESH_RETRY_DELAY_MS = 1500UL;
constexpr unsigned long OTA_DOWNLOAD_STALL_TIMEOUT_MS = 15000UL;
constexpr uint8_t OTA_DOWNLOAD_MAX_RESUME_ATTEMPTS = 3;
// TLS handshakes and filtered GitHub JSON parsing exceed the Arduino loop
// task's stack on ESP32-S3. Every network-backed OTA operation uses its own
// generously sized task instead of ever running on loopTask.
constexpr uint32_t OTA_NETWORK_TASK_STACK_BYTES = 24576;

void logOtaTaskStack(const char* taskName) {
    const UBaseType_t minimumFreeWords = uxTaskGetStackHighWaterMark(nullptr);
    DebugLog.printf("[ota] %s minimum free stack=%u bytes\n",
                  taskName,
                  static_cast<unsigned>(minimumFreeWords * sizeof(StackType_t)));
}

String githubReleaseAssetUrl(const SettingsBundle& settings, const String& version, const String& assetName) {
    return String("https://github.com/") + settings.ota.owner + "/" + settings.ota.repository + "/releases/download/" + version + "/" + assetName;
}

String applyVersionTemplate(String templ, const String& version) {
    templ.replace("${version}", version);
    return templ;
}

String normalizedVersionTag(String value) {
    if (value.isEmpty()) {
        return "";
    }
    if (value.startsWith("v") || value.startsWith("V")) {
        return value;
    }
    return "v" + value;
}

String defaultAssetTemplateForBuild() {
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
#else
    #ifdef APP_ENABLE_HACS_MQTT
        #ifdef APP_DISABLE_WEB_UI
        return "esp32-notifier-hacs-slim-${version}.bin";
        #else
        return "esp32-notifier-hacs-${version}.bin";
        #endif
    #else
        return "esp32-notifier-${version}.bin";
    #endif
#endif
}

String effectiveAssetTemplate(const SettingsBundle& settings) {
        String templ = settings.ota.assetTemplate;
        templ.trim();
        if (templ.isEmpty()) {
                return defaultAssetTemplateForBuild();
        }

#if defined(CONFIG_IDF_TARGET_ESP32S3)
    if (templ == "esp32-notifier-${version}.bin" || templ == "esp32-notifier-hacs-${version}.bin" || templ == "esp32-notifier-hacs-slim-${version}.bin") {
                return defaultAssetTemplateForBuild();
        }
#endif

        return templ;
}

String currentReleaseAssetName(const SettingsBundle& settings) {
    const String version = normalizedVersionTag(APP_VERSION);
    return applyVersionTemplate(effectiveAssetTemplate(settings), version);
}

String releaseAssetNameForVersion(const SettingsBundle& settings, const String& version) {
    return applyVersionTemplate(effectiveAssetTemplate(settings), normalizedVersionTag(version));
}

String variantLabelForAssetName(const String& assetName) {
    String lowered = assetName;
    lowered.toLowerCase();
    if (lowered.indexOf("hacs-slim") >= 0) {
        return "HACS Slim";
    }
    if (lowered.indexOf("hacs") >= 0) {
        return "HACS";
    }
    return "Standard";
}

String normalizeChipFamilyToken(String value) {
    value.trim();
    value.toLowerCase();
    value.replace("-", "");
    value.replace("_", "");
    value.replace(" ", "");
    return value;
}

String chipFamilyDisplayName(const String& chipFamily) {
    const String normalized = normalizeChipFamilyToken(chipFamily);
    if (normalized == "esp32c3") {
        return "ESP32-C3";
    }
    if (normalized == "esp32s3") {
        return "ESP32-S3";
    }
    if (normalized == "esp32") {
        return "ESP32";
    }
    return normalized.isEmpty() ? String("unknown target") : chipFamily;
}

String chipFamilyFromChipId(esp_chip_id_t chipId) {
    switch (chipId) {
        case ESP_CHIP_ID_ESP32:
            return "esp32";
        case ESP_CHIP_ID_ESP32C3:
            return "esp32c3";
        case ESP_CHIP_ID_ESP32S3:
            return "esp32s3";
        default:
            return "";
    }
}

String chipFamilyForAssetName(const String& assetName) {
    const String lowered = normalizeChipFamilyToken(assetName);
    if (lowered.indexOf("esp32c3") >= 0) {
        return "esp32c3";
    }
    if (lowered.indexOf("esp32s3") >= 0) {
        return "esp32s3";
    }
    if (lowered.indexOf("esp32") >= 0) {
        return "esp32";
    }
    return "";
}

String currentChipFamily() {
#if defined(CONFIG_IDF_TARGET_ESP32C3)
    return "esp32c3";
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
    return "esp32s3";
#elif defined(CONFIG_IDF_TARGET_ESP32)
    return "esp32";
#else
    String model = normalizeChipFamilyToken(ESP.getChipModel());
    if (model.indexOf("esp32c3") >= 0) {
        return "esp32c3";
    }
    if (model.indexOf("esp32s3") >= 0) {
        return "esp32s3";
    }
    if (model.indexOf("esp32") >= 0) {
        return "esp32";
    }
    return model;
#endif
}

bool isCompatibleChipFamily(const String& targetChipFamily) {
    const String normalized = normalizeChipFamilyToken(targetChipFamily);
    return normalized.isEmpty() || normalized == currentChipFamily();
}

String incompatibleChipMessage(const String& targetChipFamily) {
    return String("Firmware targets ") + chipFamilyDisplayName(targetChipFamily) + ", but this device is " + chipFamilyDisplayName(currentChipFamily()) + ".";
}

String chipFamilyFromManifest(JsonVariantConst release, const String& assetName, const String& assetUrl) {
    if (release["chip"].is<const char*>()) {
        const String chip = normalizeChipFamilyToken(String(static_cast<const char*>(release["chip"])));
        if (!chip.isEmpty()) {
            return chip;
        }
    }
    if (release["target"].is<const char*>()) {
        const String chip = normalizeChipFamilyToken(String(static_cast<const char*>(release["target"])));
        if (!chip.isEmpty()) {
            return chip;
        }
    }
    if (release["chipId"].is<uint16_t>()) {
        const String chip = chipFamilyFromChipId(static_cast<esp_chip_id_t>(release["chipId"].as<uint16_t>()));
        if (!chip.isEmpty()) {
            return chip;
        }
    }
    if (release["chip_id"].is<uint16_t>()) {
        const String chip = chipFamilyFromChipId(static_cast<esp_chip_id_t>(release["chip_id"].as<uint16_t>()));
        if (!chip.isEmpty()) {
            return chip;
        }
    }

    String chip = chipFamilyForAssetName(assetName);
    if (!chip.isEmpty()) {
        return chip;
    }
    return chipFamilyForAssetName(assetUrl);
}

bool validateImageHeader(const uint8_t* data, size_t len, String& chipFamily, String& error) {
    if (len < sizeof(esp_image_header_t)) {
        error = "Firmware image header is incomplete.";
        return false;
    }

    esp_image_header_t header;
    memcpy(&header, data, sizeof(header));
    if (header.magic != ESP_IMAGE_HEADER_MAGIC) {
        error = "Uploaded file is not a valid ESP32 firmware image.";
        return false;
    }

    chipFamily = chipFamilyFromChipId(header.chip_id);
    if (chipFamily.isEmpty()) {
        error = "Firmware image does not declare a supported ESP32 target.";
        return false;
    }
    if (!isCompatibleChipFamily(chipFamily)) {
        error = incompatibleChipMessage(chipFamily);
        return false;
    }
    return true;
}

void configureTlsClient(WiFiClientSecure& client, bool allowInsecureTls) {
    if (allowInsecureTls) {
        client.setInsecure();
    }
}

bool hasWritableOtaTargetPartition(String& error) {
    const esp_partition_t* runningPartition = esp_ota_get_running_partition();
    const esp_partition_t* targetPartition = esp_ota_get_next_update_partition(nullptr);
    if (runningPartition == nullptr || targetPartition == nullptr) {
        error = "No writable OTA target partition is available on this flash layout. Use USB flashing for firmware updates.";
        return false;
    }
    if (runningPartition->address == targetPartition->address && runningPartition->size == targetPartition->size) {
        error = "This firmware uses a single-app flash layout, so in-browser OTA updates are disabled. Use USB flashing for firmware updates.";
        return false;
    }
    return true;
}

String httpErrorWithDetail(HTTPClient& http, const char* prefix, int code) {
    String message = prefix;
    message += code;
    if (code < 0) {
        message += " (";
        message += http.errorToString(code);
        message += ")";
    }
    return message;
}

template <typename ConfigureHeaders>
int beginAndGet(HTTPClient& http, WiFiClientSecure& client, const String& url, bool allowInsecureTls, uint16_t timeoutMs, ConfigureHeaders configureHeaders) {
    auto beginRequest = [&](bool insecure) {
        http.end();
        client.stop();
        client = WiFiClientSecure();
        if (insecure) {
            client.setInsecure();
        } else {
            configureTlsClient(client, allowInsecureTls);
        }
        http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
        http.setTimeout(timeoutMs);
        if (!http.begin(client, url)) {
            return false;
        }
        configureHeaders(http);
        return true;
    };

    if (!beginRequest(allowInsecureTls)) {
        yield();
        if (!beginRequest(allowInsecureTls)) {
            if (allowInsecureTls || !beginRequest(true)) {
                return HTTPC_ERROR_CONNECTION_REFUSED;
            }
        }
    }

    int code = http.GET();
    if (code < 0 && !allowInsecureTls) {
        if (!beginRequest(true)) {
            return code;
        }
        code = http.GET();
    }
    return code;
}

}  // namespace

bool OtaManager::hasBinExtension(const String& filename) const {
    String lowercase = filename;
    lowercase.toLowerCase();
    return lowercase.endsWith(".bin");
}

void OtaManager::resetProgress() {
    updatePhase_ = "";
    progressBytes_ = 0;
    progressTotalBytes_ = 0;
    progressPercent_ = 0;
}

void OtaManager::scheduleReboot(unsigned long delayMs) {
    rebootPending_ = true;
    rebootAtMs_ = millis() + delayMs;
}

void OtaManager::syncAppState(const String& lastResult, const String& lastError) {
    if (appState_ != nullptr) {
        appState_->setOta(busy_, updateAvailable_, latestVersion_, lastResult, lastError, updatePhase_, progressPercent_);
    }
}

void OtaManager::pumpProgressCallback() {
    if (progressCallback_ != nullptr) {
        progressCallback_();
    }
    yield();
}

void OtaManager::begin(const SettingsBundle& settings, AppState& appState) {
    appState_ = &appState;
    applySettings(settings);
}

void OtaManager::setRollbackState(bool pendingVerify, const String& pendingVersion, const String& rolledBackVersion, const String& rollbackReason) {
    rollbackPendingVerify_ = pendingVerify;
    rollbackPendingVersion_ = pendingVersion;
    rolledBackVersion_ = rolledBackVersion;
    rollbackReason_ = rollbackReason;

    if (!rollbackReason_.isEmpty()) {
        lastMessage_ = rollbackReason_;
        syncAppState("rolled_back", rollbackReason_);
    } else if (rollbackPendingVerify_) {
        syncAppState("pending_verify");
    }
}

void OtaManager::setProgressCallback(void (*callback)()) {
    progressCallback_ = callback;
}

void OtaManager::setRestartHandler(RestartHandler handler) {
    restartHandler_ = handler;
}

void OtaManager::applySettings(const SettingsBundle& settings) {
    settings_ = settings;
}

bool OtaManager::triggerReleaseRefresh(String& error) {
    if (busy_) {
        error = "Another update is already in progress.";
        return false;
    }
    if (pendingReleaseRefresh_ || releaseRefreshInProgress_) {
        error = "Firmware release refresh is already running.";
        return false;
    }

    pendingReleaseRefresh_ = true;
    releaseRefreshAttemptsRemaining_ = RELEASE_REFRESH_MAX_ATTEMPTS;
    releaseRefreshAttemptsStarted_ = 0;
    releaseRefreshNextAttemptAtMs_ = millis();
    releaseRefreshError_ = "";
    lastMessage_ = "queued release refresh";
    syncAppState("queued");
    return true;
}

void OtaManager::reportError(const String& error) {
    if (error.isEmpty()) {
        return;
    }
    lastMessage_ = error;
    syncAppState("error", error);
}

void OtaManager::loop() {
    if (rebootPending_ && static_cast<long>(millis() - rebootAtMs_) >= 0) {
        rebootPending_ = false;
        if (restartHandler_ != nullptr) {
            restartHandler_(String("ota"));
        } else {
            DebugLog.service(true);
            ESP.restart();
        }
    }
    // A selected install must never overlap a GitHub release refresh: both
    // operations allocate TLS/HTTP state and stream data. If a refresh is
    // already running, wait for it to finish. Once it does, the explicit
    // install takes priority over any queued refresh retry.
    if (!pendingInstallVersion_.isEmpty() && !busy_ && !releaseRefreshInProgress_) {
        pendingReleaseRefresh_ = false;
        releaseRefreshAttemptsRemaining_ = 0;
        busy_ = true;
        const BaseType_t created = xTaskCreatePinnedToCore(
            installTaskEntry, "ota-install", OTA_NETWORK_TASK_STACK_BYTES, this, 1, &installTaskHandle_, 0);
        if (created != pdPASS) {
            busy_ = false;
            pendingInstallVersion_ = "";
            pendingInstallAssetName_ = "";
            pendingInstallAssetUrl_ = "";
            lastMessage_ = "Unable to start background firmware install.";
            syncAppState("error", lastMessage_);
        }
        return;
    }
    const bool playbackActive = (pendingReleaseRefresh_ || pendingCheck_) && appState_ != nullptr && [this]() {
        const auto playback = appState_->snapshot().playback;
        return playback.state == "playing" || playback.state == "buffering";
    }();
    if (pendingReleaseRefresh_ && !playbackActive && !busy_ && !releaseRefreshInProgress_ &&
        static_cast<long>(millis() - releaseRefreshNextAttemptAtMs_) >= 0) {
        releaseRefreshInProgress_ = true;
        const BaseType_t created = xTaskCreatePinnedToCore(
            releaseRefreshTaskEntry, "ota-release-check", OTA_NETWORK_TASK_STACK_BYTES, this, 1, &releaseRefreshTaskHandle_, 0);
        if (created != pdPASS) {
            releaseRefreshInProgress_ = false;
            pendingReleaseRefresh_ = false;
            releaseRefreshError_ = "Unable to start background release check.";
            lastMessage_ = releaseRefreshError_;
            syncAppState("release refresh failed", releaseRefreshError_);
        }
        return;
    }
    if (pendingCheck_ && (!playbackActive || pendingApply_) && !busy_ && !releaseRefreshInProgress_) {
        const bool applyAfterCheck = pendingApply_;
        pendingCheck_ = false;
        pendingApply_ = false;
        busy_ = true;
        checkTaskApplyAfterCheck_ = applyAfterCheck;
        const BaseType_t created = xTaskCreatePinnedToCore(
            checkTaskEntry, "ota-version-check", OTA_NETWORK_TASK_STACK_BYTES, this, 1, &checkTaskHandle_, 0);
        if (created != pdPASS) {
            busy_ = false;
            checkTaskApplyAfterCheck_ = false;
            lastMessage_ = "Unable to start background firmware check.";
            syncAppState("error", lastMessage_);
        }
    }
}

void OtaManager::releaseRefreshTaskEntry(void* context) {
    OtaManager* manager = static_cast<OtaManager*>(context);
    manager->runReleaseRefreshTask();
    logOtaTaskStack("release refresh");
    manager->releaseRefreshTaskHandle_ = nullptr;
    vTaskDelete(nullptr);
}

void OtaManager::checkTaskEntry(void* context) {
    OtaManager* manager = static_cast<OtaManager*>(context);
    const bool applyAfterCheck = manager->checkTaskApplyAfterCheck_;
    manager->checkTaskApplyAfterCheck_ = false;
    manager->runTask(applyAfterCheck);
    logOtaTaskStack(applyAfterCheck ? "check and install" : "version check");
    manager->checkTaskHandle_ = nullptr;
    vTaskDelete(nullptr);
}

void OtaManager::installTaskEntry(void* context) {
    OtaManager* manager = static_cast<OtaManager*>(context);
    const String version = manager->pendingInstallVersion_;
    const String assetName = manager->pendingInstallAssetName_;
    const String assetUrl = manager->pendingInstallAssetUrl_;
    manager->pendingInstallVersion_ = "";
    manager->pendingInstallAssetName_ = "";
    manager->pendingInstallAssetUrl_ = "";
    manager->runVersionTask(version, assetName, assetUrl);
    logOtaTaskStack("selected install");
    manager->installTaskHandle_ = nullptr;
    vTaskDelete(nullptr);
}

bool OtaManager::triggerCheck(bool applyAfterCheck) {
    if (busy_) {
        return false;
    }
    pendingCheck_ = true;
    pendingApply_ = applyAfterCheck;
    pendingInstallAssetUrl_ = "";
    selectedVersion_ = "";
    selectedAssetName_ = "";
    resetProgress();
    lastMessage_ = applyAfterCheck ? "queued update" : "queued check";
    syncAppState("queued");
    return true;
}

bool OtaManager::beginLocalUpload(const String& filename, size_t totalSize, String& error, const String& sessionId) {
    if (busy_) {
        if (localUploadStarted_ && !sessionId.isEmpty() && sessionId == localUploadSessionId_ &&
            filename == localUploadFilename_ && totalSize == progressTotalBytes_) {
            return true;
        }
        error = "Another update is already in progress.";
        return false;
    }
    if (!hasWritableOtaTargetPartition(error)) {
        return false;
    }
    if (!hasBinExtension(filename)) {
        error = "Select a .bin firmware image.";
        return false;
    }

    selectedVersion_ = filename.isEmpty() ? String("local upload") : String("local upload (") + filename + ")";
    lastMessage_ = "Flashing local firmware...";
    updatePhase_ = "Flashing";
    progressBytes_ = 0;
    progressTotalBytes_ = totalSize;
    progressPercent_ = 0;
    localUploadStarted_ = true;
    localUploadHadData_ = false;
    localUploadOk_ = false;
    localUploadHeaderValidated_ = false;
    localUploadHeaderBytes_ = 0;
    localUploadSessionId_ = sessionId;
    localUploadFilename_ = filename;
    memset(localUploadHeader_, 0, sizeof(localUploadHeader_));
    busy_ = true;

    const size_t updateSize = totalSize > 0 ? totalSize : UPDATE_SIZE_UNKNOWN;
    if (!Update.begin(updateSize, U_FLASH)) {
        error = String("Local firmware update failed: ") + Update.errorString();
        abortLocalUpload(error);
        return false;
    }
    syncAppState("updating");
    pumpProgressCallback();
    return true;
}

bool OtaManager::writeLocalUploadChunk(const uint8_t* data, size_t len, String& error) {
    if (!localUploadStarted_ || !busy_) {
        error = "Local upload is not active.";
        return false;
    }
    if (len == 0) {
        return true;
    }

    if (!localUploadHadData_) {
        localUploadHadData_ = true;
        if (data[0] != 0xE9) {
            error = "Uploaded file is not a valid ESP32 firmware image.";
            abortLocalUpload(error);
            return false;
        }
    }

    size_t offset = 0;
    if (!localUploadHeaderValidated_) {
        const size_t needed = sizeof(localUploadHeader_) - localUploadHeaderBytes_;
        const size_t toCopy = min(needed, len);
        memcpy(localUploadHeader_ + localUploadHeaderBytes_, data, toCopy);
        localUploadHeaderBytes_ += toCopy;
        offset += toCopy;

        if (localUploadHeaderBytes_ == sizeof(localUploadHeader_)) {
            String chipFamily;
            if (!validateImageHeader(localUploadHeader_, localUploadHeaderBytes_, chipFamily, error)) {
                abortLocalUpload(error);
                return false;
            }
            if (Update.write(localUploadHeader_, localUploadHeaderBytes_) != localUploadHeaderBytes_) {
                error = String("Local firmware update failed: ") + Update.errorString();
                abortLocalUpload(error);
                return false;
            }
            localUploadHeaderValidated_ = true;
        }
    }

    if (localUploadHeaderValidated_ && offset < len) {
        const size_t remaining = len - offset;
        if (Update.write(const_cast<uint8_t*>(data + offset), remaining) != remaining) {
            error = String("Local firmware update failed: ") + Update.errorString();
            abortLocalUpload(error);
            return false;
        }
    }

    progressBytes_ += len;
    if (progressTotalBytes_ > 0) {
        progressPercent_ = static_cast<uint8_t>(min<size_t>(100U, (progressBytes_ * 100U) / progressTotalBytes_));
        lastMessage_ = String("Flashing local firmware... ") + progressPercent_ + "%";
    }
    syncAppState("updating");
    pumpProgressCallback();
    return true;
}

bool OtaManager::writeLocalUploadChunkAt(
    const String& sessionId,
    size_t offset,
    const uint8_t* data,
    size_t len,
    String& error) {
    if (!localUploadStarted_ || sessionId.isEmpty() || sessionId != localUploadSessionId_) {
        error = "Local upload session is not active.";
        return false;
    }
    if (offset > progressBytes_) {
        error = String("Upload offset mismatch; resume at byte ") + progressBytes_ + ".";
        return false;
    }
    if (offset + len <= progressBytes_) {
        // The browser did not receive the previous acknowledgement and retried
        // an already committed chunk. Treat this as idempotent success.
        return true;
    }

    const size_t acceptedPrefix = progressBytes_ - offset;
    return writeLocalUploadChunk(data + acceptedPrefix, len - acceptedPrefix, error);
}

bool OtaManager::finishLocalUpload(String& error, const String& sessionId) {
    if (!localUploadStarted_) {
        error = "Firmware upload did not start.";
        return false;
    }
    if (!sessionId.isEmpty() && sessionId != localUploadSessionId_) {
        error = "Local upload session does not match.";
        return false;
    }
    if (!localUploadSessionId_.isEmpty() && progressTotalBytes_ > 0 && progressBytes_ != progressTotalBytes_) {
        error = String("Firmware upload is incomplete; resume at byte ") + progressBytes_ + ".";
        return false;
    }
    if (!localUploadHadData_) {
        error = "Uploaded firmware did not contain data.";
        abortLocalUpload(error);
        return false;
    }
    if (!localUploadHeaderValidated_) {
        error = "Uploaded firmware header is incomplete.";
        abortLocalUpload(error);
        return false;
    }
    if (!Update.end(true)) {
        error = String("Local firmware update failed: ") + Update.errorString();
        abortLocalUpload(error);
        return false;
    }
    if (!Update.isFinished()) {
        error = "Local firmware update failed: incomplete write.";
        abortLocalUpload(error);
        return false;
    }

    localUploadOk_ = true;
    localUploadStarted_ = false;
    localUploadHeaderValidated_ = false;
    localUploadHeaderBytes_ = 0;
    localUploadSessionId_ = "";
    localUploadFilename_ = "";
    busy_ = false;
    progressBytes_ = progressTotalBytes_;
    progressPercent_ = 100;
    updatePhase_ = "";
    lastMessage_ = "Local firmware uploaded. Restarting...";
    syncAppState("installed");
    pumpProgressCallback();
    scheduleReboot(1500);
    return true;
}

bool OtaManager::cancelLocalUpload(const String& sessionId, String& error) {
    if (!localUploadStarted_) {
        error = "No local firmware upload is active.";
        return false;
    }
    if (!sessionId.isEmpty() && sessionId != localUploadSessionId_) {
        error = "Local upload session does not match.";
        return false;
    }
    abortLocalUpload("Local firmware upload cancelled by user.");
    return true;
}

void OtaManager::abortLocalUpload(const String& error) {
    Update.abort();
    localUploadStarted_ = false;
    localUploadHadData_ = false;
    localUploadOk_ = false;
    localUploadHeaderValidated_ = false;
    localUploadHeaderBytes_ = 0;
    localUploadSessionId_ = "";
    localUploadFilename_ = "";
    busy_ = false;
    resetProgress();
    selectedVersion_ = "local";
    lastMessage_ = error;
    syncAppState("error", error);
    pumpProgressCallback();
}

void OtaManager::appendLocalUploadStatus(JsonObject root) const {
    root["active"] = localUploadStarted_;
    root["sessionId"] = localUploadSessionId_;
    root["filename"] = localUploadFilename_;
    root["offset"] = progressBytes_;
    root["total"] = progressTotalBytes_;
    root["progress"] = progressPercent_;
    root["message"] = lastMessage_;
}

void OtaManager::appendStatusJson(JsonObject root) const {
    root["busy"] = busy_;
    root["updateAvailable"] = updateAvailable_;
    root["latestVersion"] = latestVersion_;
    root["message"] = lastMessage_;
    root["selectedVersion"] = selectedVersion_;
    root["updatePhase"] = updatePhase_;
    root["updateProgress"] = progressPercent_;
    root["updateBytes"] = progressBytes_;
    root["updateTotalBytes"] = progressTotalBytes_;
}

String OtaManager::releaseOptionLabel(const ReleaseInfo& release) const {
    String label = release.tag;
    if (!release.variantLabel.isEmpty()) {
        label += " - ";
        label += release.variantLabel;
    }
    if (release.prerelease) {
        label += " (pre)";
    }
    return label;
}

const OtaManager::ReleaseInfo* OtaManager::findReleaseForOption(const String& optionLabel) const {
    for (const ReleaseInfo& release : releaseCache_) {
        if (releaseOptionLabel(release) == optionLabel) {
            return &release;
        }
    }
    return nullptr;
}

void OtaManager::ensureSelectedReleaseStillValid() {
    for (const ReleaseInfo& release : releaseCache_) {
        if (release.tag == selectedVersion_ && release.assetName == selectedAssetName_ &&
            release.tag == latestVersion_) {
            return;
        }
    }

    selectedVersion_ = "";
    selectedAssetName_ = "";
    // Prefer the same build variant as the installed firmware for the newest
    // release. GitHub returns assets alphabetically, which otherwise tends to
    // select HACS Slim before the full HACS build.
    const String preferredLatestAsset = releaseAssetNameForVersion(settings_, latestVersion_);
    for (const ReleaseInfo& release : releaseCache_) {
        if (release.tag == latestVersion_ && release.assetName == preferredLatestAsset) {
            selectedVersion_ = release.tag;
            selectedAssetName_ = release.assetName;
            return;
        }
    }
    for (const ReleaseInfo& release : releaseCache_) {
        if (release.tag == latestVersion_) {
            selectedVersion_ = release.tag;
            selectedAssetName_ = release.assetName;
            return;
        }
    }
    if (!releaseCache_.empty()) {
        selectedVersion_ = releaseCache_.front().tag;
        selectedAssetName_ = releaseCache_.front().assetName;
    }
}

void OtaManager::appendFirmwareInfoJson(JsonObject root, bool refresh, String& error) {
    root["currentVersion"] = normalizeVersion(APP_VERSION);
    root["currentChip"] = currentChipFamily();
    root["installedAssetName"] = currentReleaseAssetName(settings_);
    root["updateBusy"] = busy_;
    root["updateStatus"] = lastMessage_;
    root["selectedVersion"] = selectedVersion_;
    root["selectedAssetName"] = selectedAssetName_;
    root["selectedOption"] = "";
    root["updatePhase"] = updatePhase_;
    root["updateProgress"] = progressPercent_;
    root["updateBytes"] = progressBytes_;
    root["updateTotalBytes"] = progressTotalBytes_;
    root["releaseRefreshPending"] = pendingReleaseRefresh_;
    root["releaseRefreshInProgress"] = releaseRefreshInProgress_;
    root["releaseRefreshAttemptsRemaining"] = releaseRefreshAttemptsRemaining_;
    root["releaseRefreshAttemptsStarted"] = releaseRefreshAttemptsStarted_;
    root["rollbackPendingVerify"] = rollbackPendingVerify_;
    root["rollbackPendingVersion"] = rollbackPendingVersion_;
    root["rolledBackVersion"] = rolledBackVersion_;
    root["rollbackReason"] = rollbackReason_;
    root["autoCheck"] = settings_.ota.autoCheck;
    root["autoUpdate"] = settings_.ota.autoUpdate;

    if (refresh) {
        if (busy_) {
            error = "Another update is already in progress.";
        } else if (!pendingReleaseRefresh_ && !releaseRefreshInProgress_) {
            pendingReleaseRefresh_ = true;
            releaseRefreshAttemptsRemaining_ = RELEASE_REFRESH_MAX_ATTEMPTS;
            releaseRefreshAttemptsStarted_ = 0;
            releaseRefreshNextAttemptAtMs_ = millis();
            releaseRefreshError_ = "";
            lastMessage_ = "queued release refresh";
            root["updateStatus"] = lastMessage_;
            root["releaseRefreshPending"] = true;
        } else {
            root["releaseRefreshPending"] = pendingReleaseRefresh_;
            root["releaseRefreshInProgress"] = releaseRefreshInProgress_;
        }
    }

    root["latestVersion"] = latestVersion_;
    root["compatibleReleaseCount"] = releaseCache_.size();
    if (!releaseCache_.empty()) {
        std::vector<String> uniqueVersions;
        std::vector<String> latestAssets;
        const String latestTag = latestVersion_.isEmpty() ? releaseCache_.front().tag : latestVersion_;
        JsonArray releases = root["releases"].to<JsonArray>();
        JsonArray releaseOptions = root["releaseOptions"].to<JsonArray>();
        for (const ReleaseInfo& release : releaseCache_) {
            JsonObject item = releases.add<JsonObject>();
            const String optionLabel = releaseOptionLabel(release);
            item["tag"] = release.tag;
            item["name"] = release.name;
            item["publishedAt"] = release.publishedAt;
            item["assetUrl"] = release.assetUrl;
            item["assetName"] = release.assetName;
            item["optionLabel"] = optionLabel;
            item["variantLabel"] = release.variantLabel;
            item["chipFamily"] = release.chipFamily;
            item["prerelease"] = release.prerelease;
            item["isInstalled"] = release.isInstalled;
            item["isLatest"] = release.isLatest;
            item["isNew"] = release.isNew;
            releaseOptions.add(optionLabel);
            if (release.tag == selectedVersion_ && release.assetName == selectedAssetName_) {
                root["selectedOption"] = optionLabel;
            }

            bool seenVersion = false;
            for (const String& version : uniqueVersions) {
                if (version == release.tag) {
                    seenVersion = true;
                    break;
                }
            }
            if (!seenVersion) {
                uniqueVersions.push_back(release.tag);
            }

            if (release.tag == latestTag) {
                latestAssets.push_back(release.assetName);
            }
        }

        String compatibleVersionsSummary;
        for (size_t index = 0; index < uniqueVersions.size(); ++index) {
            if (index > 0) {
                compatibleVersionsSummary += ", ";
            }
            compatibleVersionsSummary += uniqueVersions[index];
        }
        root["compatibleVersionsSummary"] = compatibleVersionsSummary;

        String latestAssetsSummary;
        for (size_t index = 0; index < latestAssets.size(); ++index) {
            if (index > 0) {
                latestAssetsSummary += ", ";
            }
            latestAssetsSummary += latestAssets[index];
        }
        root["latestAssetsSummary"] = latestAssetsSummary;
    }

    if (!error.isEmpty()) {
        root["error"] = error;
    } else if (!releaseRefreshError_.isEmpty()) {
        root["error"] = releaseRefreshError_;
    }
}

bool OtaManager::selectReleaseOption(const String& optionLabel, String& error) {
    if (busy_) {
        error = "Another update is already in progress.";
        return false;
    }
    if (optionLabel.isEmpty()) {
        error = "Select a firmware release first.";
        return false;
    }
    // Selection is called from the main loop's deferred-action service. Never
    // perform HTTPS here; require the already-backgrounded refresh to populate
    // a current cache first.
    if (releaseCache_.empty() || (millis() - releasesFetchedAtMs_) >= RELEASE_CACHE_TTL_MS) {
        error = "Firmware release list is not ready. Run Check Firmware Releases first.";
        return false;
    }

    const ReleaseInfo* release = findReleaseForOption(optionLabel);
    if (release == nullptr) {
        error = "Selected firmware option is not available. Run Check Firmware Releases again.";
        return false;
    }

    selectedVersion_ = release->tag;
    selectedAssetName_ = release->assetName;
    resetProgress();
    lastMessage_ = String("selected ") + release->assetName;
    syncAppState("selected");
    return true;
}

bool OtaManager::triggerInstallSelected(String& error) {
    if (busy_) {
        error = "Another update is already in progress.";
        return false;
    }
    if (selectedVersion_.isEmpty()) {
        error = "Select a firmware release first.";
        return false;
    }
    return triggerInstallVersion(selectedVersion_, selectedAssetName_, error);
}

bool OtaManager::triggerInstallVersion(const String& version, String& error) {
    if (busy_) {
        error = "Another update is already in progress.";
        return false;
    }

    if (version.isEmpty()) {
        error = "Select a firmware release first.";
        return false;
    }

    const String normalizedVersion = normalizeVersion(version);
    pendingInstallVersion_ = normalizedVersion;
    pendingInstallAssetName_ = "";
    pendingInstallAssetUrl_ = "";
    selectedVersion_ = normalizedVersion;
    selectedAssetName_ = "";
    resetProgress();
    lastMessage_ = String("queued install ") + normalizedVersion;
    syncAppState("queued");
    return true;
}

bool OtaManager::triggerInstallVersion(const String& version, const String& assetName, String& error) {
    if (assetName.isEmpty()) {
        return triggerInstallVersion(version, error);
    }
    if (busy_) {
        error = "Another update is already in progress.";
        return false;
    }
    if (version.isEmpty()) {
        error = "Select a firmware release first.";
        return false;
    }

    const String targetChipFamily = chipFamilyForAssetName(assetName);
    if (!isCompatibleChipFamily(targetChipFamily)) {
        error = incompatibleChipMessage(targetChipFamily);
        return false;
    }

    const String normalizedVersion = normalizeVersion(version);
    pendingInstallVersion_ = normalizedVersion;
    pendingInstallAssetName_ = assetName;
    pendingInstallAssetUrl_ = githubReleaseAssetUrl(settings_, normalizedVersion, assetName);
    selectedVersion_ = normalizedVersion;
    selectedAssetName_ = assetName;
    resetProgress();
    lastMessage_ = String("queued install ") + assetName;
    syncAppState("queued");
    return true;
}

void OtaManager::runReleaseRefreshTask() {
    releaseRefreshInProgress_ = true;
    releaseRefreshError_ = "";
    if (releaseRefreshAttemptsRemaining_ > 0) {
        releaseRefreshAttemptsRemaining_--;
    }
    releaseRefreshAttemptsStarted_++;
    lastMessage_ = String("checking releases (attempt ") + releaseRefreshAttemptsStarted_ + "/" + RELEASE_REFRESH_MAX_ATTEMPTS + ")";
    syncAppState("checking releases");

    String error;
    const bool success = fetchAvailableReleases(true, error);

    releaseRefreshInProgress_ = false;
    if (success) {
        pendingReleaseRefresh_ = false;
        releaseRefreshAttemptsRemaining_ = 0;
        lastMessage_ = releaseCache_.empty() ? "No firmware releases found." : "Firmware releases refreshed";
        syncAppState("releases refreshed");
        return;
    }

    if (releaseRefreshAttemptsRemaining_ > 0) {
        pendingReleaseRefresh_ = true;
        releaseRefreshError_ = error;
        releaseRefreshNextAttemptAtMs_ = millis() + RELEASE_REFRESH_RETRY_DELAY_MS;
        lastMessage_ = String("Retrying release refresh...");
        syncAppState("release refresh retry", error);
        return;
    }

    pendingReleaseRefresh_ = false;
    releaseRefreshError_ = error;
    lastMessage_ = error;
    syncAppState("release refresh failed", error);
}

void OtaManager::runTask(bool applyAfterCheck) {
    busy_ = true;
    selectedVersion_ = "";
    resetProgress();
    updatePhase_ = "Checking";
    lastMessage_ = applyAfterCheck ? "checking and applying" : "checking";
    syncAppState(applyAfterCheck ? "checking/apply" : "checking");

    const CheckResult result = checkNow();
    latestVersion_ = result.latestVersion;
    updateAvailable_ = result.updateAvailable;
    lastMessage_ = result.message;
    if (!result.success) {
        busy_ = false;
        syncAppState("error", result.message);
        return;
    }

    if (!applyAfterCheck || !result.updateAvailable) {
        updatePhase_ = "";
        busy_ = false;
        syncAppState(result.updateAvailable ? "available" : "current");
        return;
    }

    recoveryRebootRequested_ = false;
    String installMessage;
    if (!installNow(result, installMessage)) {
        lastMessage_ = installMessage;
        busy_ = false;
        syncAppState("error", installMessage);
        if (recoveryRebootRequested_) {
            updatePhase_ = "Restarting";
            lastMessage_ = installMessage + " Restarting...";
            syncAppState("error", installMessage);
            scheduleReboot(1500);
        }
        return;
    }

    lastMessage_ = installMessage;
    busy_ = false;
    syncAppState("installed");
    scheduleReboot(1500);
}

String OtaManager::pendingInstallVersion() const {
    return selectedVersion_;
}

bool OtaManager::isBusy() const {
    return busy_ || localUploadStarted_ || pendingApply_ || installTaskHandle_ != nullptr ||
        checkTaskHandle_ != nullptr || releaseRefreshTaskHandle_ != nullptr;
}

bool OtaManager::isFirmwareTransferActive() const {
    return localUploadStarted_ || pendingApply_ || installTaskHandle_ != nullptr || !pendingInstallVersion_.isEmpty() ||
        (busy_ && (updatePhase_ == "Connecting" || updatePhase_ == "Downloading" ||
                   updatePhase_ == "Flashing" || updatePhase_ == "Verifying" || updatePhase_ == "Finalizing"));
}

void OtaManager::runVersionTask(const String& version, const String& assetName, const String& assetUrl) {
    busy_ = true;
    selectedVersion_ = version;
    selectedAssetName_ = assetName;
    resetProgress();
    updatePhase_ = "Resolving release";
    lastMessage_ = assetName.isEmpty() ? String("Resolving firmware ") + version : String("Resolving firmware ") + assetName;
    syncAppState("checking");

    CheckResult result;
    String error;
    if (!assetUrl.isEmpty()) {
        result.success = true;
        result.latestVersion = version;
        result.assetName = assetName;
        result.assetUrl = assetUrl;
        result.checksumSha256 = "";
        result.updateAvailable = compareVersions(normalizeVersion(APP_VERSION), version) < 0;
        result.message = result.updateAvailable ? "update available" : "selected release ready";
    } else {
        if (!resolveVersionResult(version, assetName, result, error)) {
            lastMessage_ = error;
            busy_ = false;
            syncAppState("error", error);
            return;
        }
    }
    latestVersion_ = releaseCache_.empty() ? version : latestVersion_;
    updateAvailable_ = compareVersions(normalizeVersion(APP_VERSION), version) < 0;

    recoveryRebootRequested_ = false;
    String installMessage;
    if (!installNow(result, installMessage)) {
        lastMessage_ = installMessage;
        busy_ = false;
        syncAppState("error", installMessage);
        if (recoveryRebootRequested_) {
            updatePhase_ = "Restarting";
            lastMessage_ = installMessage + " Restarting...";
            syncAppState("error", installMessage);
            scheduleReboot(1500);
        }
        return;
    }

    lastMessage_ = installMessage;
    busy_ = false;
    syncAppState("installed");
    scheduleReboot(1500);
}

int OtaManager::compareVersions(const String& left, const String& right) const {
    auto tokenize = [](String value, int* parts, size_t count) {
        value.replace("v", "");
        value.replace("V", "");
        for (size_t i = 0; i < count; ++i) parts[i] = 0;
        size_t index = 0;
        int start = 0;
        while (index < count && start < value.length()) {
            int dot = value.indexOf('.', start);
            String token = dot >= 0 ? value.substring(start, dot) : value.substring(start);
            parts[index++] = token.toInt();
            if (dot < 0) break;
            start = dot + 1;
        }
    };
    int leftParts[4];
    int rightParts[4];
    tokenize(left, leftParts, 4);
    tokenize(right, rightParts, 4);
    for (size_t i = 0; i < 4; ++i) {
        if (leftParts[i] < rightParts[i]) return -1;
        if (leftParts[i] > rightParts[i]) return 1;
    }
    return 0;
}

String OtaManager::normalizeVersion(const String& value) const {
    if (value.isEmpty()) {
        return "";
    }
    if (value.startsWith("v") || value.startsWith("V")) {
        return value;
    }
    return "v" + value;
}

bool OtaManager::fetchAvailableReleases(bool refresh, String& error) {
    if (!refresh && !releaseCache_.empty() && (millis() - releasesFetchedAtMs_) < RELEASE_CACHE_TTL_MS) {
        return true;
    }

    if (WiFi.status() != WL_CONNECTED) {
        error = "Connect to Wi-Fi to check GitHub releases.";
        return false;
    }

    GithubReleaseClient releaseClient;
    std::vector<GithubReleaseMetadata> githubReleases;
    if (!releaseClient.fetch(
            settings_.ota.owner,
            settings_.ota.repository,
            settings_.ota.allowInsecureTls,
            githubReleases,
            error)) {
        return false;
    }

    releaseCache_.clear();
    latestVersion_ = "";
    const String currentVersion = normalizeVersion(APP_VERSION);
    const String installedAssetName = currentReleaseAssetName(settings_);
    for (const GithubReleaseMetadata& release : githubReleases) {
        const String releaseTag = normalizeVersion(release.tag);
        if (releaseTag.isEmpty()) {
            continue;
        }
        const String releaseName = release.name;
        const String publishedAt = release.publishedAt;
        const bool prerelease = release.prerelease;
        if (latestVersion_.isEmpty() && !prerelease) {
            latestVersion_ = releaseTag;
        }

        bool matchedAsset = false;
        for (const String& assetName : release.assetNames) {
            if (!hasBinExtension(assetName)) {
                continue;
            }

            const String chipFamily = chipFamilyForAssetName(assetName);
            // Release assets must identify their chip family in the filename.
            // A generic firmware.bin cannot be proven safe for this device.
            if (chipFamily.isEmpty() || !isCompatibleChipFamily(chipFamily)) {
                continue;
            }

            ReleaseInfo item;
            item.tag = releaseTag;
            item.name = releaseName;
            item.publishedAt = publishedAt;
            item.assetName = assetName;
            item.assetUrl = githubReleaseAssetUrl(settings_, releaseTag, assetName);
            item.variantLabel = variantLabelForAssetName(assetName);
            item.chipFamily = chipFamily;
            item.prerelease = prerelease;
            item.isInstalled = compareVersions(currentVersion, releaseTag) == 0 && assetName == installedAssetName;
            item.isLatest = !latestVersion_.isEmpty() && releaseTag == latestVersion_;
            item.isNew = compareVersions(currentVersion, releaseTag) < 0;
            releaseCache_.push_back(item);
            matchedAsset = true;
        }

        if (!matchedAsset) {
            ReleaseInfo item;
            item.tag = releaseTag;
            item.name = releaseName;
            item.publishedAt = publishedAt;
            item.prerelease = prerelease;
            item.assetName = applyVersionTemplate(effectiveAssetTemplate(settings_), releaseTag);
            item.assetUrl = githubReleaseAssetUrl(settings_, releaseTag, item.assetName);
            item.variantLabel = variantLabelForAssetName(item.assetName);
            item.chipFamily = chipFamilyForAssetName(item.assetName);
            if (isCompatibleChipFamily(item.chipFamily)) {
                item.isInstalled = compareVersions(currentVersion, releaseTag) == 0 && item.assetName == installedAssetName;
                item.isLatest = !latestVersion_.isEmpty() && releaseTag == latestVersion_;
                item.isNew = compareVersions(currentVersion, releaseTag) < 0;
                releaseCache_.push_back(item);
            }
        }
    }

    if (latestVersion_.isEmpty() && !releaseCache_.empty()) {
        latestVersion_ = releaseCache_.front().tag;
        releaseCache_.front().isLatest = true;
    }
    ensureSelectedReleaseStillValid();
    releasesFetchedAtMs_ = millis();
    return true;
}

bool OtaManager::resolveVersionResult(const String& version, const String& assetName, CheckResult& result, String& error) {
    if (!fetchAvailableReleases(false, error)) {
        return false;
    }

    const String preferredAssetName = assetName.isEmpty()
        ? releaseAssetNameForVersion(settings_, version)
        : assetName;
    const ReleaseInfo* fallbackRelease = nullptr;
    if (!assetName.isEmpty()) {
        const String targetChipFamily = chipFamilyForAssetName(assetName);
        if (!isCompatibleChipFamily(targetChipFamily)) {
            error = incompatibleChipMessage(targetChipFamily);
            return false;
        }
    }

    for (const ReleaseInfo& release : releaseCache_) {
        if (release.tag != version) {
            continue;
        }
        if (!assetName.isEmpty() && release.assetName != assetName) {
            continue;
        }
        if (release.assetName == preferredAssetName) {
            result.success = true;
            result.latestVersion = release.tag;
            result.assetName = release.assetName;
            result.assetUrl = release.assetUrl;
            result.checksumSha256 = "";
            result.updateAvailable = compareVersions(normalizeVersion(APP_VERSION), release.tag) < 0;
            result.message = result.updateAvailable ? "update available" : "selected release ready";
            return true;
        }
        if (fallbackRelease == nullptr) {
            fallbackRelease = &release;
        }
    }

    if (fallbackRelease != nullptr) {
        result.success = true;
        result.latestVersion = fallbackRelease->tag;
        result.assetName = fallbackRelease->assetName;
        result.assetUrl = fallbackRelease->assetUrl;
        result.checksumSha256 = "";
        result.updateAvailable = compareVersions(normalizeVersion(APP_VERSION), fallbackRelease->tag) < 0;
        result.message = result.updateAvailable ? "update available" : "selected release ready";
        return true;
    }

    error = assetName.isEmpty() ? String("Release not found: ") + version : String("Release asset not found: ") + assetName;
    return false;
}

OtaManager::CheckResult OtaManager::checkNow() {
    CheckResult result;
    if (WiFi.status() != WL_CONNECTED) {
        result.message = "Wi-Fi not connected";
        return result;
    }

    WiFiClientSecure client;
    HTTPClient http;

    if (!settings_.ota.manifestUrl.isEmpty()) {
        const int code = beginAndGet(
            http,
            client,
            settings_.ota.manifestUrl,
            settings_.ota.allowInsecureTls,
            10000,
            [](HTTPClient&) {});
        if (code == HTTPC_ERROR_CONNECTION_REFUSED) {
            result.message = "Failed to open manifest URL";
            return result;
        }
        if (code != HTTP_CODE_OK) {
            result.message = httpErrorWithDetail(http, "Manifest HTTP ", code);
            http.end();
            return result;
        }
        JsonDocument doc;
        if (deserializeJson(doc, http.getString()) != DeserializationError::Ok) {
            result.message = "Manifest parse failed";
            http.end();
            return result;
        }
        http.end();
        JsonVariantConst release;
        if (doc["release"].isNull()) {
            release = doc.as<JsonVariantConst>();
        } else {
            release = doc["release"].as<JsonVariantConst>();
        }
        result.latestVersion = normalizeVersion(String(static_cast<const char*>(release["version"] | "")));
        result.assetUrl = String(static_cast<const char*>(release["url"] | ""));
        result.assetName = String(static_cast<const char*>(release["asset"] | ""));
        result.checksumSha256 = String(static_cast<const char*>(release["sha256"] | ""));
        if (release["channel"].is<const char*>()) {
            const String channel = String(static_cast<const char*>(release["channel"]));
            if (!settings_.ota.channel.isEmpty() && channel != settings_.ota.channel) {
                result.message = "Manifest channel mismatch";
                return result;
            }
        }
        const String targetChipFamily = chipFamilyFromManifest(release, result.assetName, result.assetUrl);
        if (!isCompatibleChipFamily(targetChipFamily)) {
            result.message = incompatibleChipMessage(targetChipFamily);
            return result;
        }
    } else {
        String error;
        if (!fetchAvailableReleases(false, error)) {
            result.message = error;
            return result;
        }

        result.latestVersion = latestVersion_;
        const String preferredAsset = releaseAssetNameForVersion(settings_, result.latestVersion);
        const ReleaseInfo* fallback = nullptr;
        for (const ReleaseInfo& release : releaseCache_) {
            if (release.tag != result.latestVersion) {
                continue;
            }
            if (fallback == nullptr) {
                fallback = &release;
            }
            if (release.assetName == preferredAsset) {
                fallback = &release;
                break;
            }
        }
        if (fallback != nullptr) {
            result.assetName = fallback->assetName;
            result.assetUrl = fallback->assetUrl;
        }
    }

    if (result.latestVersion.isEmpty() || result.assetUrl.isEmpty()) {
        result.message = "Incomplete release metadata";
        return result;
    }
    result.updateAvailable = compareVersions(normalizeVersion(APP_VERSION), result.latestVersion) < 0;
    result.success = true;
    result.message = result.updateAvailable ? "update available" : "already current";
    return result;
}

bool OtaManager::installNow(const CheckResult& result, String& message) {
    if (!hasWritableOtaTargetPartition(message)) {
        return false;
    }
    selectedVersion_ = result.latestVersion;
    selectedAssetName_ = result.assetName;
    updatePhase_ = "Connecting";
    progressBytes_ = 0;
    progressTotalBytes_ = 0;
    progressPercent_ = 0;
    lastMessage_ = result.assetName.isEmpty() ? "Connecting to firmware URL" : String("Connecting to ") + result.assetName;
    syncAppState("updating");
    pumpProgressCallback();
    WiFiClientSecure client;
    HTTPClient http;
    const String firmwareUrl = (!result.latestVersion.isEmpty() && !result.assetName.isEmpty())
        ? githubReleaseAssetUrl(settings_, result.latestVersion, result.assetName)
        : result.assetUrl;
    auto openFirmwareRequest = [&](size_t startOffset, String& error) {
        DebugLog.printf("[ota] opening firmware url: %s (offset=%u)\n", firmwareUrl.c_str(), static_cast<unsigned>(startOffset));
        const int requestCode = beginAndGet(
            http,
            client,
            firmwareUrl,
            settings_.ota.allowInsecureTls,
            15000,
            [&](HTTPClient& request) {
                if (startOffset > 0) {
                    request.addHeader("Range", String("bytes=") + startOffset + "-");
                }
            });
        if (requestCode == HTTPC_ERROR_CONNECTION_REFUSED) {
            DebugLog.printf("[ota] failed to open firmware url: %s\n", firmwareUrl.c_str());
            error = startOffset > 0 ? "Failed to reopen firmware URL" : "Failed to open firmware URL";
            return false;
        }

        const int expectedCode = startOffset > 0 ? HTTP_CODE_PARTIAL_CONTENT : HTTP_CODE_OK;
        if (requestCode != expectedCode) {
            DebugLog.printf("[ota] firmware request failed code=%d url=%s offset=%u\n", requestCode, firmwareUrl.c_str(), static_cast<unsigned>(startOffset));
            if (startOffset > 0 && requestCode == HTTP_CODE_OK) {
                error = "Firmware server does not support OTA resume";
            } else {
                error = httpErrorWithDetail(http, "Firmware HTTP ", requestCode);
            }
            http.end();
            return false;
        }
        return true;
    };
    if (!openFirmwareRequest(0, message)) {
        return false;
    }
    updatePhase_ = "Downloading";
    lastMessage_ = result.assetName.isEmpty() ? "Downloading firmware" : String("Downloading ") + result.assetName;
    syncAppState("updating");
    pumpProgressCallback();
    const int contentLength = http.getSize();
    if (contentLength <= 0) {
        message = "Invalid content length";
        http.end();
        return false;
    }
    const esp_partition_t* runningPartition = esp_ota_get_running_partition();
    const size_t runningPartitionSize = runningPartition != nullptr ? runningPartition->size : 0;
    if (runningPartitionSize > 0 && static_cast<size_t>(contentLength) > runningPartitionSize) {
        message = String("Firmware image (") + contentLength + " bytes) is larger than the current app slot (" +
            runningPartitionSize + " bytes). This update requires a partition layout change, so install it once using local USB flashing.";
        http.end();
        return false;
    }
    progressTotalBytes_ = static_cast<size_t>(contentLength);
    if (!Update.begin(contentLength)) {
        message = "Not enough space for OTA";
        http.end();
        return false;
    }
    updatePhase_ = "Flashing";
    lastMessage_ = "Starting firmware write";
    syncAppState("updating");
    pumpProgressCallback();

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts_ret(&sha, 0);

    WiFiClient* stream = http.getStreamPtr();
    uint8_t buffer[1024];
    uint8_t imageHeader[sizeof(esp_image_header_t)] = {0};
    size_t imageHeaderBytes = 0;
    bool imageHeaderValidated = false;
    size_t written = 0;
    unsigned long lastProgressAt = millis();
    uint8_t resumeAttempts = 0;
    auto resumeDownload = [&](const char* reason) {
        if (resumeAttempts >= OTA_DOWNLOAD_MAX_RESUME_ATTEMPTS) {
            message = String(reason) + ". Rebooting device to recover from stalled OTA.";
            recoveryRebootRequested_ = true;
            Update.abort();
            http.end();
            mbedtls_sha256_free(&sha);
            return false;
        }

        ++resumeAttempts;
        updatePhase_ = "Retrying";
        lastMessage_ = String("Retrying firmware download ") + resumeAttempts + "/" + OTA_DOWNLOAD_MAX_RESUME_ATTEMPTS + " at " + progressPercent_ + "%";
        syncAppState("updating");
        pumpProgressCallback();
        http.end();
        client.stop();

        String resumeError;
        if (!openFirmwareRequest(written, resumeError)) {
            message = resumeError + ". Rebooting device to recover from stalled OTA.";
            recoveryRebootRequested_ = true;
            Update.abort();
            mbedtls_sha256_free(&sha);
            return false;
        }

        stream = http.getStreamPtr();
        lastProgressAt = millis();
        updatePhase_ = "Flashing";
        lastMessage_ = String("Resuming firmware download at ") + progressPercent_ + "%";
        syncAppState("updating");
        pumpProgressCallback();
        return true;
    };
    while (written < static_cast<size_t>(contentLength)) {
        if (!http.connected()) {
            if (!resumeDownload("OTA connection dropped")) {
                return false;
            }
            continue;
        }
        const size_t available = stream->available();
        if (available == 0) {
            if (static_cast<unsigned long>(millis() - lastProgressAt) >= OTA_DOWNLOAD_STALL_TIMEOUT_MS) {
                if (!resumeDownload("OTA download stalled")) {
                    return false;
                }
                continue;
            }
            pumpProgressCallback();
            delay(1);
            continue;
        }
        const int read = stream->readBytes(buffer, min(sizeof(buffer), available));
        if (read <= 0) {
            if (static_cast<unsigned long>(millis() - lastProgressAt) >= OTA_DOWNLOAD_STALL_TIMEOUT_MS) {
                if (!resumeDownload("OTA read stalled")) {
                    return false;
                }
            }
            continue;
        }
        lastProgressAt = millis();
        resumeAttempts = 0;
        mbedtls_sha256_update_ret(&sha, buffer, read);

        size_t offset = 0;
        if (!imageHeaderValidated) {
            const size_t needed = sizeof(imageHeader) - imageHeaderBytes;
            const size_t toCopy = min(needed, static_cast<size_t>(read));
            memcpy(imageHeader + imageHeaderBytes, buffer, toCopy);
            imageHeaderBytes += toCopy;
            offset += toCopy;

            if (imageHeaderBytes == sizeof(imageHeader)) {
                String chipFamily;
                if (!validateImageHeader(imageHeader, imageHeaderBytes, chipFamily, message)) {
                    Update.abort();
                    http.end();
                    mbedtls_sha256_free(&sha);
                    return false;
                }
                if (Update.write(imageHeader, imageHeaderBytes) != imageHeaderBytes) {
                    message = "OTA write failed";
                    Update.abort();
                    http.end();
                    mbedtls_sha256_free(&sha);
                    return false;
                }
                imageHeaderValidated = true;
            }
        }

        if (imageHeaderValidated && offset < static_cast<size_t>(read)) {
            const size_t remaining = static_cast<size_t>(read) - offset;
            if (Update.write(buffer + offset, remaining) != remaining) {
                message = "OTA write failed";
                Update.abort();
                http.end();
                mbedtls_sha256_free(&sha);
                return false;
            }
        }
        written += static_cast<size_t>(read);
        progressBytes_ = written;
        progressPercent_ = static_cast<uint8_t>(min(100U, static_cast<unsigned>((written * 100U) / static_cast<size_t>(contentLength))));
        lastMessage_ = String("Flashing firmware... ") + progressPercent_ + "%";
        syncAppState("updating");
        pumpProgressCallback();
    }

    if (!imageHeaderValidated) {
        message = "Firmware image header is incomplete.";
        Update.abort();
        http.end();
        mbedtls_sha256_free(&sha);
        return false;
    }

    updatePhase_ = "Verifying";
    lastMessage_ = "Verifying downloaded firmware";
    syncAppState("updating");
    pumpProgressCallback();

    uint8_t digest[32];
    mbedtls_sha256_finish_ret(&sha, digest);
    mbedtls_sha256_free(&sha);
    String actualHash;
    for (uint8_t byte : digest) {
        char chunk[3];
        snprintf(chunk, sizeof(chunk), "%02x", byte);
        actualHash += chunk;
    }
    if (!result.checksumSha256.isEmpty() && !actualHash.equalsIgnoreCase(result.checksumSha256)) {
        message = "SHA256 mismatch";
        Update.abort();
        http.end();
        return false;
    }

    updatePhase_ = "Finalizing";
    lastMessage_ = "Finalizing firmware update";
    syncAppState("updating");
    pumpProgressCallback();

    if (!Update.end()) {
        message = String("Update finalize failed: ") + Update.errorString();
        http.end();
        return false;
    }
    if (!Update.isFinished()) {
        message = "Update incomplete";
        http.end();
        return false;
    }

    http.end();
    updatePhase_ = "Restarting";
    progressBytes_ = progressTotalBytes_;
    progressPercent_ = 100;
    lastMessage_ = "Firmware installed. Restarting...";
    message = "update installed";
    syncAppState("installed");
    pumpProgressCallback();
    return true;
}
