#include "web_server.h"
#include "device_log.h"

#ifdef APP_DISABLE_WEB_UI

WebServerManager::WebServerManager() = default;

void WebServerManager::setWebUiLocked(bool) {
}

bool WebServerManager::webUiLocked() const {
    return true;
}

void WebServerManager::begin(
    AppState&,
    WiFiManager&,
    SettingsManager&,
    OtaManager&,
    SettingsGetter,
    SettingsSaver,
    PlayHandler,
    StopHandler,
    VolumeHandler,
    EqualizerHandler,
    SeekHandler,
    OtaHandler,
    MqttHandler,
    MotorRunHandler,
    MotorConfigSaver,
    StatusAppender,
    SimpleHandler,
    SimpleHandler,
    SimpleHandler,
    SimpleHandler) {
}

#else

#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <cstring>
#include <memory>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>

#include "generated_web_assets.h"
#include "json_buffer_response.h"
#include "storage_backend.h"
#include "system_metrics.h"
#include "version.h"

namespace {
constexpr size_t kStorageReserveBytes = 4096;
constexpr size_t kStorageDirectoryListBatchDefault = 0;
constexpr size_t kSettingsJsonMaxContentLength = 32768;
constexpr uint32_t kClonePartitionTableAddress = 0x8000;
constexpr uint32_t kClonePartitionTableSize = 0x1000;
constexpr uint32_t kCloneOtaDataAddress = 0xe000;
constexpr uint32_t kCloneOtaDataSize = 0x2000;
constexpr uint32_t kCloneApplicationAddress = 0x10000;

#if defined(CONFIG_IDF_TARGET_ESP32S3)
constexpr uint32_t kCloneBootloaderAddress = 0x0000;
constexpr uint32_t kCloneBootloaderSize = 0x8000;
constexpr char kCloneChipFamily[] = "ESP32-S3";
#else
constexpr uint32_t kCloneBootloaderAddress = 0x1000;
constexpr uint32_t kCloneBootloaderSize = 0x7000;
constexpr char kCloneChipFamily[] = "ESP32";
#endif

struct StorageReindexStatus {
    bool active = false;
    bool completed = false;
    bool success = false;
    StorageTarget target = StorageTarget::Sd;
    char directoryPath[160] = "/";
    char stage[24] = "idle";
    char error[160] = "";
    size_t processedEntries = 0;
    size_t totalEntries = 0;
    uint32_t startedAt = 0;
    uint32_t updatedAt = 0;
};

struct StorageReindexTaskParams {
    StorageTarget target = StorageTarget::Sd;
    String directoryPath = "/";
};

StorageReindexStatus storageReindexStatus;
portMUX_TYPE storageReindexMux = portMUX_INITIALIZER_UNLOCKED;

StorageReindexStatus snapshotStorageReindexStatus() {
    portENTER_CRITICAL(&storageReindexMux);
    const StorageReindexStatus snapshot = storageReindexStatus;
    portEXIT_CRITICAL(&storageReindexMux);
    return snapshot;
}

void copyReindexText(char* destination, size_t destinationSize, const String& value) {
    if (destination == nullptr || destinationSize == 0) {
        return;
    }
    strlcpy(destination, value.c_str(), destinationSize);
}

void copyReindexText(char* destination, size_t destinationSize, const char* value) {
    if (destination == nullptr || destinationSize == 0) {
        return;
    }
    strlcpy(destination, value != nullptr ? value : "", destinationSize);
}

void updateStorageReindexStatus(const std::function<void(StorageReindexStatus&)>& updater) {
    portENTER_CRITICAL(&storageReindexMux);
    updater(storageReindexStatus);
    storageReindexStatus.updatedAt = millis();
    portEXIT_CRITICAL(&storageReindexMux);
}

void resetStorageReindexStatus(StorageTarget target, const String& directoryPath) {
    portENTER_CRITICAL(&storageReindexMux);
    storageReindexStatus = StorageReindexStatus{};
    storageReindexStatus.target = target;
    copyReindexText(storageReindexStatus.directoryPath, sizeof(storageReindexStatus.directoryPath), directoryPath);
    storageReindexStatus.active = true;
    copyReindexText(storageReindexStatus.stage, sizeof(storageReindexStatus.stage), "counting");
    storageReindexStatus.startedAt = millis();
    storageReindexStatus.updatedAt = storageReindexStatus.startedAt;
    portEXIT_CRITICAL(&storageReindexMux);
}

const EmbeddedWebAsset* findAsset(const String& path) {
    for (size_t index = 0; index < WEB_ASSET_COUNT; ++index) {
        if (path == WEB_ASSETS[index].path) {
            return &WEB_ASSETS[index];
        }
    }
    return nullptr;
}

String assetEtag(const EmbeddedWebAsset& asset) {
    String tag = "\"";
    tag += APP_VERSION;
    tag += ':';
    tag += asset.path;
    tag += ':';
    tag += String(static_cast<unsigned>(asset.size));
    tag += asset.gzip ? ":gz" : ":raw";
    tag += '"';
    return tag;
}

bool requestMatchesEtag(AsyncWebServerRequest* request, const String& tag) {
    if (request == nullptr || !request->hasHeader("If-None-Match")) {
        return false;
    }
    const AsyncWebHeader* header = request->getHeader("If-None-Match");
    return header != nullptr && header->value() == tag;
}

void sendCloneFlashRegion(
    AsyncWebServerRequest* request,
    uint32_t sourceAddress,
    uint32_t length,
    const String& downloadName) {
    AsyncWebServerResponse* response = request->beginChunkedResponse(
        "application/octet-stream",
        [sourceAddress, length](uint8_t* buffer, size_t maxLen, size_t index) -> size_t {
            if (index >= length || maxLen == 0) {
                return 0;
            }
            const size_t remaining = static_cast<size_t>(length) - index;
            const size_t chunkSize = min(maxLen, remaining);
            const esp_err_t result = esp_flash_read(
                esp_flash_default_chip,
                buffer,
                sourceAddress + static_cast<uint32_t>(index),
                static_cast<uint32_t>(chunkSize));
            return result == ESP_OK ? chunkSize : 0;
        });
    response->addHeader("Cache-Control", "no-store");
    response->addHeader("Content-Disposition", String("attachment; filename=\"") + downloadName + "\"");
    request->send(response);
}

StorageTarget storageTargetFromRequest(AsyncWebServerRequest* request) {
    if (request->hasParam("target")) {
        return parseStorageTarget(request->getParam("target")->value());
    }
    return StorageTarget::Flash;
}

String normalizeStoragePath(const String& rawPath, bool allowRoot = false) {
    String path = rawPath;
    path.replace('\\', '/');
    path.trim();
    if (path.isEmpty()) {
        return allowRoot ? String("/") : String();
    }
    if (!path.startsWith("/")) {
        path = "/" + path;
    }
    while (path.indexOf("//") >= 0) {
        path.replace("//", "/");
    }
    if (path.indexOf("..") >= 0) {
        return String();
    }
    while (path.length() > 1 && path.endsWith("/")) {
        path.remove(path.length() - 1);
    }
    if (!allowRoot && path == "/") {
        return String();
    }
    return path;
}

String storagePathFromRequest(const String& rawPath) {
    return normalizeStoragePath(rawPath, false);
}

String storageDirectoryFromRequest(AsyncWebServerRequest* request) {
    if (!request->hasParam("dir")) {
        return String("/");
    }
    return normalizeStoragePath(request->getParam("dir")->value(), true);
}

String sanitizeUploadFilename(const String& filename) {
    String clean = filename;
    clean.replace('\\', '/');
    const int slashIndex = clean.lastIndexOf('/');
    if (slashIndex >= 0) {
        clean = clean.substring(slashIndex + 1);
    }
    clean.trim();
    if (clean.isEmpty()) {
        return String();
    }

    String sanitized;
    sanitized.reserve(clean.length());
    for (size_t index = 0; index < clean.length(); ++index) {
        const char ch = clean.charAt(index);
        const bool allowed = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
            ch == '.' || ch == '-' || ch == '_' || ch == ' ';
        sanitized += allowed ? ch : '_';
    }

    sanitized.trim();
    return sanitized;
}

String storageBaseName(const String& path) {
    if (path.isEmpty() || path == "/") {
        return String("/");
    }
    const int slashIndex = path.lastIndexOf('/');
    return slashIndex >= 0 ? path.substring(slashIndex + 1) : path;
}

String storageParentPath(const String& path) {
    const String normalized = normalizeStoragePath(path, true);
    if (normalized.isEmpty() || normalized == "/") {
        return String();
    }
    const int slashIndex = normalized.lastIndexOf('/');
    if (slashIndex <= 0) {
        return String("/");
    }
    return normalized.substring(0, slashIndex);
}

String joinStoragePath(const String& directoryPath, const String& name) {
    String dir = normalizeStoragePath(directoryPath, true);
    String leaf = name;
    leaf.replace('\\', '/');
    while (leaf.startsWith("/")) {
        leaf.remove(0, 1);
    }
    while (leaf.endsWith("/")) {
        leaf.remove(leaf.length() - 1);
    }
    leaf.trim();
    if (dir.isEmpty() || leaf.isEmpty()) {
        return String();
    }
    return dir == "/" ? String("/") + leaf : dir + "/" + leaf;
}

String storageDownloadName(const String& path) {
    if (!path.startsWith("/")) {
        return path;
    }
    return path.substring(1);
}

String storageIndexPathForDirectory(const String& directoryPath) {
    const String normalized = normalizeStoragePath(directoryPath, true);
    if (normalized.isEmpty()) {
        return String();
    }
    return normalized == "/" ? String("/.index") : normalized + "/.index";
}

String parseIndexedStorageEntryPath(const String& directoryPath, const String& rawLine, bool& isDirectoryHint) {
    String line = rawLine;
    isDirectoryHint = false;
    line.replace("\r", "");
    line.trim();
    if (line.isEmpty() || line.startsWith("#")) {
        return String();
    }

    const int commaIndex = line.indexOf(',');
    if (commaIndex > 0) {
        const String metadata = line.substring(commaIndex + 1);
        line = line.substring(0, commaIndex);
        const int metadataSeparator = metadata.indexOf(',');
        const String directoryToken = metadataSeparator >= 0 ? metadata.substring(0, metadataSeparator) : metadata;
        const String normalizedDirectoryToken = directoryToken;
        isDirectoryHint = normalizedDirectoryToken == "1" || normalizedDirectoryToken.equalsIgnoreCase("true") || normalizedDirectoryToken.equalsIgnoreCase("dir");
    } else {
        int separatorIndex = line.indexOf('\t');
        const int pipeIndex = line.indexOf('|');
        if (separatorIndex < 0 || (pipeIndex >= 0 && pipeIndex < separatorIndex)) {
            separatorIndex = pipeIndex;
        }
        if (separatorIndex > 0) {
            line = line.substring(0, separatorIndex);
            line.trim();
        }
    }

    if (line.isEmpty() || line == ".index" || line == "/.index") {
        return String();
    }

    while (line.endsWith("/")) {
        isDirectoryHint = true;
        line.remove(line.length() - 1);
    }
    line.trim();
    if (line.isEmpty()) {
        return String();
    }

    return line.startsWith("/") ? normalizeStoragePath(line, false) : joinStoragePath(directoryPath, line);
}

bool isAllowedStoragePath(const String& path) {
    const String lower = path;
    return lower.endsWith(".mp3") || lower.endsWith(".wav") || lower.endsWith(".aac") || lower.endsWith(".m4a") ||
        lower.endsWith(".ogg") || lower.endsWith(".opus") || lower.endsWith(".flac") ||
        lower.endsWith(".jpg") || lower.endsWith(".jpeg") || lower.endsWith(".png") || lower.endsWith(".webp");
}

const char* contentTypeForPath(const String& path) {
    const String lower = path;
    if (lower.endsWith(".mp3")) return "audio/mpeg";
    if (lower.endsWith(".wav")) return "audio/wav";
    if (lower.endsWith(".aac")) return "audio/aac";
    if (lower.endsWith(".m4a")) return "audio/mp4";
    if (lower.endsWith(".ogg")) return "audio/ogg";
    if (lower.endsWith(".opus")) return "audio/ogg";
    if (lower.endsWith(".flac")) return "audio/flac";
    if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) return "image/jpeg";
    if (lower.endsWith(".png")) return "image/png";
    if (lower.endsWith(".webp")) return "image/webp";
    return "application/octet-stream";
}

