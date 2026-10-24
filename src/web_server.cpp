#include "sunton_cpu.h"
#include "robot_service.h"
#include "motion_capture.h"
#include "camera_service.h"
#include "plot_telemetry.h"
#include "plot_storage.h"
#include "storage_download.h"
#include "storage_integrity.h"
#include <esp_system.h>
#include "static_asset_policy.h"
#include "bounded_stream_response.h"
#include "storage_memory.h"
#include <sys/time.h>
#include "web_server.h"
#include "web_access.h"
#include "bno055_service.h"
#include "rs485_service.h"
#include "microphone_service.h"
#include "spk_media.h"
#include "device_log.h"

#ifdef APP_DISABLE_WEB_UI

WebServerManager::WebServerManager() = default;

void WebServerManager::securityTick() {}

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
#include "segmented_json_response.h"
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
    // Firmware rebuilds can change CSS while retaining version and byte length.
    // Include content so a browser cannot revalidate an obsolete stylesheet.
    uint32_t contentHash = 2166136261u;
    for (size_t i = 0; i < asset.size; ++i) {
        contentHash = (contentHash ^ asset.data[i]) * 16777619u;
    }
    tag += ':';
    tag += String(contentHash, HEX);
    tag += asset.gzip ? ":gz" : ":raw";
    tag += '"';
    return tag;
}