String storageUnavailableMessage(StorageTarget target) {
    return String(storageTargetLabel(target)) + " is not mounted.";
}

void appendStorageSummaryJson(StorageTarget target, JsonObject root) {
    const StorageBackendSummary summary = getStorageSummary(target);
    root["target"] = storageTargetId(target);
    root["label"] = storageTargetLabel(target);
    root["available"] = summary.available;
    root["mounted"] = summary.mounted;
    root["cardSizeBytes"] = summary.cardSizeBytes;
    root["totalBytes"] = summary.totalBytes;
    root["usedBytes"] = summary.usedBytes;
    root["freeBytes"] = summary.freeBytes;
    root["maxUploadBytes"] = summary.freeBytes > kStorageReserveBytes ? summary.freeBytes - kStorageReserveBytes : 0;
}

size_t requestSizeParam(AsyncWebServerRequest* request, const char* name, size_t fallbackValue) {
    if (request == nullptr || !request->hasParam(name)) {
        return fallbackValue;
    }
    const String rawValue = request->getParam(name)->value();
    if (rawValue.isEmpty()) {
        return fallbackValue;
    }
    const unsigned long parsed = strtoul(rawValue.c_str(), nullptr, 10);
    return static_cast<size_t>(parsed);
}

bool requestFlagParam(AsyncWebServerRequest* request, const char* name) {
    if (request == nullptr || !request->hasParam(name)) {
        return false;
    }
    const String rawValue = request->getParam(name)->value();
    return rawValue.isEmpty() || rawValue == "1" || rawValue.equalsIgnoreCase("true") || rawValue.equalsIgnoreCase("yes");
}

bool appendStorageEntriesFromIndex(StorageTarget target, const String& directoryPath, JsonArray files, size_t offset, size_t limit, size_t& nextOffset, bool& hasMore, size_t& totalEntries) {
    if (!storageMounted(target)) {
        nextOffset = offset;
        hasMore = false;
        totalEntries = 0;
        return false;
    }

    const String indexPath = storageIndexPathForDirectory(directoryPath);
    if (indexPath.isEmpty() || !storageExists(target, indexPath)) {
        return false;
    }

    beginStorageRead(target);
    File indexFile = storageOpen(target, indexPath, "r");
    if (!indexFile || indexFile.isDirectory()) {
        endStorageRead(target);
        nextOffset = offset;
        hasMore = false;
        totalEntries = 0;
        return false;
    }

    size_t scannedEntries = 0;
    size_t returnedEntries = 0;
    while (indexFile.available()) {
        bool isDirectoryHint = false;
        const String entryPath = parseIndexedStorageEntryPath(directoryPath, indexFile.readStringUntil('\n'), isDirectoryHint);
        if (entryPath.isEmpty()) {
            continue;
        }
        totalEntries += 1;
        if (scannedEntries++ < offset) {
            continue;
        }
        if (limit > 0 && returnedEntries >= limit) {
            hasMore = true;
            continue;
        }

        File entry = storageOpen(target, entryPath, "r");
        if (!entry) {
            continue;
        }

        const bool isDirectory = isDirectoryHint || entry.isDirectory();
        JsonObject file = files.add<JsonObject>();
        file["path"] = entryPath;
        file["name"] = storageBaseName(entryPath);
        file["isDirectory"] = isDirectory;
        file["sizeBytes"] = isDirectory ? 0U : static_cast<uint32_t>(entry.size());
        if (!isDirectory) {
            file["url"] = String("/api/storage/file?target=") + storageTargetId(target) + "&path=" + entryPath;
        }
        entry.close();

        returnedEntries += 1;
        if ((scannedEntries % 32U) == 0U) {
            delay(0);
        }
    }

    indexFile.close();
    endStorageRead(target);
    nextOffset = offset + returnedEntries;
    return true;
}

void appendStorageEntriesJson(StorageTarget target, const String& directoryPath, JsonArray files, size_t offset, size_t limit, size_t& nextOffset, bool& hasMore, size_t& totalEntries, bool exhaustiveScan = true) {
    if (!storageMounted(target)) {
        nextOffset = offset;
        hasMore = false;
        totalEntries = 0;
        return;
    }

    beginStorageRead(target);
    File root = storageOpen(target, directoryPath, "r");
    if (!root || !root.isDirectory()) {
        endStorageRead(target);
        nextOffset = offset;
        hasMore = false;
        totalEntries = 0;
        return;
    }

    size_t scannedEntries = 0;
    size_t returnedEntries = 0;
    for (File entry = root.openNextFile(); entry; entry = root.openNextFile()) {
        if (exhaustiveScan) {
            totalEntries += 1;
        }
        if (scannedEntries++ < offset) {
            entry.close();
            continue;
        }
        if (limit > 0 && returnedEntries >= limit) {
            entry.close();
            hasMore = true;
            if (!exhaustiveScan) {
                break;
            }
            continue;
        }

        JsonObject file = files.add<JsonObject>();
        const String path = String(entry.path());
        const bool isDirectory = entry.isDirectory();
        file["path"] = path;
        file["name"] = storageBaseName(path);
        file["isDirectory"] = isDirectory;
        file["sizeBytes"] = isDirectory ? 0U : static_cast<uint32_t>(entry.size());
        if (!isDirectory) {
            file["url"] = String("/api/storage/file?target=") + storageTargetId(target) + "&path=" + path;
        }
        returnedEntries += 1;
        entry.close();
        if ((scannedEntries % 16U) == 0U) {
            delay(0);
        }
    }

    root.close();
    endStorageRead(target);
    nextOffset = offset + returnedEntries;
    if (!exhaustiveScan && totalEntries == 0) {
        totalEntries = returnedEntries + offset + (hasMore ? 1U : 0U);
    }
}

bool rebuildStorageDirectoryIndex(StorageTarget target, const String& directoryPath) {
    if (!storageMounted(target)) {
        return false;
    }

    const String normalizedDirectory = normalizeStoragePath(directoryPath, true);
    const String indexPath = storageIndexPathForDirectory(normalizedDirectory);
    if (normalizedDirectory.isEmpty() || indexPath.isEmpty()) {
        return false;
    }

    fs::FS* fs = getStorageFs(target);
    if (fs == nullptr) {
        return false;
    }
    const String tempIndexPath = indexPath + ".tmp";
    fs->remove(tempIndexPath);

    beginStorageWrite(target);
    File indexFile = storageOpen(target, tempIndexPath, "w");
    if (!indexFile) {
        endStorageWrite(target);
        return false;
    }

    beginStorageRead(target);
    File root = storageOpen(target, normalizedDirectory, "r");
    if (!root || !root.isDirectory()) {
        indexFile.close();
        fs->remove(tempIndexPath);
        endStorageWrite(target);
        endStorageRead(target);
        return false;
    }

    bool writeOk = true;
    size_t entryCount = 0;
    for (File entry = root.openNextFile(); entry; entry = root.openNextFile()) {
        const String path = String(entry.path());
        String name = storageBaseName(path);
        if (name == ".index" || name == ".index.tmp") {
            entry.close();
            continue;
        }
        if (entry.isDirectory()) {
            name += "/";
        }
        if (indexFile.print(name) != name.length() || indexFile.write('\n') != 1) {
            writeOk = false;
            entry.close();
            break;
        }
        entry.close();
        entryCount += 1;
        if ((entryCount % 16U) == 0U) {
            delay(0);
        }
    }

    root.close();
    endStorageRead(target);
    indexFile.flush();
    indexFile.close();
    if (!writeOk) {
        fs->remove(tempIndexPath);
        endStorageWrite(target);
        return false;
    }
    fs->remove(indexPath);
    const bool renamed = fs->rename(tempIndexPath, indexPath);
    if (!renamed) {
        fs->remove(tempIndexPath);
    }
    endStorageWrite(target);
    return renamed;
}

size_t countStorageDirectoryEntries(StorageTarget target, const String& directoryPath) {
    if (!storageMounted(target)) {
        return 0;
    }

    beginStorageRead(target);
    File root = storageOpen(target, directoryPath, "r");
    if (!root || !root.isDirectory()) {
        if (root) {
            root.close();
        }
        endStorageRead(target);
        return 0;
    }

    size_t totalEntries = 0;
    for (File entry = root.openNextFile(); entry; entry = root.openNextFile()) {
        const String name = storageBaseName(String(entry.path()));
        if (name == ".index" || name == ".index.tmp") {
            entry.close();
            continue;
        }
        totalEntries += 1;
        entry.close();
        if ((totalEntries % 32U) == 0U) {
            delay(0);
        }
    }

    root.close();
    endStorageRead(target);
    return totalEntries;
}

void appendStorageReindexStatusJson(JsonObject root, const StorageReindexStatus& status) {
    root["active"] = status.active;
    root["completed"] = status.completed;
    root["success"] = status.success;
    root["target"] = storageTargetId(status.target);
    root["directoryPath"] = status.directoryPath;
    root["stage"] = status.stage;
    root["error"] = status.error;
    root["processedEntries"] = status.processedEntries;
    root["totalEntries"] = status.totalEntries;
    root["startedAt"] = status.startedAt;
    root["updatedAt"] = status.updatedAt;
    const size_t total = status.totalEntries;
    const size_t processed = min(status.processedEntries, total > 0 ? total : status.processedEntries);
    root["percent"] = total > 0 ? static_cast<uint8_t>((processed * 100U) / total) : 0U;
}

void storageReindexTask(void* rawParams) {
    std::unique_ptr<StorageReindexTaskParams> params(static_cast<StorageReindexTaskParams*>(rawParams));
    const StorageTarget target = params ? params->target : StorageTarget::Sd;
    const String directoryPath = params ? params->directoryPath : String("/");
    bool success = false;
    String error;

    if (!storageMounted(target)) {
        error = storageUnavailableMessage(target);
    } else {
        fs::FS* fs = getStorageFs(target);
        const String normalizedDirectory = normalizeStoragePath(directoryPath, true);
        const String indexPath = storageIndexPathForDirectory(normalizedDirectory);
        const String tempIndexPath = indexPath + ".tmp";
        if (fs == nullptr || normalizedDirectory.isEmpty() || indexPath.isEmpty()) {
            error = "Invalid storage path.";
        } else {
            beginStorageRead(target);
            File root = storageOpen(target, normalizedDirectory, "r");
            if (!root || !root.isDirectory()) {
                error = "Directory not found.";
                if (root) {
                    root.close();
                }
                endStorageRead(target);
            } else {
                size_t entryCount = 0;
                for (File entry = root.openNextFile(); entry; entry = root.openNextFile()) {
                    const String name = storageBaseName(String(entry.path()));
                    if (name == ".index" || name == ".index.tmp") {
                        entry.close();
                        continue;
                    }
                    entryCount += 1;
                    entry.close();
                    if ((entryCount % 16U) == 0U) {
                        updateStorageReindexStatus([entryCount](StorageReindexStatus& status) {
                            status.processedEntries = entryCount;
                        });
                        delay(0);
                    }
                }
                root.close();
                endStorageRead(target);

                updateStorageReindexStatus([entryCount](StorageReindexStatus& status) {
                    status.totalEntries = max<size_t>(entryCount * 2U, 1U);
                    status.processedEntries = entryCount;
                    copyReindexText(status.stage, sizeof(status.stage), "writing");
                });

                fs->remove(tempIndexPath);
                beginStorageWrite(target);
                File indexFile = storageOpen(target, tempIndexPath, "w");
                if (!indexFile) {
                    error = "Unable to create index file.";
                    endStorageWrite(target);
                } else {
                    beginStorageRead(target);
                    root = storageOpen(target, normalizedDirectory, "r");
                    if (!root || !root.isDirectory()) {
                        error = "Directory not found.";
                        if (root) {
                            root.close();
                        }
                        indexFile.close();
                        fs->remove(tempIndexPath);
                        endStorageRead(target);
                        endStorageWrite(target);
                    } else {
                        size_t writtenEntries = 0;
                        bool writeOk = true;
                        for (File entry = root.openNextFile(); entry; entry = root.openNextFile()) {
                            String name = storageBaseName(String(entry.path()));
                            if (name == ".index" || name == ".index.tmp") {
                                entry.close();
                                continue;
                            }
                            if (entry.isDirectory()) {
                                name += "/";
                            }
                            if (indexFile.print(name) != name.length() || indexFile.write('\n') != 1) {
                                writeOk = false;
                                entry.close();
                                break;
                            }
                            writtenEntries += 1;
                            entry.close();
                            if ((writtenEntries % 16U) == 0U) {
                                updateStorageReindexStatus([entryCount, writtenEntries](StorageReindexStatus& status) {
                                    status.processedEntries = entryCount + writtenEntries;
                                });
                                delay(0);
                            }
                        }

                        root.close();
                        endStorageRead(target);
                        indexFile.flush();
                        indexFile.close();

                        if (!writeOk) {
                            error = "Unable to write index file.";
                            fs->remove(tempIndexPath);
                            endStorageWrite(target);
                        } else {
                            fs->remove(indexPath);
                            success = fs->rename(tempIndexPath, indexPath);
                            if (!success) {
                                error = "Unable to replace index file.";
                                fs->remove(tempIndexPath);
                            }
                            endStorageWrite(target);
                            updateStorageReindexStatus([entryCount, writtenEntries](StorageReindexStatus& status) {
                                status.processedEntries = max<size_t>(entryCount * 2U, entryCount + writtenEntries);
                            });
                        }
                    }
                }
            }
        }
    }

    updateStorageReindexStatus([&](StorageReindexStatus& status) {
        status.active = false;
        status.completed = true;
        status.success = success;
        copyReindexText(status.stage, sizeof(status.stage), success ? "complete" : "error");
        copyReindexText(status.error, sizeof(status.error), error);
    });
    vTaskDelete(nullptr);
}

bool beginStorageReindexJob(StorageTarget target, const String& directoryPath, String& error) {
    const StorageReindexStatus current = snapshotStorageReindexStatus();
    if (current.active) {
        error = "A storage reindex is already in progress.";
        return false;
    }

    std::unique_ptr<StorageReindexTaskParams> params(new StorageReindexTaskParams());
    if (!params) {
        error = "Unable to allocate reindex task.";
        return false;
    }
    params->target = target;
    params->directoryPath = normalizeStoragePath(directoryPath, true);
    resetStorageReindexStatus(target, params->directoryPath);

    StorageReindexTaskParams* released = params.release();
    const BaseType_t taskCreated = xTaskCreate(
        storageReindexTask,
        "storage_reindex",
        8192,
        released,
        1,
        nullptr);
    if (taskCreated != pdPASS) {
        delete released;
        updateStorageReindexStatus([](StorageReindexStatus& status) {
            status.active = false;
            status.completed = true;
            status.success = false;
            copyReindexText(status.stage, sizeof(status.stage), "error");
            copyReindexText(status.error, sizeof(status.error), "Unable to start reindex task.");
        });
        error = "Unable to start reindex task.";
        return false;
    }
    return true;
}

bool removeStoragePathRecursive(StorageTarget target, const String& path) {
    fs::FS* fs = getStorageFs(target);
    if (fs == nullptr) {
        return false;
    }

    File entry = fs->open(path, "r");
    if (!entry) {
        return false;
    }
    if (!entry.isDirectory()) {
        entry.close();
        return fs->remove(path);
    }

    while (File child = entry.openNextFile()) {
        const String childPath = String(child.path());
        child.close();
        if (!removeStoragePathRecursive(target, childPath)) {
            entry.close();
            return false;
        }
    }
    entry.close();
    return path != "/" && fs->rmdir(path);
}

bool createStorageDirectory(StorageTarget target, const String& path) {
    fs::FS* fs = getStorageFs(target);
    return fs != nullptr && path != "/" && fs->mkdir(path);
}