bool requestMatchesEtag(AsyncWebServerRequest* request, const String& tag) {
    if (request == nullptr || !request->hasHeader("If-None-Match")) {
        return false;
    }
    const AsyncWebHeader* header = request->getHeader("If-None-Match");
    return header != nullptr && StaticAssetPolicy::etagMatches(header->value().c_str(), tag.c_str());
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
#if APP_SUNTON_PANEL
    return StorageTarget::Sd;
#else
    return StorageTarget::Flash;
#endif
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
    if (path.indexOf("..") >= 0 || path.indexOf(".elma-settings-") >= 0) {
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
    String lower = path;lower.toLowerCase();
    if (lower.startsWith("/3d/") && lower.endsWith(".glb")) return true;
    if (lower.startsWith("/web/")) {
        if (lower.endsWith(".gz")) lower.remove(lower.length()-3);
        if (lower.endsWith(".html") || lower.endsWith(".htm") || lower.endsWith(".css") ||
            lower.endsWith(".js") || lower.endsWith(".mjs") || lower.endsWith(".json") ||
            lower.endsWith(".map") || lower.endsWith(".svg") || lower.endsWith(".txt") ||
            lower.endsWith(".xml") || lower.endsWith(".wasm") || lower.endsWith(".woff") ||
            lower.endsWith(".woff2") || lower.endsWith(".ico")) return true;
    }
    return lower.endsWith(".mp3") || lower.endsWith(".wav") || lower.endsWith(".aac") || lower.endsWith(".m4a") ||
        lower.endsWith(".ogg") || lower.endsWith(".opus") || lower.endsWith(".flac") ||
        lower.endsWith(".jpg") || lower.endsWith(".jpeg") || lower.endsWith(".png") || lower.endsWith(".webp");
}

const char* contentTypeForPath(const String& path) {
    String lower = path;lower.toLowerCase();
    if (lower.endsWith(".html") || lower.endsWith(".htm")) return "text/html; charset=utf-8";
    if (lower.endsWith(".css")) return "text/css; charset=utf-8";
    if (lower.endsWith(".js") || lower.endsWith(".mjs")) return "application/javascript; charset=utf-8";
    if (lower.endsWith(".json") || lower.endsWith(".map")) return "application/json";
    if (lower.endsWith(".svg")) return "image/svg+xml";
    if (lower.endsWith(".xml")) return "application/xml";
    if (lower.endsWith(".txt") || lower.endsWith(".csv")) return "text/plain; charset=utf-8";
    if (lower.endsWith(".wasm")) return "application/wasm";
    if (lower.endsWith(".woff")) return "font/woff";
    if (lower.endsWith(".woff2")) return "font/woff2";
    if (lower.endsWith(".ttf")) return "font/ttf";
    if (lower.endsWith(".otf")) return "font/otf";
    if (lower.endsWith(".ico")) return "image/x-icon";
    if (lower.endsWith(".gif")) return "image/gif";
    if (lower.endsWith(".avif")) return "image/avif";
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

String readStorageIndexLine(File& file) {
    // A corrupt index must never allocate a String the size of the card.
    char line[769];size_t size=0;bool tooLong=false;uint32_t started=millis();
    while(file.available()) {
        int c=file.read();if(c<0)break;
        if(c=='\n'){if(tooLong)return String();break;}
        if(size<sizeof(line)-1)line[size++]=char(c);else tooLong=true;
        if(uint32_t(millis()-started)>=25)return String();
    }
    if(tooLong)return String();line[size]=0;return String(line);
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
        const String entryPath = parseIndexedStorageEntryPath(directoryPath, readStorageIndexLine(indexFile), isDirectoryHint);
        if (entryPath.isEmpty()) {
            continue;
        }
        totalEntries += 1;
        if (scannedEntries++ < offset) {
            continue;
        }
        if (limit > 0 && returnedEntries >= limit) {
            hasMore = true;
            break;
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
    totalEntries=max(totalEntries,nextOffset+(hasMore?1U:0U));
    return true;
}

void appendStorageEntriesJson(StorageTarget target, const String& directoryPath, JsonArray files, size_t offset, size_t limit, size_t& nextOffset, bool& hasMore, size_t& totalEntries, bool exhaustiveScan = false) {
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
        const String entryName = storageBaseName(String(entry.path()));
        if (entryName == ".index" || entryName == ".index.tmp" || entryName.startsWith(".elma-settings-")) {
            entry.close();
            continue;
        }
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
        if (name == ".index" || name == ".index.tmp" || name.startsWith(".elma-settings-")) {
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
        if (name == ".index" || name == ".index.tmp" || name.startsWith(".elma-settings-")) {
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
restartIndex:
    while(storageForegroundPending(target)||storageBusy(target))vTaskDelay(pdMS_TO_TICKS(100));
    bool preempted = false;
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
                    if(storageForegroundPending(target)){entry.close();preempted=true;break;}
                    const String name = storageBaseName(String(entry.path()));
                    if (name == ".index" || name == ".index.tmp" || name.startsWith(".elma-settings-")) {
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
                if(preempted){updateStorageReindexStatus([](StorageReindexStatus& status){copyReindexText(status.stage,sizeof(status.stage),"waiting for file operation");});goto restartIndex;}

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
                        bool writeOk = !preempted;
                        for (File entry = root.openNextFile(); entry; entry = root.openNextFile()) {
                    if(storageForegroundPending(target)){entry.close();preempted=true;break;}
                            String name = storageBaseName(String(entry.path()));
                            if (name == ".index" || name == ".index.tmp" || name.startsWith(".elma-settings-")) {
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

                        if (!writeOk || preempted) {
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

    if(preempted){
        updateStorageReindexStatus([](StorageReindexStatus& status){copyReindexText(status.stage,sizeof(status.stage),"waiting for file operation");});
        goto restartIndex;
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

bool removeStoragePathRecursive(StorageTarget target, const String& path,unsigned depth=0) {
    if(depth>8)return false;
    beginStorageWrite(target);
    struct Lease{StorageTarget target;~Lease(){endStorageWrite(target);}} lease{target};
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
        if (!removeStoragePathRecursive(target, childPath,depth+1)) {
            entry.close();
            return false;
        }
    }
    entry.close();
    return path != "/" && fs->rmdir(path);
}

bool createStorageDirectory(StorageTarget target, const String& path) {
    beginStorageWrite(target);
    struct Lease{StorageTarget target;~Lease(){endStorageWrite(target);}} lease{target};
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
    const size_t limit = min<size_t>(128,max<size_t>(1,requestSizeParam(request, "limit", kStorageDirectoryListBatchDefault)));
    const bool preferLiveScan = requestFlagParam(request, "live");
    const bool rebuildIndex = requestFlagParam(request, "reindex");
    JsonObject storage = response["storage"].to<JsonObject>();
    appendStorageSummaryJson(target, storage);
    if (rebuildIndex) {
        String error;response["reindexPending"]=beginStorageReindexJob(target,currentPath,error);
        if(!error.isEmpty())response["reindexError"]=error;
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
void WebServerManager::releaseStorageUploadLease(){
    if(storageUploadLeased_){endStorageWrite(storageUploadTarget_);storageUploadLeased_=false;}
#if APP_SUNTON_PANEL == 3
    // Foreground transfers own this buffer; do not keep it after completion,
    // cancellation or an error while music and the LCD need internal SRAM.
    if(storageTransferBuffer_){heap_caps_free(storageTransferBuffer_);storageTransferBuffer_=nullptr;storageTransferBufferCapacity_=0;}
#endif
}

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

    void* memory = allocateStorageBuffer(minimumSize);
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

    security_.begin();
    DebugLog.printf("[web] registering routes heap=%u largest=%u\n",ESP.getFreeHeap(),ESP.getMaxAllocHeap());
    // The route-level checks also cover streaming upload callbacks, which run
    // before middleware. Middleware protects future API handlers by default.
    server_.addMiddleware([this](AsyncWebServerRequest* request,ArMiddlewareNext next){
#if APP_SUNTON_PANEL == 3
        suntonWebBoost();
        if(gatePresentation(request))return;
        // Reserve the remaining heap for a small retry response rather than
        // constructing a large JSON payload or starting another asset stream.
        const size_t requestReserve=request->contentLength()<1024?4096:12288;
        if(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<requestReserve ||
           heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<768){
            // Let the response lifecycle close the socket after receiving
            // finishes; closing inside onData can invalidate AsyncTCP's PCB.
            request->send(503,"application/json","{\"error\":\"Memory busy; retry shortly.\"}");return;
        }
#endif
        if(rejectIfWebUiLocked(request))return;
        next();
    });
#if defined(APP_SPK_BOARD) && !defined(APP_DISABLE_AUDIO) && !defined(APP_DISABLE_WEB_UI)
    MotionCapture::routes(server_,[this](AsyncWebServerRequest* request){return !redirectCaptivePortalIfNeeded(request)&&ensureAuthorized(request);});
#endif
#if APP_SUNTON_PANEL == 3
    registerPresentationRoutes();
#endif
    registerSecurityRoutes();
    registerApiRoutes();
    registerWebRoutes();
    server_.begin();
    listenerStartPending_=server_.state()!=LISTEN;listenerRetryAt_=millis();
    DebugLog.printf("[web] %s heap=%u largest=%u\n",server_.state()==LISTEN?"ready":"listener failed to start",ESP.getFreeHeap(),ESP.getMaxAllocHeap());
}

bool WebServerManager::ensureAuthorized(AsyncWebServerRequest* request) {
#if APP_SUNTON_PANEL == 3
    suntonWebBoost();
    // Upload/body callbacks run before server middleware. Gate them before
    // parsing settings or opening files while LCD widgets own the memory.
    if(request->url().startsWith("/api/") && gatePresentation(request))return false;
#endif
#if APP_SUNTON_PANEL
    if(request->url().startsWith("/api/storage") && storageTargetFromRequest(request)==StorageTarget::Flash){
        request->send(404,"text/plain","Internal file browsing is unavailable on this board; use SD storage.");return false;
    }
#endif
    if (rejectIfWebUiLocked(request)) {
        return false;
    }
    bool ready=false,allowed=false;
    auto authenticate=[&](const SettingsBundle& settings){
        ready=true;
        allowed=!settings.webAuth.enabled || request->authenticate(settings.webAuth.username.c_str(),settings.webAuth.password.c_str());
    };
    if(settingsReader_)settingsReader_(authenticate);else authenticate(settingsGetter_());
    if(!ready){request->send(503,"text/plain","Configuration busy; retry shortly");return false;}
    if(allowed)return true;
    request->requestAuthentication();
    return false;
}

bool WebServerManager::rejectIfWebUiLocked(AsyncWebServerRequest* request) {
    const String path=request->url();
    // Static assets must load the lock screen; only sanitized status and the
    // security protocol remain accessible through the API while locked.
    if(!path.startsWith("/api/") || path=="/api/security" || ((path=="/api/status" || path=="/api/presentation")&&request->method()==HTTP_GET))return false;
    if(!security_.locked())return false;
    JsonDocument doc;doc["error"]="Interface locked";doc["locked"]=true;sendJson(request,doc,423);return true;
}
void WebServerManager::securityTick(){
    security_.tick();
#if APP_SUNTON_PANEL == 3
    if(clockTestResumeAt_ && int32_t(millis()-clockTestResumeAt_)>=0){
        clockTestResumeAt_=0;server_.begin();
        listenerStartPending_=server_.state()!=LISTEN;listenerRetryAt_=millis();
        DebugLog.println("[power-test] HTTP resumed after quiet interval");
    }
#endif
    static uint32_t lastWebAccessTick=0;
    if(settingsGetter_ && millis()-lastWebAccessTick>=1000){lastWebAccessTick=millis();
        if(settingsReader_)settingsReader_([&](const SettingsBundle& settings){WebAccess::tick(settings,otaManager_ && otaManager_->isBusy());});
        else WebAccess::tick(settingsGetter_(),otaManager_ && otaManager_->isBusy());}
    if(listenerStartPending_&&millis()-listenerRetryAt_>=5000&&ESP.getFreeHeap()>24000){
        listenerRetryAt_=millis();server_.begin();listenerStartPending_=server_.state()!=LISTEN;
        if(!listenerStartPending_)DebugLog.println("[web] listener recovered");
    }
}
void WebServerManager::setWebUiLocked(bool locked){security_.externalLock(locked);}
bool WebServerManager::webUiLocked() const {return security_.locked();}
#if APP_SUNTON_PANEL == 3
void WebServerManager::pauseHttpForClockTest(){
    // USB-only, bounded and volatile. Wi-Fi, touch and Logics stay running.
    // Existing responses may finish; browser polling cannot renew the lease.
    server_.end();listenerStartPending_=false;clockTestResumeAt_=millis()+30000;
    DebugLog.println("[power-test] HTTP paused for 30 seconds to measure idle clock; auto-resume armed");
}
#endif

bool WebServerManager::redirectCaptivePortalIfNeeded(AsyncWebServerRequest* request) {
    if(settingsGetter_){
        String host=request->host();int port=host.indexOf(':');if(port>=0)host=host.substring(0,port);host.toLowerCase();
        String hostname;
        if(settingsReader_)settingsReader_([&](const SettingsBundle& settings){hostname=WebAccess::hostname(settings)+".local";});
        else hostname=WebAccess::hostname(settingsGetter_())+".local";
        if(host==hostname)return false;
    }
    String captiveHost=request->host();int portSeparator=captiveHost.indexOf(':');if(portSeparator>=0)captiveHost=captiveHost.substring(0,portSeparator);
    if (wifiManager_ != nullptr && wifiManager_->shouldRedirectCaptivePortal(captiveHost)) {
        request->redirect("http://192.168.4.1/");
        return true;
    }
    return false;
}

void WebServerManager::sendJson(AsyncWebServerRequest* request, const JsonDocument& doc, int statusCode) {
    const size_t length = measureJson(doc);
    if(!psramFound()){
    // All ESP32 boards without PSRAM use bounded response blocks.
    // The document is released as the handler returns; keep TCP reserve while
    // copying it into 512-byte blocks that are freed as COPY writes queue.
    const size_t reserve=length<2048?4096:8192;
    const size_t blocks=(length+511)/512;
    if(doc.overflowed()||heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<length+blocks*16+reserve||heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)<768){request->send(503,"application/json","{\"error\":\"Memory busy; retry shortly.\"}");return;}
    auto* response=new(std::nothrow) SegmentedJsonResponse(doc,length,statusCode);
    if(!response||!response->_sourceValid()){delete response;request->send(503,"application/json","{\"error\":\"Memory busy; retry shortly.\"}");return;}
    response->addHeader("Cache-Control","no-store");request->send(response);
    return;
    }
    // Keep room for TCP and the response object's send buffer. Failed JSON
    // allocations must return a retryable response, never reset the device.
    char* data = nullptr;
    constexpr size_t jsonReserve=12000;
    constexpr size_t jsonContiguousReserve=0;
    if (!doc.overflowed() && heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT) > length + jsonReserve &&
        heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) > length + 1 + jsonContiguousReserve) {
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
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
}


void WebServerManager::registerApiRoutes() {
    MiniExco::routes(server_,[this](AsyncWebServerRequest* r){return !redirectCaptivePortalIfNeeded(r)&&ensureAuthorized(r);},[this]{return !security_.locked();});
    server_.on("/api/rs485",HTTP_GET,[this](AsyncWebServerRequest* request){
        if(redirectCaptivePortalIfNeeded(request)||!ensureAuthorized(request))return;
        JsonDocument doc;Rs485::snapshot(doc.to<JsonObject>());sendJson(request,doc);
    });
    auto* rsHandler=new AsyncCallbackJsonWebHandler("/api/rs485",[this](AsyncWebServerRequest* request,JsonVariant& json){
        if(redirectCaptivePortalIfNeeded(request)||!ensureAuthorized(request))return;
        String error;JsonDocument result;bool ok=Rs485::command(json,error);result["queued"]=ok;if(!ok)result["error"]=error;
        String data;serializeJson(result,data);request->send(ok?202:400,"application/json",data);
    });rsHandler->setMaxContentLength(512);rsHandler->setMethod(HTTP_POST);server_.addHandler(rsHandler);

    server_.on("/api/bno055",HTTP_GET,[this](AsyncWebServerRequest* request){
        if(redirectCaptivePortalIfNeeded(request)||!ensureAuthorized(request))return;
        JsonDocument doc;Bno055::snapshot(doc.to<JsonObject>());sendJson(request,doc);
    });
    auto* bnoHandler=new AsyncCallbackJsonWebHandler("/api/bno055",[this](AsyncWebServerRequest* request,JsonVariant& json){
        if(redirectCaptivePortalIfNeeded(request)||!ensureAuthorized(request))return;
        String error;JsonDocument result;bool ok=Bno055::command(json,error);result["queued"]=ok;if(!ok)result["error"]=error;
        String data;serializeJson(result,data);request->send(ok?202:400,"application/json",data);
    });bnoHandler->setMaxContentLength(512);bnoHandler->setMethod(HTTP_POST);server_.addHandler(bnoHandler);
#if defined(APP_SPK_BOARD) && !defined(APP_DISABLE_AUDIO)
    registerMicrophoneRoutes(server_,[this](AsyncWebServerRequest* request){return !redirectCaptivePortalIfNeeded(request)&&ensureAuthorized(request);});
    registerSpkMediaRoutes(server_,[this](AsyncWebServerRequest* request){return !redirectCaptivePortalIfNeeded(request)&&ensureAuthorized(request);},[this]{return !security_.locked();});
#endif
#if APP_HAS_CAMERA
    beginCameraService();
    registerCameraRoutes(server_,[this](AsyncWebServerRequest* request){return !redirectCaptivePortalIfNeeded(request)&&ensureAuthorized(request);});
#endif
#if !APP_SUNTON_PANEL
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
#endif
    server_.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
            return;
        }
        JsonDocument doc;
        JsonObject root = doc.to<JsonObject>();
        if(security_.locked()) {
            const auto state=appState_->snapshot();
            root["device"]["deviceName"]=state.device.deviceName;root["device"]["friendlyName"]=state.device.friendlyName;
            root["network"]["ip"]=state.network.ip;root["network"]["wifiConnected"]=state.network.wifiConnected;root["network"]["mqttConnected"]=state.network.mqttConnected;
            root["battery"]["voltage"]=state.battery.voltage;root["system"]["webUiLocked"]=true;
            root["firmware"]["version"]=APP_VERSION;sendJson(request,doc);return;
        }
#ifndef APP_LEGACY_OTA_FIT
        appState_->toJson(root);
        wifiManager_->appendTxPowerStatus(root["network"].as<JsonObject>());
#if APP_SUNTON_PANEL == 3
        appendSystemMetricsJson(root,!(request->hasParam("detail")&&request->getParam("detail")->value()=="compact"));
#else
        appendSystemMetricsJson(root);
#endif
        root["system"]["webUiLocked"] = security_.locked();
#else
        if(settingsReader_)settingsReader_([&](const SettingsBundle& settings){root["system"]["deviceName"]=settings.device.friendlyName;});
        else root["system"]["deviceName"] = settingsGetter_().device.friendlyName;
#endif
        root["sdFormat"]["prompt"]=sdFormatPromptNeeded();
        root["sdFormat"]["state"]=static_cast<int>(sdFormatState());
        root["sdFormat"]["mounted"]=storageMounted(StorageTarget::Sd);
        root["sdFormat"]["ejected"]=sdStorageEjected();
        JsonObject firmware = doc["firmware"].to<JsonObject>();
        firmware["version"] = APP_VERSION;
#if defined(APP_SPK_BOARD) && !defined(APP_DISABLE_AUDIO)
        firmware["microphones"] = true;
#endif
#if APP_HAS_CAMERA
        firmware["camera"] = true;
#endif
#if APP_HAS_ONBOARD_PANEL
        firmware["touchscreen"] = true;
#endif
        firmware["configurationSnapshotVersion"] = 1;
        firmware["serialConfigurationSnapshot"] = true;
        firmware["selectiveConfigurationApply"] = true;
        firmware["buildDate"] = APP_BUILD_DATE;
    #if defined(CONFIG_IDF_TARGET_ESP32S3)
        firmware["chipFamily"] = "esp32s3";
    #elif defined(CONFIG_IDF_TARGET_ESP32S2)
        firmware["chipFamily"] = "esp32s2";
    #elif defined(CONFIG_IDF_TARGET_ESP32C6)
        firmware["chipFamily"] = "esp32c6";
    #elif defined(CONFIG_IDF_TARGET_ESP32C2)
        firmware["chipFamily"] = "esp32c2";
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
        if(settingsReader_)settingsReader_([&](const SettingsBundle& settings){firmware["buttonEventTopic"]=settings.mqtt.baseTopic+"/event/button_action";});
        else firmware["buttonEventTopic"] = settingsGetter_().mqtt.baseTopic + "/event/button_action";
        otaManager_->appendStatusJson(doc["otaManager"].to<JsonObject>());
        if (motorStatusAppender_) {
            motorStatusAppender_(doc["motor"].to<JsonObject>());
        }
        if (ledStatusAppender_) ledStatusAppender_(doc.as<JsonObject>());
#endif
        sendJson(request, doc);
    });

#ifndef APP_LEGACY_OTA_FIT
    server_.on("/api/settings", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
            return;
        }
#if APP_SUNTON_PANEL == 3
        JsonDocument doc(settingsJsonAllocator());
#else
        JsonDocument doc;
#endif
        bool ready=false;
        if(settingsReader_)settingsReader_([&](const SettingsBundle& settings){settingsManager_->toJson(settings,doc.to<JsonObject>());ready=true;});
        else {settingsManager_->toJson(settingsGetter_(),doc.to<JsonObject>());ready=true;}
        if(!ready){request->send(503,"application/json","{\"error\":\"Configuration busy; retry\"}");return;}
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

    auto* plotClockHandler=new AsyncCallbackJsonWebHandler("/api/plots/time",[this](AsyncWebServerRequest* request,JsonVariant& json){
        if(redirectCaptivePortalIfNeeded(request)||!ensureAuthorized(request))return;
        if(request->hasHeader("Origin")){String origin=request->getHeader("Origin")->value();if(origin!=String("http://")+request->host()&&origin!=String("https://")+request->host()){request->send(403);return;}}
        double epoch=json["epoch"]|0.0;if(!std::isfinite(epoch)||epoch<1577836800000.0||epoch>4102444800000.0){request->send(400,"application/json","{\"error\":\"Invalid browser time\"}");return;}
        timeval value;value.tv_sec=static_cast<time_t>(epoch/1000);value.tv_usec=static_cast<suseconds_t>(std::fmod(epoch,1000)*1000);
        if(settimeofday(&value,nullptr)!=0){request->send(500,"application/json","{\"error\":\"Clock update failed\"}");return;}
        request->send(200,"application/json","{\"ok\":true}");
    });plotClockHandler->setMaxContentLength(128);plotClockHandler->setMethod(HTTP_POST);server_.addHandler(plotClockHandler);
    server_.on("/api/plots/config",HTTP_GET,[this](AsyncWebServerRequest* request){
        if(redirectCaptivePortalIfNeeded(request)||!ensureAuthorized(request))return;
        JsonDocument graph,result;if(logicsGetter_)logicsGetter_(graph,true);
        if(graph["error"].is<const char*>()){request->send(503,"application/json","{\"error\":\"Logics busy\"}");return;}
        JsonArray plots=result["plots"].to<JsonArray>(),recordings=result["recordings"].to<JsonArray>();
        for(JsonObjectConst node:graph["graph"]["nodes"].as<JsonArrayConst>()){
            const char* type=node["type"]|"";if(strcmp(type,"mainboard.plot")&&strcmp(type,"mainboard.save_data"))continue;
            JsonObject item=(strcmp(type,"mainboard.plot")==0?plots:recordings).add<JsonObject>();item["id"]=node["id"];item["plot"]=node["parameters"]["plot"];item["series"]=node["parameters"]["series"];item["path"]=node["parameters"]["path"]|"/";
        }
        plotRecordingStatus(result["storage"].to<JsonObject>());sendJson(request,result);
    });
    server_.on("/api/plots/history",HTTP_GET,[this](AsyncWebServerRequest* request){
        if(redirectCaptivePortalIfNeeded(request)||!ensureAuthorized(request))return;
        if(!request->hasParam("id")){request->send(400,"application/json","{\"error\":\"Recording ID required\"}");return;}
        String id=request->getParam("id")->value();JsonDocument graph(storageJsonAllocator()),result(storageJsonAllocator());if(logicsGetter_)logicsGetter_(graph,true);
        String path;for(JsonObjectConst node:graph["graph"]["nodes"].as<JsonArrayConst>())if(node["type"]=="mainboard.save_data"&&node["id"]==id){plotRecordingPath(node["parameters"]["path"]|"/",node["parameters"]["plot"]|"",path);break;}
        if(path.isEmpty()){request->send(404,"application/json","{\"error\":\"Recording not configured\"}");return;}
        uint32_t offset=request->hasParam("offset")?strtoul(request->getParam("offset")->value().c_str(),nullptr,10):0;
        double from=request->hasParam("from")?strtod(request->getParam("from")->value().c_str(),nullptr):0;
        double to=request->hasParam("to")?strtod(request->getParam("to")->value().c_str(),nullptr):9007199254740991.0;
        if(!std::isfinite(from)||!std::isfinite(to)||from<0||to<from){request->send(400,"application/json","{\"error\":\"Invalid date range\"}");return;}
        String error;if(!readPlotHistory(path,offset,from,to,result,error)){result["error"]=error;String body;serializeJson(result,body);request->send(503,"application/json",body);return;}
        sendJson(request,result);
    });
    server_.on("/api/plots",HTTP_GET,[this](AsyncWebServerRequest* request){
        if(redirectCaptivePortalIfNeeded(request)||!ensureAuthorized(request))return;
        uint32_t after=request->hasParam("after")?strtoul(request->getParam("after")->value().c_str(),nullptr,10):0;
        uint32_t boot=request->hasParam("boot")?strtoul(request->getParam("boot")->value().c_str(),nullptr,10):0;
        JsonDocument result(storageJsonAllocator());plotSamplesSince(after,boot,result);plotRecordingStatus(result["storage"].to<JsonObject>());sendJson(request,result);
    });
    server_.on("/api/logics",HTTP_GET,[this](AsyncWebServerRequest* request){
        if(redirectCaptivePortalIfNeeded(request)||!ensureAuthorized(request))return;
        if(!logicsGetter_){request->send(503,"application/json","{\"error\":\"Logics unavailable\"}");return;}
        JsonDocument result;logicsGetter_(result,!request->hasParam("live"));sendJson(request,result);
    });
    auto* logicHandler=new AsyncCallbackJsonWebHandler("/api/logics",[this](AsyncWebServerRequest* request,JsonVariant& json){
        if(redirectCaptivePortalIfNeeded(request)||!ensureAuthorized(request))return;
        JsonDocument result;String error;
        if(!logicsHandler_ || !logicsHandler_(json,result,error)){result["error"]=error.isEmpty()?"Logics unavailable":error;String body;serializeJson(result,body);request->send(400,"application/json",body);return;}
        sendJson(request,result);
    });logicHandler->setMaxContentLength(32768);logicHandler->setMethod(HTTP_POST);server_.addHandler(logicHandler);

    auto* sourceHandler = new AsyncCallbackJsonWebHandler("/api/audio/source", [this](AsyncWebServerRequest* request, JsonVariant& json) {
        if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) return;
        String error;
        if (!audioSourceHandler_ || !audioSourceHandler_(json,error)) {
            JsonDocument result;result["error"]=error.isEmpty()?"Audio source runtime unavailable":error;String body;serializeJson(result,body);request->send(400,"application/json",body);return;
        }
        request->send(202,"application/json","{\"queued\":true}");
    });sourceHandler->setMaxContentLength(16384);sourceHandler->setMethod(HTTP_POST);server_.addHandler(sourceHandler);

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
            const String url = String(static_cast<const char*>(json["text"] | json["url"] | ""));
            const String label = String(static_cast<const char*>(json["label"] | ""));
            const String type = String(static_cast<const char*>(json["type"] | (json["text"].isNull() ? "stream" : "tts-offline")));
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

        beginStorageRead(target);
        struct ReadLease {StorageTarget target;~ReadLease(){endStorageRead(target);}} requestLease{target};
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
        const size_t downloadSize=file.size();
        auto stream = StorageDownload::create(target,std::move(file));
        if (!stream) {
            request->send(503,"application/json","{\"error\":\"Storage stream limit or memory reserve reached.\"}");return;
        }
        AsyncWebServerResponse* response=request->beginResponse(contentTypeForPath(path),downloadSize,
            [stream](uint8_t* buffer,size_t maximum,size_t)->size_t{return stream->read(buffer,maximum);});
        if (!response || !stream->start()) {
            delete response;request->send(503,"application/json","{\"error\":\"Storage reader unavailable.\"}");return;
        }
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
            String mountError;const bool mounted = requestSdMount(true,mountError);

            JsonDocument response;
            response["ok"] = mounted;
            response["message"] = mounted ? "Mount requested. Refresh files shortly." : mountError;
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

    server_.on("/api/storage/eject",HTTP_POST,[this](AsyncWebServerRequest* request){
        if(redirectCaptivePortalIfNeeded(request)||!ensureAuthorized(request))return;
        String error;JsonDocument response;bool ok=requestSdMount(false,error);
        response["ok"]=ok;if(!ok)response["error"]=error;
        sendJson(request,response,ok?200:409);
    });
    server_.on("/api/storage/format",HTTP_POST,[this](AsyncWebServerRequest* request){
        if(redirectCaptivePortalIfNeeded(request)||!ensureAuthorized(request))return;
        String action=request->hasParam("action")?request->getParam("action")->value():String();
        JsonDocument result;String error;
        if(action=="prompt"){if(!requestSdFormatPrompt(error)){result["error"]=error;sendJson(request,result,409);return;}result["ok"]=true;}
        else if(action=="cancel"){dismissSdFormatPrompt();result["ok"]=true;}
        else if(action=="confirm" && request->hasParam("erase") && request->getParam("erase")->value()=="yes"){
            if(!requestSdFormat(true,error,request->hasParam("filesystem")?request->getParam("filesystem")->value():String("FAT32"))){result["error"]=error;sendJson(request,result,409);return;}
            result["ok"]=true;
        }else{result["error"]="Explicit erase confirmation required";sendJson(request,result,400);return;}
        sendJson(request,result);
    });
    // AsyncCallbackWebHandler also matches descendants; register checksum
    // before the broader /api/storage listing route.
    registerStorageIntegrityRoutes(server_,[this](AsyncWebServerRequest* request){return ensureAuthorized(request);});
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

            prioritizeStorageOperation(target);
            if(storageBusy(target)){request->send(423,"application/json","{\"error\":\"Waiting for storage; background indexing is paused. Active transfers or recordings must finish safely.\"}");return;}
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
            // Invalidate the cached listing immediately. A batch must not start a
            // competing index writer after every deleted file. Live listing is the fallback.
            storageRemove(target,storageIndexPathForDirectory(storageParentPath(path)));

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
            String indexError;beginStorageReindexJob(target,directoryPath,indexError);

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

            if(storageUploadOwner_!=request)return;
            JsonDocument response;
            if(!storageUploadFinished_ && storageUploadError_.isEmpty())storageUploadError_="Upload did not complete";
            if (!storageUploadError_.isEmpty()) {
                response["error"] = storageUploadError_;
                const int failureStatus=storageUploadError_.startsWith("Storage is busy") ? 423 : 400;
                if (storageUploadFile_) {
                    storageUploadFile_.close();
                }
                if (storageUploadCreated_ && !storageUploadPath_.isEmpty()) {
                    storageRemove(storageUploadTarget_, storageUploadTempPath_);
                }
                releaseStorageUploadLease();storageUploadOwner_=nullptr;
                storageUploadPath_ = "";
                storageUploadBytesWritten_ = 0;
                storageUploadLimitBytes_ = 0;
                storageUploadError_ = "";
                sendJson(request, response, failureStatus);
                return;
            }

            char checksum[9];snprintf(checksum,sizeof(checksum),"%08lx",static_cast<unsigned long>(storageUploadCrc_^0xffffffff));
            response["crc32"]=checksum;response["sizeBytes"]=storageUploadBytesWritten_;
            response["ok"] = true;
            response["message"] = String("Uploaded ") + storageUploadPath_;
            response["path"] = storageUploadPath_;
            appendStorageDirectoryJson(storageUploadTarget_, storageParentPath(storageUploadPath_), response);
            storageUploadPath_ = "";
            storageUploadBytesWritten_ = 0;
            storageUploadLimitBytes_ = 0;
            storageUploadError_ = "";
            releaseStorageUploadLease();storageUploadOwner_=nullptr;
            sendJson(request, response);
        },
        [this](AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool final) {
            if (redirectCaptivePortalIfNeeded(request) || !ensureAuthorized(request)) {
                return;
            }

            if (index == 0) {
                if(storageUploadOwner_ && storageUploadOwner_!=request){request->send(409,"application/json","{\"error\":\"Another upload is active.\"}");return;}
                if(storageUploadOwner_==request){storageUploadError_="Only one file per upload request";return;}
                storageUploadOwner_=request;storageUploadCreated_=false;storageUploadFinished_=false;
                request->onDisconnect([this,request](){
                    if(storageUploadOwner_!=request)return;
                    storageUploadFile_.close();
                    if(storageUploadCreated_&&!storageUploadFinished_)storageRemove(storageUploadTarget_,storageUploadTempPath_);
                    releaseStorageUploadLease();storageUploadOwner_=nullptr;storageUploadPath_="";
                });
                storageUploadTarget_ = storageTargetFromRequest(request);
                const String directoryPath = storageDirectoryFromRequest(request);
                storageUploadError_ = "";
                storageUploadPath_ = joinStoragePath(directoryPath, sanitizeUploadFilename(filename));
                storageUploadCrc_=0xffffffff;storageUploadExpectedCrc_=request->header("X-File-CRC32");storageUploadExpectedCrc_.toLowerCase();
                storageUploadExpectedSize_=request->hasHeader("X-File-Size")?strtoul(request->header("X-File-Size").c_str(),nullptr,10):0;
                storageUploadTempPath_=storageUploadPath_+".upload-"+String(esp_random(),HEX);
                if(storageUploadExpectedCrc_.length() && (storageUploadExpectedCrc_.length()!=8 || !storageUploadExpectedSize_)){storageUploadError_="Invalid upload checksum or length";return;}
                storageUploadBytesWritten_ = 0;
                storageUploadLimitBytes_ = 0;

                if (!storageMounted(storageUploadTarget_)) {
                    storageUploadError_ = storageUnavailableMessage(storageUploadTarget_);
                    return;
                }
                if (storageUploadPath_.isEmpty() || !isAllowedStoragePath(storageUploadPath_)) {
                    storageUploadError_ = "Upload audio/artwork files, or static web assets (including .gz) inside /web/.";
                    return;
                }

                prioritizeStorageOperation(storageUploadTarget_);
                if(storageBusy(storageUploadTarget_)){storageUploadError_="Storage is busy; waiting for foreground access";return;}
                beginStorageWrite(storageUploadTarget_);storageUploadLeased_=true;
                const StorageBackendSummary summary = getStorageSummary(storageUploadTarget_);
                const size_t freeBytes = summary.freeBytes;
                const size_t availableBytes = freeBytes; // Stage beside the existing destination; preserve it until verified.
                storageUploadLimitBytes_ = availableBytes > kStorageReserveBytes ? availableBytes - kStorageReserveBytes : 0;
                if (storageUploadLimitBytes_ == 0) {
                    storageUploadError_ = "Not enough free space for upload.";
                    return;
                }

                storageUploadFile_ = storageOpen(storageUploadTarget_, storageUploadTempPath_, "w");
                if (!storageUploadFile_) {
                    storageUploadError_ = "Unable to open destination file.";
                    return;
                }

                storageUploadCreated_=true;
            }

            if(storageUploadOwner_!=request)return;
            if (!storageUploadError_.isEmpty()) {
                return;
            }
            if (len > 0) {
                if ((storageUploadBytesWritten_ + len) > storageUploadLimitBytes_) {
                    storageUploadError_ = "File is larger than remaining filesystem space.";
                    storageUploadFile_.close();
                    storageRemove(storageUploadTarget_, storageUploadTempPath_);
                    releaseStorageUploadLease();
                    return;
                }
                if (!ensureStorageTransferBuffer(min<size_t>(4096,len))) {
                    storageUploadError_ = psramFound() ? "Unable to allocate PSRAM-backed transfer buffer." : "Unable to allocate transfer buffer.";
                    storageUploadFile_.close();
                    storageRemove(storageUploadTarget_, storageUploadTempPath_);
                    releaseStorageUploadLease();
                    return;
                }
                size_t written=0;
                while(written<len){size_t chunk=min<size_t>(storageTransferBufferCapacity_,len-written);memcpy(storageTransferBuffer_,data+written,chunk);size_t count=storageUploadFile_.write(storageTransferBuffer_,chunk);written+=count;if(count!=chunk)break;}
                if (written != len) {
                    storageUploadError_ = "Failed while writing uploaded file.";
                    storageUploadFile_.close();
                    storageRemove(storageUploadTarget_, storageUploadTempPath_);
                    releaseStorageUploadLease();
                    return;
                }
                storageUploadCrc_=StorageIntegrity::update(storageUploadCrc_,data,written);
                storageUploadBytesWritten_ += written;
            }
            if (final && storageUploadFile_) {
                storageUploadFile_.flush();const size_t storedSize=storageUploadFile_.size();storageUploadFile_.close();
                char checksum[9];snprintf(checksum,sizeof(checksum),"%08lx",static_cast<unsigned long>(storageUploadCrc_^0xffffffff));
                if(storedSize!=storageUploadBytesWritten_ || (storageUploadExpectedCrc_.length() && (storageUploadExpectedSize_!=storedSize || storageUploadExpectedCrc_!=checksum))){storageUploadError_="Upload checksum/length mismatch; destination preserved";storageRemove(storageUploadTarget_,storageUploadTempPath_);return;}
                auto* fs=getStorageFs(storageUploadTarget_);const String backup=storageUploadTempPath_+".previous";const bool existed=fs&&fs->exists(storageUploadPath_);
                if(!fs || (existed&&!fs->rename(storageUploadPath_,backup))){storageUploadError_="Cannot stage destination replacement";return;}
                if(!fs->rename(storageUploadTempPath_,storageUploadPath_)){if(existed)fs->rename(backup,storageUploadPath_);storageUploadError_="Cannot finalize verified upload";return;}
                if(existed)fs->remove(backup);storageUploadFinished_=true;
                releaseStorageUploadLease();
                String indexError;beginStorageReindexJob(storageUploadTarget_,storageParentPath(storageUploadPath_),indexError);
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

bool WebServerManager::serveStorageWebAsset(AsyncWebServerRequest* request,const String& url,bool fallback) {
    if(request->method()!=HTTP_GET && request->method()!=HTTP_HEAD)return false;
    bool customWeb=false;
    if(settingsReader_)settingsReader_([&](const SettingsBundle& settings){customWeb=settings.ui.customWebInterface;});
    else customWeb=settingsGetter_().ui.customWebInterface;
    if(!customWeb)return false;
    if(!storageMounted(StorageTarget::Sd))return false;
    std::string normalized;if(!StaticAssetPolicy::webPath(url.c_str(),normalized))return false;
    if(redirectCaptivePortalIfNeeded(request)||!ensureAuthorized(request))return true;
    auto error=[&](int code,const char* message){if(fallback)return false;auto* response=new(std::nothrow) BoundedBufferResponse(message,strlen(message),"text/plain; charset=utf-8",code,true);if(!response){request->send(503);return true;}response->addHeader("Cache-Control","no-store");response->addHeader("Vary","Accept-Encoding");request->send(response);return true;};
    beginStorageRead(StorageTarget::Sd);struct Lease{~Lease(){endStorageRead(StorageTarget::Sd);}} lease;
    String path=normalized.c_str();
    String header=request->hasHeader("Accept-Encoding")?request->getHeader("Accept-Encoding")->value():String();
    auto encoding=StaticAssetPolicy::encodings(header.c_str());
    File file;bool gzip=false;
    if(encoding.gzip>0){file=storageOpen(StorageTarget::Sd,path+".gz","r");if(file&&file.isDirectory())file.close();gzip=bool(file);}
    if(!file&&encoding.identity>0){file=storageOpen(StorageTarget::Sd,path,"r");if(file&&file.isDirectory())file.close();}
    if(!file){bool exists=storageExists(StorageTarget::Sd,path)||storageExists(StorageTarget::Sd,path+".gz");return error(exists?406:404,exists?"No acceptable representation. Enable gzip or upload the uncompressed asset.":"Custom web asset not found.");}
    const size_t size=file.size();if(size>16u*1024u*1024u)return error(413,"Custom web asset exceeds the 16 MiB limit.");
    if(gzip){uint8_t magic[10];if(size<18||file.read(magic,sizeof(magic))!=sizeof(magic)||magic[0]!=0x1f||magic[1]!=0x8b||magic[2]!=8||!file.seek(0))return error(500,"Invalid or unreadable gzip asset.");}
    if(request->url()=="/"){auto* response=request->beginResponse(302);response->addHeader("Location","/web/");response->addHeader("Cache-Control","no-store");response->addHeader("Vary","Accept-Encoding");request->send(response);return true;}
    const time_t modified=file.getLastWrite();
    const bool html=path.endsWith(".html")||path.endsWith(".htm");
    String tag;if(modified>0){uint32_t hash=2166136261u;for(char c:normalized)hash=(hash^uint8_t(c))*16777619u;tag="W/\"sd:"+String(hash,HEX)+":"+String(static_cast<unsigned long>(modified))+":"+String(size)+(gzip?":gz\"":":raw\"");}
    auto headers=[&](AsyncWebServerResponse* response){response->addHeader("Vary","Accept-Encoding");response->addHeader("X-Content-Type-Options","nosniff");response->addHeader("Accept-Ranges","none");if(gzip)response->addHeader("Content-Encoding","gzip");if(tag.length())response->addHeader("ETag",tag);response->addHeader("Cache-Control",html||!tag.length()?"no-store, no-cache, must-revalidate, max-age=0":"public, max-age=0, must-revalidate");};
    if(!html&&tag.length()&&requestMatchesEtag(request,tag)){auto* response=request->beginResponse(304);headers(response);request->send(response);return true;}
    AsyncWebServerResponse* response=nullptr;
    if(request->method()==HTTP_HEAD){response=new(std::nothrow) BoundedBufferResponse("",size,contentTypeForPath(path),200,true);}
    else {
        auto stream=StorageDownload::create(StorageTarget::Sd,std::move(file));if(!stream)return error(503,"Storage stream limit or memory reserve reached.");
        response=new(std::nothrow) BoundedStreamResponse<StorageDownload>(stream,size,contentTypeForPath(path));
        if(!response||!stream->start()){delete response;return error(503,"Storage reader unavailable.");}
    }
    if(!response)return error(503,"Device busy; retry shortly.");headers(response);request->send(response);return true;
}

void WebServerManager::registerWebRoutes() {
    server_.on("/api/web-access",HTTP_GET,[this](AsyncWebServerRequest* request){
        if(redirectCaptivePortalIfNeeded(request)||!ensureAuthorized(request))return;
        JsonDocument doc;JsonObject output=doc.to<JsonObject>();WebAccess::diagnostics(output);
        const auto settings=settingsGetter_();output["hostname"]=WebAccess::hostname(settings)+".local";
        output["mdnsEnabled"]=settings.webAccess.mdnsEnabled;output["httpsEnabled"]=settings.webAccess.httpsEnabled;
        output["httpsUrl"]=String("https://")+WebAccess::hostname(settings)+".local"+(settings.webAccess.httpsPort==443?String():String(":")+String(settings.webAccess.httpsPort))+"/";
        sendJson(request,doc);
    });
    auto* certificateHandler=new AsyncCallbackJsonWebHandler("/api/web-access/certificate",[this](AsyncWebServerRequest* request,JsonVariant& json){
        if(redirectCaptivePortalIfNeeded(request)||!ensureAuthorized(request))return;
        if(request->hasHeader("Origin")){String origin=request->getHeader("Origin")->value();if(origin!=String("http://")+request->host() && origin!=String("https://")+request->host()){request->send(403);return;}}
        String error;bool saved=WebAccess::importCertificate(String(json["certificate"]|""),String(json["privateKey"]|""),error);
        JsonDocument doc;doc["ok"]=saved;if(!saved)doc["error"]=error;else doc["message"]="Certificate saved. New HTTPS connections use it; existing sessions retain their certificate.";
        sendJson(request,doc,saved?200:400);
    });certificateHandler->setMaxContentLength(16384);certificateHandler->setMethod(HTTP_POST);server_.addHandler(certificateHandler);
#ifdef APP_LEGACY_OTA_FIT
    static const char kLegacyPage[] =
        "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>ELMA Legacy OTA</title><style>body{font:16px system-ui;max-width:680px;margin:40px auto;padding:20px}"
        "a{color:#b65f00}</style><h1>ELMA IoT</h1><p>This legacy-partition build keeps audio, MQTT, GPIO, display, "
        "controls and OTA services. Use ELMA IoT РІР‚вЂњ ESP32 Toolkit on the same LAN for configuration and firmware updates.</p>"
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
        String encodingHeader=request->hasHeader("Accept-Encoding")?request->getHeader("Accept-Encoding")->value():String();
        const auto encoding=StaticAssetPolicy::encodings(encodingHeader.c_str());
        if((asset->gzip && encoding.gzip<=0)||(!asset->gzip && encoding.identity<=0)){
            static const char message[]="No acceptable representation. This built-in asset requires gzip.";
            auto* error=new(std::nothrow) BoundedBufferResponse(message,sizeof(message)-1,"text/plain; charset=utf-8",406,true);
            if(!error){request->send(503);return;}error->addHeader("Vary","Accept-Encoding");error->addHeader("Cache-Control","no-store");request->send(error);return;
        }
        const String tag = assetEtag(*asset);
        if (!isHtmlShell && requestMatchesEtag(request, tag)) {
            AsyncWebServerResponse* notModified = request->beginResponse(304);
            notModified->addHeader("ETag", tag);
            notModified->addHeader("Cache-Control", "public, max-age=0, must-revalidate");
            notModified->addHeader("Vary", "Accept-Encoding");
            if(asset->gzip)notModified->addHeader("Content-Encoding","gzip");
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
        response->addHeader("Vary", "Accept-Encoding");
        if (asset->gzip) {
            response->addHeader("Content-Encoding", "gzip");
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

    server_.on("/", HTTP_GET | HTTP_HEAD, [this,serveAsset](AsyncWebServerRequest* request) {
        if(serveStorageWebAsset(request,"/web/index.html",true))return;
        serveAsset(request,"/index.html");
    });
    server_.on("/system/ui",HTTP_GET | HTTP_HEAD,[serveAsset](AsyncWebServerRequest* request){serveAsset(request,"/index.html");});
    server_.on("/fallback",HTTP_GET | HTTP_HEAD,[serveAsset](AsyncWebServerRequest* request){if(request->url().endsWith("/")){auto* response=request->beginResponse(302);response->addHeader("Location","/fallback");response->addHeader("Cache-Control","no-store");request->send(response);return;}serveAsset(request,"/index.html");});
    server_.on("/fallback/",HTTP_GET | HTTP_HEAD,[](AsyncWebServerRequest* request){auto* response=request->beginResponse(302);response->addHeader("Location","/fallback");response->addHeader("Cache-Control","no-store");request->send(response);});
    server_.on("/index.html", HTTP_GET | HTTP_HEAD, [serveAsset](AsyncWebServerRequest* request) { serveAsset(request, "/index.html"); });
    server_.on("/style.css", HTTP_GET | HTTP_HEAD, [serveAsset](AsyncWebServerRequest* request) { serveAsset(request, "/style.css"); });
    server_.on("/app.js", HTTP_GET | HTTP_HEAD, [serveAsset](AsyncWebServerRequest* request) { serveAsset(request, "/app.js"); });
    server_.on("/generate_204", HTTP_GET, [this](AsyncWebServerRequest* request) { request->redirect("/"); });
    server_.on("/hotspot-detect.html", HTTP_GET, [this](AsyncWebServerRequest* request) { request->redirect("/"); });
    server_.on("/connecttest.txt", HTTP_GET, [this](AsyncWebServerRequest* request) { request->redirect("/"); });
    server_.on("/ncsi.txt", HTTP_GET, [this](AsyncWebServerRequest* request) { request->redirect("/"); });

    server_.onNotFound([this, serveAsset](AsyncWebServerRequest* request) {
        if(request->method()!=HTTP_GET&&request->method()!=HTTP_HEAD){request->send(405,"text/plain","Method not allowed");return;}
        if(serveStorageWebAsset(request,request->url()))return;
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