void appendStorageDirectoryJson(StorageTarget target, const String& directoryPath, JsonDocument& response, AsyncWebServerRequest* request = nullptr) {
    const String currentPath = normalizeStoragePath(directoryPath, true);
    response["target"] = storageTargetId(target);
    response["label"] = storageTargetLabel(target);
    response["currentPath"] = currentPath;
    response["parentPath"] = storageParentPath(currentPath);
    const size_t offset = requestSizeParam(request, "offset", 0);
    const size_t limit = requestSizeParam(request, "limit", kStorageDirectoryListBatchDefault);
    const bool preferLiveScan = requestFlagParam(request, "live");
    const bool rebuildIndex = requestFlagParam(request, "reindex");
    JsonObject storage = response["storage"].to<JsonObject>();
    appendStorageSummaryJson(target, storage);
    if (rebuildIndex) {
        rebuildStorageDirectoryIndex(target, currentPath);
    }
    size_t nextOffset = 0;
    bool hasMore = false;
    size_t totalEntries = 0;
    JsonArray entries = response["entries"].to<JsonArray>();
    if (!preferLiveScan && !appendStorageEntriesFromIndex(target, currentPath, entries, offset, limit, nextOffset, hasMore, totalEntries)) {
        appendStorageEntriesJson(target, currentPath, entries, offset, limit, nextOffset, hasMore, totalEntries);
    } else if (preferLiveScan) {
        appendStorageEntriesJson(target, currentPath, entries, offset, limit, nextOffset, hasMore, totalEntries, false);
    }
    response["offset"] = offset;
    response["returned"] = nextOffset >= offset ? nextOffset - offset : 0;
    response["nextOffset"] = nextOffset;
    response["hasMore"] = hasMore;
    response["totalEntries"] = totalEntries;
}
}  // namespace

WebServerManager::WebServerManager() : server_(80) {}

bool WebServerManager::ensureStorageTransferBuffer(size_t minimumSize) {
    if (minimumSize == 0) {
        return true;
    }
    if (storageTransferBuffer_ != nullptr && storageTransferBufferCapacity_ >= minimumSize) {
        return true;
    }

    if (storageTransferBuffer_ != nullptr) {
        heap_caps_free(storageTransferBuffer_);
        storageTransferBuffer_ = nullptr;
        storageTransferBufferCapacity_ = 0;
    }

    void* memory = nullptr;
    if (psramFound()) {
        memory = heap_caps_malloc(minimumSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (memory == nullptr) {
        memory = heap_caps_malloc(minimumSize, MALLOC_CAP_8BIT);
    }
    if (memory == nullptr) {
        return false;
    }

    storageTransferBuffer_ = static_cast<uint8_t*>(memory);
    storageTransferBufferCapacity_ = minimumSize;
    return true;
}

void WebServerManager::begin(
    AppState& appState,
    WiFiManager& wifiManager,
    SettingsManager& settingsManager,
    OtaManager& otaManager,
    SettingsGetter settingsGetter,
    SettingsSaver settingsSaver,
    PlayHandler playHandler,
    StopHandler stopHandler,
    VolumeHandler volumeHandler,
    EqualizerHandler equalizerHandler,
    SeekHandler seekHandler,
    OtaHandler otaHandler,
    MqttHandler mqttHandler,
    MotorRunHandler motorRunHandler,
    MotorConfigSaver motorConfigSaver,
    StatusAppender motorStatusAppender,
    SimpleHandler displayTriggerHandler,
    SimpleHandler serverShutdownHandler,
    SimpleHandler rebootHandler,
    SimpleHandler factoryResetHandler) {
    appState_ = &appState;
    wifiManager_ = &wifiManager;
    settingsManager_ = &settingsManager;
    otaManager_ = &otaManager;
    settingsGetter_ = settingsGetter;
    settingsSaver_ = settingsSaver;
    playHandler_ = playHandler;
    stopHandler_ = stopHandler;
    volumeHandler_ = volumeHandler;
    equalizerHandler_ = equalizerHandler;
    seekHandler_ = seekHandler;
    otaHandler_ = otaHandler;
    mqttHandler_ = mqttHandler;
    motorRunHandler_ = motorRunHandler;
    motorConfigSaver_ = motorConfigSaver;
    motorStatusAppender_ = motorStatusAppender;
    displayTriggerHandler_ = displayTriggerHandler;
    serverShutdownHandler_ = serverShutdownHandler;
    rebootHandler_ = rebootHandler;
    factoryResetHandler_ = factoryResetHandler;

    registerApiRoutes();
    registerWebRoutes();
    server_.begin();
}

bool WebServerManager::ensureAuthorized(AsyncWebServerRequest* request) {
    if (rejectIfWebUiLocked(request)) {
        return false;
    }
    const SettingsBundle settings = settingsGetter_();
    if (!settings.webAuth.enabled) {
        return true;
    }
    if (request->authenticate(settings.webAuth.username.c_str(), settings.webAuth.password.c_str())) {
        return true;
    }
    request->requestAuthentication();
    return false;
}

bool WebServerManager::rejectIfWebUiLocked(AsyncWebServerRequest* request) {
    if (!webUiLocked_) {
        return false;
    }

    if (request->url().startsWith("/api/")) {
        JsonDocument doc;
        doc["error"] = "Web interface is locked. Unlock it via MQTT command <baseTopic>/cmd/web_ui with payload unlock.";
        sendJson(request, doc, 423);
    } else {
        request->send(423, "text/plain", "Web interface is locked. Unlock it via MQTT command <baseTopic>/cmd/web_ui with payload unlock.");
    }
    return true;
}

void WebServerManager::setWebUiLocked(bool locked) {
    webUiLocked_ = locked;
}

bool WebServerManager::webUiLocked() const {
    return webUiLocked_;
}

bool WebServerManager::redirectCaptivePortalIfNeeded(AsyncWebServerRequest* request) {
    if (wifiManager_ != nullptr && wifiManager_->shouldRedirectCaptivePortal(request->host())) {
        request->redirect("http://192.168.4.1/");
        return true;
    }
    return false;
}

void WebServerManager::sendJson(AsyncWebServerRequest* request, const JsonDocument& doc, int statusCode) {
    const size_t length = measureJson(doc);
    // Keep room for TCP and the response object's send buffer. Failed JSON
    // allocations must return a retryable response, never reset the device.
    char* data = nullptr;
    if (!doc.overflowed() && ESP.getFreeHeap() > length + 12000 &&
        heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) > length + 1) {
        data = static_cast<char*>(malloc(length + 1));
    }
    if (data == nullptr) {
        request->send(503, "application/json", "{\"error\":\"Memory busy; retry shortly.\"}");
        return;
    }
    if (serializeJson(doc, data, length + 1) != length) {
        free(data);
        request->send(503, "application/json", "{\"error\":\"JSON response unavailable.\"}");
        return;
    }
    auto* response = new (std::nothrow) JsonBufferResponse(data, length, statusCode);
    if (response == nullptr) {
        free(data);
        request->send(503, "application/json", "{\"error\":\"Memory busy; retry shortly.\"}");
        return;
    }
    request->send(response);
}

void WebServerManager::registerApiRoutes() {
    server_.on("/api/logs", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) return;
        JsonDocument doc;
        const String since = request->hasParam("since") ? request->getParam("since")->value() : String();
        if (!DebugLog.snapshot(doc.to<JsonObject>(), since)) {
            doc.clear();
            doc["error"] = "Log storage is busy; retry shortly.";
            sendJson(request, doc, 503);
            return;
        }
        sendJson(request, doc);
    });
    server_.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
            return;
        }
        JsonDocument doc;
        JsonObject root = doc.to<JsonObject>();
#ifndef APP_LEGACY_OTA_FIT
        appState_->toJson(root);
        wifiManager_->appendTxPowerStatus(root["network"].as<JsonObject>());
        appendSystemMetricsJson(root);
        root["system"]["webUiLocked"] = webUiLocked_;
#else
        root["system"]["deviceName"] = settingsGetter_().device.friendlyName;
#endif
        JsonObject firmware = doc["firmware"].to<JsonObject>();
        firmware["version"] = APP_VERSION;
        firmware["buildDate"] = APP_BUILD_DATE;
    #if defined(CONFIG_IDF_TARGET_ESP32S3)
        firmware["chipFamily"] = "esp32s3";
    #elif defined(CONFIG_IDF_TARGET_ESP32C3)
        firmware["chipFamily"] = "esp32c3";
    #elif defined(CONFIG_IDF_TARGET_ESP32)
        firmware["chipFamily"] = "esp32";
    #else
        firmware["chipFamily"] = ESP.getChipModel();
    #endif
    #ifdef APP_DISABLE_AUDIO
        firmware["audioEnabled"] = false;
    #else
        firmware["audioEnabled"] = true;
    #endif
#ifndef APP_LEGACY_OTA_FIT
        firmware["buttonEventTopic"] = settingsGetter_().mqtt.baseTopic + "/event/button_action";
        otaManager_->appendStatusJson(doc["otaManager"].to<JsonObject>());
        if (motorStatusAppender_) {
            motorStatusAppender_(doc["motor"].to<JsonObject>());
        }
#endif
        sendJson(request, doc);
    });

#ifndef APP_LEGACY_OTA_FIT
    server_.on("/api/settings", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
            return;
        }
        JsonDocument doc;
        settingsManager_->toJson(settingsGetter_(), doc.to<JsonObject>());
        sendJson(request, doc);
    });
#endif

#ifndef APP_LEGACY_OTA_FIT
    server_.on("/api/wifi/scan", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
            return;
        }
        JsonDocument doc;
        const bool start = request->hasParam("start") && request->getParam("start")->value() == "1";
        if (start) {
            doc["started"] = wifiManager_->startScan();
        }
        const WiFiManager::ScanSnapshot snapshot = wifiManager_->getScanSnapshot();
        doc["scanning"] = snapshot.active;
        doc["complete"] = snapshot.complete;
        doc["failed"] = snapshot.failed;
        doc["scanAgeMs"] = snapshot.ageMs;
        if (snapshot.complete) {
            wifiManager_->appendScanResultsJson(doc["networks"].to<JsonArray>());
        }
        sendJson(request, doc);
    });

    server_.on("/api/wifi/handoff", HTTP_POST, [this](AsyncWebServerRequest* request) {
        if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
            return;
        }

        IPAddress stationIp;
        uint32_t shutdownDelayMs = 0;
        JsonDocument doc;
        if (!wifiManager_->prepareStationHandoff(stationIp, shutdownDelayMs)) {
            doc["error"] = "Station Wi-Fi is not connected yet.";
            sendJson(request, doc, 409);
            return;
        }

        const String stationIpText = stationIp.toString();
        doc["ok"] = true;
        doc["stationIp"] = stationIpText;
        doc["redirectUrl"] = String("http://") + stationIpText + "/";
        doc["shutdownDelayMs"] = shutdownDelayMs;
        doc["accessPointWillStop"] = shutdownDelayMs > 0;
        sendJson(request, doc);
    });

#endif

    server_.on(
        "/api/settings",
        HTTP_POST,
        [this](AsyncWebServerRequest* request) {
            if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
                return;
            }
            if (!request->contentType().equalsIgnoreCase("application/json")) {
                request->send(415, "application/json", "{\"error\":\"expected application/json\"}");
                return;
            }
            if (request->contentLength() > kSettingsJsonMaxContentLength) {
                request->send(413, "application/json", "{\"error\":\"settings payload too large\"}");
                return;
            }
            if (request->_tempObject == nullptr) {
                request->send(400, "application/json", "{\"error\":\"invalid json\"}");
                return;
            }

            const size_t bodyLength = request->contentLength();
            const size_t jsonCapacity = bodyLength == 0 ? 4096U : (bodyLength * 2U + 1024U);
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
            DynamicJsonDocument document(jsonCapacity);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
            const DeserializationError parseError = deserializeJson(document, static_cast<const char*>(request->_tempObject));
            if (parseError || document.overflowed() || !document.is<JsonObject>()) {
                request->send(400, "application/json", "{\"error\":\"invalid json\"}");
                return;
            }

            String message;
            if (!settingsSaver_(document.as<JsonVariantConst>(), message)) {
                request->send(400, "application/json", String("{\"error\":\"") + message + "\"}");
                return;
            }
            JsonDocument response;
            response["ok"] = true;
            sendJson(request, response);
        },
        nullptr,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (total == 0 || total > kSettingsJsonMaxContentLength) {
                return;
            }

            if (index == 0 && request->_tempObject == nullptr) {
                request->_tempObject = calloc(total + 1, sizeof(uint8_t));
                if (request->_tempObject == nullptr) {
                    request->abort();
                    return;
                }
            }

            if (request->_tempObject == nullptr || index + len > total) {
                request->abort();
                return;
            }

            memcpy(static_cast<uint8_t*>(request->_tempObject) + index, data, len);
        });

#ifndef APP_LEGACY_OTA_FIT
    server_.on(
        "/api/motor/config",
        HTTP_POST,
        [this](AsyncWebServerRequest* request) {
            if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
                return;
            }
            if (!request->contentType().equalsIgnoreCase("application/json")) {
                request->send(415, "application/json", "{\"error\":\"expected application/json\"}");
                return;
            }
            if (request->contentLength() > kSettingsJsonMaxContentLength) {
                request->send(413, "application/json", "{\"error\":\"settings payload too large\"}");
                return;
            }
            if (request->_tempObject == nullptr) {
                request->send(400, "application/json", "{\"error\":\"invalid json\"}");
                return;
            }
            if (motorConfigSaver_ == nullptr) {
                request->send(503, "application/json", "{\"error\":\"motor config unavailable\"}");
                return;
            }

            const size_t bodyLength = request->contentLength();
            const size_t jsonCapacity = bodyLength == 0 ? 2048U : (bodyLength * 2U + 512U);
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
            DynamicJsonDocument document(jsonCapacity);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
            const DeserializationError parseError = deserializeJson(document, static_cast<const char*>(request->_tempObject));
            if (parseError || document.overflowed() || !document.is<JsonObject>()) {
                request->send(400, "application/json", "{\"error\":\"invalid json\"}");
                return;
            }

            String message;
            if (!motorConfigSaver_(document.as<JsonVariantConst>(), message)) {
                request->send(400, "application/json", String("{\"error\":\"") + message + "\"}");
                return;
            }
            JsonDocument response;
            response["ok"] = true;
            sendJson(request, response);
        },
        nullptr,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (total == 0 || total > kSettingsJsonMaxContentLength) {
                return;
            }

            if (index == 0 && request->_tempObject == nullptr) {
                request->_tempObject = calloc(total + 1, sizeof(uint8_t));
                if (request->_tempObject == nullptr) {
                    request->abort();
                    return;
                }
            }

            if (request->_tempObject == nullptr || index + len > total) {
                request->abort();
                return;
            }

            memcpy(static_cast<uint8_t*>(request->_tempObject) + index, data, len);
        });

    auto* playHandler = new AsyncCallbackJsonWebHandler(
        "/api/play",
        [this](AsyncWebServerRequest* request, JsonVariant& json) {
            if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
                return;
            }
            if (json.isNull()) {
                request->send(400, "application/json", "{\"error\":\"invalid json\"}");
                return;
            }
            const String url = String(static_cast<const char*>(json["url"] | ""));
            const String label = String(static_cast<const char*>(json["label"] | ""));
            const String type = String(static_cast<const char*>(json["type"] | "stream"));
            String message;
            if (!playHandler_(url, label, type, message)) {
                request->send(400, "application/json", String("{\"error\":\"") + message + "\"}");
                return;
            }
            request->send(200, "application/json", "{\"ok\":true}");
        });
    playHandler->setMethod(HTTP_POST);
    server_.addHandler(playHandler);

    server_.on(
        "/api/volume", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            (void)request;
        }, nullptr,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (index != 0 || len != total) {
                return;
            }
            if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
                return;
            }
            JsonDocument doc;
            if (deserializeJson(doc, data, len) != DeserializationError::Ok) {
                request->send(400, "application/json", "{\"error\":\"invalid json\"}");
                return;
            }
            const uint8_t volume = doc["volumePercent"] | doc["volume"] | 0;
            volumeHandler_(volume);
            request->send(200, "application/json", "{\"ok\":true}");
        });

    auto* equalizerHandler = new AsyncCallbackJsonWebHandler(
        "/api/equalizer",
        [this](AsyncWebServerRequest* request, JsonVariant& json) {
            if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
                return;
            }
            if (json.isNull() || !equalizerHandler_) {
                request->send(400, "application/json", "{\"error\":\"invalid equalizer request\"}");
                return;
            }
            JsonObjectConst object = json.as<JsonObjectConst>();
            String preset = object["preset"] | "custom";
            const int8_t lowDb = constrain(object["lowDb"] | 0, -6, 6);
            const int8_t presenceDb = constrain(object["presenceDb"] | 0, -6, 6);
            const int8_t highDb = constrain(object["highDb"] | 0, -6, 6);
            equalizerHandler_(preset, lowDb, presenceDb, highDb);
            request->send(200, "application/json", "{\"ok\":true}");
        });
    equalizerHandler->setMethod(HTTP_POST);
    server_.addHandler(equalizerHandler);

    auto* seekHandler = new AsyncCallbackJsonWebHandler(
        "/api/playback/seek",
        [this](AsyncWebServerRequest* request, JsonVariant& json) {
            if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
                return;
            }
            const uint32_t positionSeconds = json["positionSeconds"] | 0U;
            if (json.isNull() || !seekHandler_ || !seekHandler_(positionSeconds)) {
                request->send(400, "application/json", "{\"error\":\"Seeking is available only for a File Manager track that is currently playing.\"}");
                return;
            }
            request->send(200, "application/json", "{\"ok\":true}");
        });
    seekHandler->setMethod(HTTP_POST);
    server_.addHandler(seekHandler);

    server_.on("/api/stop", HTTP_POST, [this](AsyncWebServerRequest* request) {
        if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
            return;
        }
        stopHandler_();
        request->send(200, "application/json", "{\"ok\":true}");
    });

    auto* motorRunHandler = new AsyncCallbackJsonWebHandler(
        "/api/motor/run",
        [this](AsyncWebServerRequest* request, JsonVariant& json) {
            if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
                return;
            }
            if (json.isNull()) {
                request->send(400, "application/json", "{\"error\":\"invalid json\"}");
                return;
            }
            if (!motorRunHandler_) {
                request->send(503, "application/json", "{\"error\":\"motor control unavailable\"}");
                return;
            }

            const uint8_t channelIndex = json["channel"] | 0;
            const String directionValue = String(static_cast<const char*>(json["direction"] | "forward"));
            const bool forward = !directionValue.equalsIgnoreCase("backward");
            const uint32_t durationMs = json["durationMs"] | 0;
            const int8_t limitInputIndex = json["limitInputIndex"].isNull()
                ? static_cast<int8_t>(-1)
                : static_cast<int8_t>(json["limitInputIndex"].as<int>());

            String error;
            if (!motorRunHandler_(channelIndex, forward, durationMs, limitInputIndex, error)) {
                request->send(400, "application/json", String("{\"error\":\"") + error + "\"}");
                return;
            }

            request->send(200, "application/json", "{\"ok\":true}");
        });
    motorRunHandler->setMethod(HTTP_POST);
    server_.addHandler(motorRunHandler);

    server_.on("/api/display-trigger", HTTP_POST, [this](AsyncWebServerRequest* request) {
        if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
            return;
        }
        if (displayTriggerHandler_) {
            displayTriggerHandler_();
        }
        request->send(200, "application/json", "{\"ok\":true}");
    });

    server_.on(
        "/api/ota/check", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            (void)request;
        }, nullptr,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (index != 0 || len != total) {
                return;
            }
            if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
                return;
            }
            JsonDocument doc;
            bool apply = false;
            if (len > 0 && deserializeJson(doc, data, len) == DeserializationError::Ok) {
                apply = doc["apply"] | false;
            }
            if (!otaHandler_(apply)) {
                request->send(409, "application/json", "{\"error\":\"ota busy\"}");
                return;
            }
            JsonDocument response;
            response["ok"] = true;
            otaManager_->appendStatusJson(response["ota"].to<JsonObject>());
            sendJson(request, response);
        });

    server_.on(
        "/api/mqtt", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            (void)request;
        }, nullptr,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (index != 0 || len != total) {
                return;
            }
            if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
                return;
            }

            JsonDocument doc;
            if (len == 0 || deserializeJson(doc, data, len) != DeserializationError::Ok) {
                request->send(400, "application/json", "{\"error\":\"invalid json\"}");
                return;
            }

            const String action = String(static_cast<const char*>(doc["action"] | ""));
            String error;
            if (!mqttHandler_ || !mqttHandler_(action, error)) {
                request->send(400, "application/json", String("{\"error\":\"") + error + "\"}");
                return;
            }

            JsonDocument response;
            response["ok"] = true;
            response["action"] = action;
            response["message"] = action == "disconnect"
                ? "MQTT disconnect requested"
                : (action == "rediscover" ? "MQTT discovery republished" : "MQTT connect requested");
            sendJson(request, response);
        });

    server_.on("/api/usb-flasher/manifest", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
            return;
        }

        const esp_partition_t* runningPartition = esp_ota_get_running_partition();
        if (runningPartition == nullptr) {
            request->send(503, "application/json", "{\"error\":\"Running firmware partition is unavailable.\"}");
            return;
        }

        JsonDocument response;
        response["version"] = APP_VERSION;
        response["chipFamily"] = kCloneChipFamily;
        response["flashSize"] = ESP.getFlashChipSize();
        response["identityPolicy"] = "target-hardware-id";
        response["configurationIncluded"] = true;

        JsonArray parts = response["parts"].to<JsonArray>();
        JsonObject bootloader = parts.add<JsonObject>();
        bootloader["name"] = "bootloader";
        bootloader["sourceUrl"] = "/api/usb-flasher/part?name=bootloader";
        bootloader["address"] = kCloneBootloaderAddress;
        bootloader["size"] = kCloneBootloaderSize;

        JsonObject partitions = parts.add<JsonObject>();
        partitions["name"] = "partitions";
        partitions["sourceUrl"] = "/api/usb-flasher/part?name=partitions";
        partitions["address"] = kClonePartitionTableAddress;
        partitions["size"] = kClonePartitionTableSize;

        JsonObject application = parts.add<JsonObject>();
        application["name"] = "application";
        application["sourceUrl"] = "/api/usb-flasher/part?name=application";
        application["address"] = kCloneApplicationAddress;
        application["size"] = ESP.getSketchSize();

        JsonObject otaData = response["otaData"].to<JsonObject>();
        otaData["address"] = kCloneOtaDataAddress;
        otaData["size"] = kCloneOtaDataSize;
        otaData["action"] = "erase";
        sendJson(request, response);
    });

    server_.on("/api/usb-flasher/part", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
            return;
        }
        if (!request->hasParam("name")) {
            request->send(400, "application/json", "{\"error\":\"Missing firmware part name.\"}");
            return;
        }

        const String name = request->getParam("name")->value();
        if (name == "bootloader") {
            sendCloneFlashRegion(request, kCloneBootloaderAddress, kCloneBootloaderSize, "bootloader.bin");
            return;
        }
        if (name == "partitions") {
            sendCloneFlashRegion(request, kClonePartitionTableAddress, kClonePartitionTableSize, "partitions.bin");
            return;
        }
        if (name == "application") {
            const esp_partition_t* runningPartition = esp_ota_get_running_partition();
            if (runningPartition == nullptr) {
                request->send(503, "application/json", "{\"error\":\"Running firmware partition is unavailable.\"}");
                return;
            }
            sendCloneFlashRegion(
                request,
                runningPartition->address,
                ESP.getSketchSize(),
                String("esp32-notifier-clone-v") + APP_VERSION + ".bin");
            return;
        }

        request->send(404, "application/json", "{\"error\":\"Unknown firmware clone part.\"}");
    });

    server_.on(
        "/api/firmware/update", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            (void)request;
        }, nullptr,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (index != 0 || len != total) {
                return;
            }
            if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
                return;
            }
            JsonDocument doc;
            if (deserializeJson(doc, data, len) != DeserializationError::Ok) {
                request->send(400, "application/json", "{\"error\":\"invalid json\"}");
                return;
            }
            const String version = String(static_cast<const char*>(doc["version"] | ""));
            const String assetName = String(static_cast<const char*>(doc["assetName"] | ""));
            String error;
            if (!otaManager_->triggerInstallVersion(version, assetName, error)) {
                request->send(409, "application/json", String("{\"error\":\"") + error + "\"}");
                return;
            }

            JsonDocument response;
            response["ok"] = true;
            response["message"] = String("Update queued for ") + (assetName.isEmpty() ? version : assetName);
            sendJson(request, response);
        });

#endif

    auto* localUploadStartHandler = new AsyncCallbackJsonWebHandler(
        "/api/firmware/upload/start",
        [this](AsyncWebServerRequest* request, JsonVariant& json) {
            if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
                return;
            }
            const String sessionId = String(static_cast<const char*>(json["sessionId"] | ""));
            const String filename = String(static_cast<const char*>(json["filename"] | ""));
            const size_t totalSize = json["size"] | 0U;
            if (sessionId.isEmpty() || filename.isEmpty() || totalSize == 0) {
                request->send(400, "application/json", "{\"error\":\"Missing upload session, filename, or size.\"}");
                return;
            }
            String error;
            if (!otaManager_->beginLocalUpload(filename, totalSize, error, sessionId)) {
                request->send(409, "application/json", String("{\"error\":\"") + error + "\"}");
                return;
            }
            JsonDocument response;
            response["ok"] = true;
            otaManager_->appendLocalUploadStatus(response["upload"].to<JsonObject>());
            sendJson(request, response);
        });
    localUploadStartHandler->setMethod(HTTP_POST);
    server_.addHandler(localUploadStartHandler);

    server_.on("/api/firmware/upload/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
            return;
        }
        JsonDocument response;
        response["ok"] = true;
        otaManager_->appendLocalUploadStatus(response["upload"].to<JsonObject>());
        sendJson(request, response);
    });
    // The library also matches child paths of plain URI routes. Register the
    // upload status route before /api/firmware so resume gets its byte offset.
    registerFirmwareRoutes();

    server_.on(
        "/api/firmware/upload/chunk", HTTP_PUT,
        [](AsyncWebServerRequest* request) {
            (void)request;
        }, nullptr,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
                return;
            }
            auto rememberError = [request](const String& error) {
                if (request->_tempObject != nullptr) {
                    return;
                }
                const size_t length = error.length();
                char* stored = static_cast<char*>(calloc(length + 1, sizeof(char)));
                if (stored != nullptr) {
                    memcpy(stored, error.c_str(), length);
                    request->_tempObject = stored;
                }
            };

            if (!request->hasParam("sessionId") || !request->hasParam("offset")) {
                rememberError("Missing upload session or offset.");
            } else if (total == 0 || total > 32768U) {
                rememberError("Firmware upload chunk must contain 1 to 32768 bytes.");
            } else if (request->_tempObject == nullptr) {
                const String sessionId = request->getParam("sessionId")->value();
                const size_t baseOffset = static_cast<size_t>(strtoull(request->getParam("offset")->value().c_str(), nullptr, 10));
                String error;
                if (!otaManager_->writeLocalUploadChunkAt(sessionId, baseOffset + index, data, len, error)) {
                    rememberError(error);
                }
            }

            if (index + len != total) {
                return;
            }
            if (request->_tempObject != nullptr) {
                request->send(
                    409,
                    "application/json",
                    String("{\"error\":\"") + static_cast<const char*>(request->_tempObject) + "\"}");
                return;
            }
            JsonDocument response;
            response["ok"] = true;
            otaManager_->appendLocalUploadStatus(response["upload"].to<JsonObject>());
            sendJson(request, response);
        });

    auto* localUploadFinishHandler = new AsyncCallbackJsonWebHandler(
        "/api/firmware/upload/finish",
        [this](AsyncWebServerRequest* request, JsonVariant& json) {
            if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
                return;
            }
            const String sessionId = String(static_cast<const char*>(json["sessionId"] | ""));
            String error;
            if (sessionId.isEmpty() || !otaManager_->finishLocalUpload(error, sessionId)) {
                request->send(409, "application/json", String("{\"error\":\"") + (error.isEmpty() ? "Missing upload session." : error) + "\"}");
                return;
            }
            request->send(200, "application/json", "{\"ok\":true,\"message\":\"Local firmware uploaded. Restarting...\"}");
        });
    localUploadFinishHandler->setMethod(HTTP_POST);
    server_.addHandler(localUploadFinishHandler);

    auto* localUploadCancelHandler = new AsyncCallbackJsonWebHandler(
        "/api/firmware/upload/cancel",
        [this](AsyncWebServerRequest* request, JsonVariant& json) {
            if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
                return;
            }
            const String sessionId = String(static_cast<const char*>(json["sessionId"] | ""));
            String error;
            if (!otaManager_->cancelLocalUpload(sessionId, error)) {
                request->send(409, "application/json", String("{\"error\":\"") + error + "\"}");
                return;
            }
            request->send(200, "application/json", "{\"ok\":true,\"message\":\"Local firmware upload cancelled.\"}");
        });
    localUploadCancelHandler->setMethod(HTTP_POST);
    server_.addHandler(localUploadCancelHandler);

#ifndef APP_LEGACY_OTA_FIT
    server_.on(
        "/api/firmware/upload", HTTP_POST,
        [this](AsyncWebServerRequest* request) {
            if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
                return;
            }
            JsonDocument response;
            JsonObject ota = response["ota"].to<JsonObject>();
            otaManager_->appendStatusJson(ota);
            const String selectedVersion = String(static_cast<const char*>(ota["selectedVersion"] | ""));
            const String message = String(static_cast<const char*>(ota["message"] | ""));
            if (selectedVersion == "local" && message.startsWith("Local firmware uploaded")) {
                response["ok"] = true;
                response["message"] = message;
                sendJson(request, response, 200);
                return;
            }
            response["error"] = message.isEmpty() ? "Firmware upload did not complete." : message;
            sendJson(request, response, 400);
        },
        [this](AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool final) {
            if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
                return;
            }
            String error;
            if (index == 0) {
                if (!otaManager_->beginLocalUpload(filename, request->contentLength(), error)) {
                    return;
                }
            }
            if (len > 0 && !otaManager_->writeLocalUploadChunk(data, len, error)) {
                return;
            }
            if (final && !otaManager_->finishLocalUpload(error)) {
                return;
            }
        });

    server_.on("/api/storage/file", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
            return;
        }
        const StorageTarget target = storageTargetFromRequest(request);
        if (!storageMounted(target)) {
            request->send(503, "application/json", String("{\"error\":\"") + storageUnavailableMessage(target) + "\"}");
            return;
        }
        if (!request->hasParam("path")) {
            request->send(400, "application/json", "{\"error\":\"Missing path.\"}");
            return;
        }

        const String path = storagePathFromRequest(request->getParam("path")->value());
        if (path.isEmpty() || !storageExists(target, path)) {
            request->send(404, "application/json", "{\"error\":\"File not found.\"}");
            return;
        }

        const bool download = request->hasParam("download") && request->getParam("download")->value() == "1";
        File file = storageOpen(target, path, "r");
        if (!file || file.isDirectory()) {
            request->send(404, "application/json", "{\"error\":\"File not found.\"}");
            return;
        }
        if (!ensureStorageTransferBuffer(4096)) {
            file.close();
            request->send(500, "application/json", "{\"error\":\"Unable to allocate transfer buffer.\"}");
            return;
        }

        beginStorageRead(target);
        AsyncWebServerResponse* response = request->beginChunkedResponse(
            contentTypeForPath(path),
            [this, target, file = std::move(file), released = false](uint8_t* buffer, size_t maxLen, size_t) mutable -> size_t {
                if (!file || maxLen == 0) {
                    if (file) {
                        file.close();
                    }
                    if (!released) {
                        endStorageRead(target);
                        released = true;
                    }
                    return 0;
                }

                const size_t chunkSize = min(maxLen, storageTransferBufferCapacity_);
                const size_t bytesRead = file.read(storageTransferBuffer_, chunkSize);
                if (bytesRead > 0) {
                    memcpy(buffer, storageTransferBuffer_, bytesRead);
                    return bytesRead;
                }

                file.close();
                if (!released) {
                    endStorageRead(target);
                    released = true;
                }
                return 0;
            });
        if (download) {
            response->addHeader("Content-Disposition", String("attachment; filename=\"") + storageDownloadName(path) + "\"");
        }
        response->addHeader("Cache-Control", "no-store");
        request->send(response);
    });

    server_.on("/api/storage/reindex", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
            return;
        }

        JsonDocument response;
        appendStorageReindexStatusJson(response["reindex"].to<JsonObject>(), snapshotStorageReindexStatus());
        sendJson(request, response);
    });

    server_.on(
        "/api/storage/reindex", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            (void)request;
        }, nullptr,
        [this](AsyncWebServerRequest* request, uint8_t*, size_t, size_t, size_t) {
            if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
                return;
            }

            const StorageTarget target = storageTargetFromRequest(request);
            const String directoryPath = storageDirectoryFromRequest(request);
            String error;
            if (!beginStorageReindexJob(target, directoryPath, error)) {
                request->send(409, "application/json", String("{\"error\":\"") + error + "\"}");
                return;
            }

            JsonDocument response;
            response["ok"] = true;
            appendStorageReindexStatusJson(response["reindex"].to<JsonObject>(), snapshotStorageReindexStatus());
            sendJson(request, response, 202);
        });

    server_.on(
        "/api/storage/remount", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            (void)request;
        }, nullptr,
        [this](AsyncWebServerRequest* request, uint8_t*, size_t, size_t, size_t) {
            if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
                return;
            }

            const StorageTarget target = storageTargetFromRequest(request);
            if (target != StorageTarget::Sd) {
                request->send(400, "application/json", "{\"error\":\"Manual remount is only supported for the SD card.\"}");
                return;
            }

            if (storageBusy(target)) {
                request->send(423, "application/json", "{\"error\":\"SD card is busy. Stop playback or transfers before remounting.\"}");
                return;
            }

            const String directoryPath = storageDirectoryFromRequest(request);
            const bool mounted = remountStorageBackend(target, settingsGetter_());

            JsonDocument response;
            response["ok"] = mounted;
            response["message"] = mounted ? "SD card remounted. Reloading files..." : "SD card remount failed.";
            appendStorageDirectoryJson(target, directoryPath, response);
            sendJson(request, response, mounted ? 200 : 503);
        });

    server_.on("/api/storage/count", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
            return;
        }

        const StorageTarget target = storageTargetFromRequest(request);
        const String directoryPath = storageDirectoryFromRequest(request);
        JsonDocument response;
        response["target"] = storageTargetId(target);
        response["currentPath"] = directoryPath;
        response["totalEntries"] = countStorageDirectoryEntries(target, directoryPath);
        sendJson(request, response);
    });

    server_.on("/api/storage", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
            return;
        }

        const StorageTarget target = storageTargetFromRequest(request);
        JsonDocument response;
        appendStorageDirectoryJson(target, storageDirectoryFromRequest(request), response, request);
        sendJson(request, response);
    });

    server_.on(
        "/api/storage/delete", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            (void)request;
        }, nullptr,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (index != 0 || len != total) {
                return;
            }
            if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
                return;
            }
            const StorageTarget target = storageTargetFromRequest(request);
            const String directoryPath = storageDirectoryFromRequest(request);
            if (!storageMounted(target)) {
                request->send(503, "application/json", String("{\"error\":\"") + storageUnavailableMessage(target) + "\"}");
                return;
            }

            JsonDocument doc;
            if (deserializeJson(doc, data, len) != DeserializationError::Ok) {
                request->send(400, "application/json", "{\"error\":\"invalid json\"}");
                return;
            }

            const String path = storagePathFromRequest(String(static_cast<const char*>(doc["path"] | "")));
            if (path.isEmpty() || !storageExists(target, path)) {
                request->send(404, "application/json", "{\"error\":\"File not found.\"}");
                return;
            }
            if (!removeStoragePathRecursive(target, path)) {
                request->send(500, "application/json", "{\"error\":\"Unable to delete path.\"}");
                return;
            }
            rebuildStorageDirectoryIndex(target, storageParentPath(path));

            JsonDocument response;
            response["ok"] = true;
            response["message"] = String("Deleted ") + path;
            appendStorageDirectoryJson(target, directoryPath, response);
            sendJson(request, response);
        });

    server_.on(
        "/api/storage/mkdir", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            (void)request;
        }, nullptr,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (index != 0 || len != total) {
                return;
            }
            if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
                return;
            }

            const StorageTarget target = storageTargetFromRequest(request);
            const String directoryPath = storageDirectoryFromRequest(request);
            if (!storageMounted(target)) {
                request->send(503, "application/json", String("{\"error\":\"") + storageUnavailableMessage(target) + "\"}");
                return;
            }

            JsonDocument doc;
            if (deserializeJson(doc, data, len) != DeserializationError::Ok) {
                request->send(400, "application/json", "{\"error\":\"invalid json\"}");
                return;
            }

            const String name = sanitizeUploadFilename(String(static_cast<const char*>(doc["name"] | "")));
            const String path = joinStoragePath(directoryPath, name);
            if (path.isEmpty()) {
                request->send(400, "application/json", "{\"error\":\"Folder name is required.\"}");
                return;
            }
            if (storageExists(target, path)) {
                request->send(409, "application/json", "{\"error\":\"A file or folder with that name already exists.\"}");
                return;
            }
            if (!createStorageDirectory(target, path)) {
                request->send(500, "application/json", "{\"error\":\"Unable to create folder.\"}");
                return;
            }
            rebuildStorageDirectoryIndex(target, directoryPath);

            JsonDocument response;
            response["ok"] = true;
            response["message"] = String("Created folder ") + path;
            appendStorageDirectoryJson(target, directoryPath, response);
            sendJson(request, response);
        });

    server_.on(
        "/api/storage/upload", HTTP_POST,
        [this](AsyncWebServerRequest* request) {
            if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
                return;
            }

            JsonDocument response;
            if (!storageUploadError_.isEmpty()) {
                response["error"] = storageUploadError_;
                if (storageUploadFile_) {
                    storageUploadFile_.close();
                }
                if (!storageUploadPath_.isEmpty()) {
                    storageRemove(storageUploadTarget_, storageUploadPath_);
                }
                storageUploadPath_ = "";
                storageUploadBytesWritten_ = 0;
                storageUploadLimitBytes_ = 0;
                storageUploadError_ = "";
                sendJson(request, response, 400);
                return;
            }

            response["ok"] = true;
            response["message"] = String("Uploaded ") + storageUploadPath_;
            response["path"] = storageUploadPath_;
            appendStorageDirectoryJson(storageUploadTarget_, storageParentPath(storageUploadPath_), response);
            storageUploadPath_ = "";
            storageUploadBytesWritten_ = 0;
            storageUploadLimitBytes_ = 0;
            storageUploadError_ = "";
            sendJson(request, response);
        },
        [this](AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool final) {
            if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
                return;
            }

            if (index == 0) {
                storageUploadTarget_ = storageTargetFromRequest(request);
                const String directoryPath = storageDirectoryFromRequest(request);
                storageUploadError_ = "";
                storageUploadPath_ = joinStoragePath(directoryPath, sanitizeUploadFilename(filename));
                storageUploadBytesWritten_ = 0;
                storageUploadLimitBytes_ = 0;

                if (!storageMounted(storageUploadTarget_)) {
                    storageUploadError_ = storageUnavailableMessage(storageUploadTarget_);
                    return;
                }
                if (storageUploadPath_.isEmpty() || !isAllowedStoragePath(storageUploadPath_)) {
                    storageUploadError_ = "Only supported audio or artwork files (.mp3, .wav, .aac, .m4a, .ogg, .opus, .flac, .jpg, .jpeg, .png, .webp) are allowed.";
                    return;
                }

                const size_t existingSize = storageExists(storageUploadTarget_, storageUploadPath_)
                    ? static_cast<size_t>(storageOpen(storageUploadTarget_, storageUploadPath_, "r").size())
                    : 0;
                const StorageBackendSummary summary = getStorageSummary(storageUploadTarget_);
                const size_t freeBytes = summary.freeBytes;
                const size_t availableBytes = freeBytes + existingSize;
                storageUploadLimitBytes_ = availableBytes > kStorageReserveBytes ? availableBytes - kStorageReserveBytes : 0;
                if (storageUploadLimitBytes_ == 0) {
                    storageUploadError_ = "Not enough free space for upload.";
                    return;
                }

                storageUploadFile_ = storageOpen(storageUploadTarget_, storageUploadPath_, "w");
                if (!storageUploadFile_) {
                    storageUploadError_ = "Unable to open destination file.";
                    return;
                }

                beginStorageWrite(storageUploadTarget_);
            }

            if (!storageUploadError_.isEmpty()) {
                return;
            }
            if (len > 0) {
                if ((storageUploadBytesWritten_ + len) > storageUploadLimitBytes_) {
                    storageUploadError_ = "File is larger than remaining filesystem space.";
                    storageUploadFile_.close();
                    endStorageWrite(storageUploadTarget_);
                    storageRemove(storageUploadTarget_, storageUploadPath_);
                    return;
                }
                if (!ensureStorageTransferBuffer(len)) {
                    storageUploadError_ = psramFound() ? "Unable to allocate PSRAM-backed transfer buffer." : "Unable to allocate transfer buffer.";
                    storageUploadFile_.close();
                    endStorageWrite(storageUploadTarget_);
                    storageRemove(storageUploadTarget_, storageUploadPath_);
                    return;
                }
                memcpy(storageTransferBuffer_, data, len);
                const size_t written = storageUploadFile_.write(storageTransferBuffer_, len);
                if (written != len) {
                    storageUploadError_ = "Failed while writing uploaded file.";
                    storageUploadFile_.close();
                    endStorageWrite(storageUploadTarget_);
                    storageRemove(storageUploadTarget_, storageUploadPath_);
                    return;
                }
                storageUploadBytesWritten_ += written;
            }
            if (final && storageUploadFile_) {
                storageUploadFile_.flush();
                storageUploadFile_.close();
                endStorageWrite(storageUploadTarget_);
                rebuildStorageDirectoryIndex(storageUploadTarget_, storageParentPath(storageUploadPath_));
            }
        });

    server_.on("/api/reboot", HTTP_POST, [this](AsyncWebServerRequest* request) {
        if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
            return;
        }
        rebootHandler_();
        request->send(200, "application/json", "{\"ok\":true}");
    });

    server_.on("/api/server-shutdown", HTTP_POST, [this](AsyncWebServerRequest* request) {
        if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
            return;
        }
        if (serverShutdownHandler_) {
            serverShutdownHandler_();
        }
        request->send(200, "application/json", "{\"ok\":true}");
    });

    server_.on("/api/factory-reset", HTTP_POST, [this](AsyncWebServerRequest* request) {
        if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
            return;
        }
        factoryResetHandler_();
        request->send(200, "application/json", "{\"ok\":true}");
    });
#endif
}

void WebServerManager::registerWebRoutes() {
#ifdef APP_LEGACY_OTA_FIT
    static const char kLegacyPage[] =
        "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>ELMA Legacy OTA</title><style>body{font:16px system-ui;max-width:680px;margin:40px auto;padding:20px}"
        "a{color:#b65f00}</style><h1>ELMA IoT</h1><p>This legacy-partition build keeps audio, MQTT, GPIO, display, "
        "controls and OTA services. Use ELMA Flasher on the same LAN for configuration and firmware updates.</p>"
        "<p><a href='/api/status'>Device status (JSON)</a></p>";
    auto serveLegacyPage = [this](AsyncWebServerRequest* request) {
        if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) return;
        AsyncWebServerResponse* response = request->beginResponse(200, "text/html; charset=utf-8", kLegacyPage);
        response->addHeader("Cache-Control", "no-store");
        request->send(response);
    };
    server_.on("/", HTTP_GET, serveLegacyPage);
    server_.on("/index.html", HTTP_GET, serveLegacyPage);
    server_.on("/generate_204", HTTP_GET, [this](AsyncWebServerRequest* request) { request->redirect("/"); });
    server_.on("/hotspot-detect.html", HTTP_GET, [this](AsyncWebServerRequest* request) { request->redirect("/"); });
    server_.on("/connecttest.txt", HTTP_GET, [this](AsyncWebServerRequest* request) { request->redirect("/"); });
    server_.on("/ncsi.txt", HTTP_GET, [this](AsyncWebServerRequest* request) { request->redirect("/"); });
    server_.onNotFound([](AsyncWebServerRequest* request) { request->send(404, "text/plain", "Not found"); });
#else
    // This intentionally bypasses web authentication and captive-portal redirects.
    // The setup page loads it from the newly assigned station address to determine
    // when the browser has rejoined the home network. It exposes no device data.
    server_.on("/wifi-handoff.svg", HTTP_GET, [](AsyncWebServerRequest* request) {
        static const char kHandoffProbeSvg[] =
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1\" height=\"1\" viewBox=\"0 0 1 1\">"
            "<path fill=\"#f59e0b\" d=\"M0 0h1v1H0z\"/></svg>";
        AsyncWebServerResponse* response = request->beginResponse(200, "image/svg+xml", kHandoffProbeSvg);
        response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
        response->addHeader("Access-Control-Allow-Origin", "*");
        request->send(response);
    });

    auto serveAsset = [this](AsyncWebServerRequest* request, const String& path) {
        if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
            return;
        }
        const EmbeddedWebAsset* asset = findAsset(path);
        if (asset == nullptr) {
            request->send(404, "text/plain", "Not found");
            return;
        }
        const bool isHtmlShell = path == "/index.html";
        const String tag = assetEtag(*asset);
        if (!isHtmlShell && requestMatchesEtag(request, tag)) {
            AsyncWebServerResponse* notModified = request->beginResponse(304);
            notModified->addHeader("ETag", tag);
            notModified->addHeader("Cache-Control", "public, max-age=0, must-revalidate");
            if (asset->gzip) {
                notModified->addHeader("Vary", "Accept-Encoding");
            }
            request->send(notModified);
            return;
        }
        AsyncWebServerResponse* response = new (std::nothrow) BoundedBufferResponse(
            reinterpret_cast<const char*>(asset->data), asset->size, asset->contentType, 200, true);
        if (!response) {
            request->send(503, "text/plain", "Device busy; retry shortly.");
            return;
        }
        response->addHeader("ETag", tag);
        if (asset->gzip) {
            response->addHeader("Content-Encoding", "gzip");
            response->addHeader("Vary", "Accept-Encoding");
        }
        if (isHtmlShell) {
            response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
            response->addHeader("Pragma", "no-cache");
            response->addHeader("Expires", "0");
        } else {
            response->addHeader("Cache-Control", "public, max-age=0, must-revalidate");
        }
        request->send(response);
    };

    server_.on("/", HTTP_GET, [serveAsset](AsyncWebServerRequest* request) { serveAsset(request, "/index.html"); });
    server_.on("/index.html", HTTP_GET, [serveAsset](AsyncWebServerRequest* request) { serveAsset(request, "/index.html"); });
    server_.on("/style.css", HTTP_GET, [serveAsset](AsyncWebServerRequest* request) { serveAsset(request, "/style.css"); });
    server_.on("/app.js", HTTP_GET, [serveAsset](AsyncWebServerRequest* request) { serveAsset(request, "/app.js"); });
    server_.on("/generate_204", HTTP_GET, [this](AsyncWebServerRequest* request) { request->redirect("/"); });
    server_.on("/hotspot-detect.html", HTTP_GET, [this](AsyncWebServerRequest* request) { request->redirect("/"); });
    server_.on("/connecttest.txt", HTTP_GET, [this](AsyncWebServerRequest* request) { request->redirect("/"); });
    server_.on("/ncsi.txt", HTTP_GET, [this](AsyncWebServerRequest* request) { request->redirect("/"); });

    server_.onNotFound([this, serveAsset](AsyncWebServerRequest* request) {
        if (redirectCaptivePortalIfNeeded(request)) {
            return;
        }
        const EmbeddedWebAsset* asset = findAsset(request->url());
        if (asset != nullptr) {
            serveAsset(request, request->url());
            return;
        }
        request->send(404, "text/plain", "Not found");
    });
#endif
}

#endif
