#include "device_log.h"
#include "storage_backend.h"

#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>
#include <esp_partition.h>
#include <freertos/FreeRTOS.h>

namespace {
constexpr uint32_t kSdFrequenciesHz[] = {80000000UL, 40000000UL, 20000000UL, 10000000UL, 4000000UL, 1000000UL, 400000UL};
constexpr unsigned long kSdHotplugPollIntervalMs = 2000UL;
constexpr unsigned long kSdRetryBackoffMs[] = {2000UL, 5000UL, 15000UL, 60000UL};

bool flashMounted = false;
bool sdMounted = false;
bool sdSpiStarted = false;
unsigned long lastSdHotplugPollAt = 0;
unsigned long nextSdMountAttemptAt = 0;
SdSettings activeSdSettings;
StorageBackendSummary sdSummaryCache;
portMUX_TYPE storageStateMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t sdWriteDepth = 0;
uint32_t sdReadDepth = 0;
uint8_t sdConsecutiveMountFailures = 0;

#if defined(CONFIG_IDF_TARGET_ESP32S3)
SPIClass sdSpi(FSPI);
#else
SPIClass sdSpi(HSPI);
#endif

bool sameSdSettings(const SdSettings& left, const SdSettings& right) {
    return left.enabled == right.enabled && left.csPin == right.csPin && left.sckPin == right.sckPin &&
        left.mosiPin == right.mosiPin && left.misoPin == right.misoPin;
}

SdSettings effectiveSdSettings(const SdSettings& settings) {
    return settings;
}

bool sdSettingsUsePin(const SdSettings& settings, uint8_t pin) {
    if (!settings.enabled) {
        return false;
    }
    return settings.csPin == pin || settings.sckPin == pin || settings.mosiPin == pin || settings.misoPin == pin;
}

unsigned long sdRetryDelayForFailureCount(uint8_t failureCount) {
    if (failureCount == 0) {
        return kSdHotplugPollIntervalMs;
    }

    const size_t maxIndex = (sizeof(kSdRetryBackoffMs) / sizeof(kSdRetryBackoffMs[0])) - 1;
    const size_t index = failureCount > maxIndex ? maxIndex : failureCount - 1;
    return kSdRetryBackoffMs[index];
}

void resetSdMountRetryState() {
    sdConsecutiveMountFailures = 0;
    nextSdMountAttemptAt = 0;
}

bool sdFilesystemHealthy() {
    if (!sdMounted) {
        return false;
    }

    const uint64_t cardBytes = SD.cardSize();
    const size_t totalBytes = SD.totalBytes();
    if (cardBytes == 0 || totalBytes == 0) {
        return false;
    }

    File root = SD.open("/");
    const bool healthy = root && root.isDirectory();
    if (root) {
        root.close();
    }
    return healthy;
}

void setSdWriteDepth(uint32_t depth) {
    portENTER_CRITICAL(&storageStateMux);
    sdWriteDepth = depth;
    portEXIT_CRITICAL(&storageStateMux);
}

void incrementSdWriteDepth() {
    portENTER_CRITICAL(&storageStateMux);
    ++sdWriteDepth;
    portEXIT_CRITICAL(&storageStateMux);
}

void decrementSdWriteDepth() {
    portENTER_CRITICAL(&storageStateMux);
    if (sdWriteDepth > 0) {
        --sdWriteDepth;
    }
    portEXIT_CRITICAL(&storageStateMux);
}

bool sdWriteInProgress() {
    portENTER_CRITICAL(&storageStateMux);
    const bool busy = sdWriteDepth > 0;
    portEXIT_CRITICAL(&storageStateMux);
    return busy;
}

void incrementSdReadDepth() {
    portENTER_CRITICAL(&storageStateMux);
    ++sdReadDepth;
    portEXIT_CRITICAL(&storageStateMux);
}

void decrementSdReadDepth() {
    portENTER_CRITICAL(&storageStateMux);
    if (sdReadDepth > 0) {
        --sdReadDepth;
    }
    portEXIT_CRITICAL(&storageStateMux);
}

bool sdReadInProgress() {
    portENTER_CRITICAL(&storageStateMux);
    const bool busy = sdReadDepth > 0;
    portEXIT_CRITICAL(&storageStateMux);
    return busy;
}

void cacheSdSummary(const StorageBackendSummary& summary) {
    portENTER_CRITICAL(&storageStateMux);
    sdSummaryCache = summary;
    portEXIT_CRITICAL(&storageStateMux);
}

StorageBackendSummary cachedSdSummary() {
    portENTER_CRITICAL(&storageStateMux);
    const StorageBackendSummary summary = sdSummaryCache;
    portEXIT_CRITICAL(&storageStateMux);
    return summary;
}

StorageBackendSummary readLiveSdSummary() {
    StorageBackendSummary summary;
    summary.available = activeSdSettings.enabled;
    summary.mounted = sdMounted;
    if (!sdMounted) {
        return summary;
    }

    summary.cardSizeBytes = SD.cardSize();
    summary.totalBytes = static_cast<uint64_t>(SD.totalBytes());
    summary.usedBytes = static_cast<uint64_t>(SD.usedBytes());
    summary.freeBytes = summary.totalBytes > summary.usedBytes ? summary.totalBytes - summary.usedBytes : 0;
    return summary;
}

void refreshSdSummaryCache() {
    cacheSdSummary(readLiveSdSummary());
}

const esp_partition_t* flashFilesystemPartition() {
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
}

void mountFlashStorage() {
    const esp_partition_t* partition = flashFilesystemPartition();
    if (partition == nullptr) {
        flashMounted = false;
        DebugLog.println("[storage] Flash filesystem disabled by partition table");
        return;
    }

    // Arduino's LittleFS wrapper defaults to the partition label "spiffs".
    // ELMA names this generic data partition "storage", so mount the actual
    // label discovered from the partition table instead of relying on that
    // unrelated default.
    flashMounted = LittleFS.begin(false, "/littlefs", 10, partition->label);
    if (!flashMounted) {
        DebugLog.println("[storage] LittleFS mount failed, attempting format");
        flashMounted = LittleFS.begin(true, "/littlefs", 10, partition->label);
    }

    if (flashMounted) {
        DebugLog.printf("[storage] LittleFS mounted total=%u used=%u\n",
                      static_cast<unsigned>(LittleFS.totalBytes()),
                      static_cast<unsigned>(LittleFS.usedBytes()));
    } else {
        DebugLog.println("[storage] LittleFS mount failed");
    }
}

void unmountSdStorage() {
    if (sdMounted) {
        SD.end();
        sdMounted = false;
    }
    if (sdSpiStarted) {
        sdSpi.end();
        sdSpiStarted = false;
    }

    StorageBackendSummary summary = cachedSdSummary();
    summary.available = activeSdSettings.enabled;
    summary.mounted = false;
    summary.usedBytes = 0;
    summary.freeBytes = summary.totalBytes;
    cacheSdSummary(summary);
}

void mountSdStorage(const SdSettings& settings) {
    unmountSdStorage();
    activeSdSettings = settings;
    lastSdHotplugPollAt = millis();
    setSdWriteDepth(0);
    portENTER_CRITICAL(&storageStateMux);
    sdReadDepth = 0;
    portEXIT_CRITICAL(&storageStateMux);

    if (!settings.enabled) {
        resetSdMountRetryState();
        cacheSdSummary(StorageBackendSummary{});
        return;
    }

    pinMode(settings.csPin, OUTPUT);
    digitalWrite(settings.csPin, HIGH);
    sdSpi.begin(settings.sckPin, settings.misoPin, settings.mosiPin, settings.csPin);
    sdSpiStarted = true;

    for (const uint32_t frequencyHz : kSdFrequenciesHz) {
        if (!SD.begin(settings.csPin, sdSpi, frequencyHz, "/sd", 5, false)) {
            DebugLog.printf("[storage] SD begin failed cs=%u sck=%u mosi=%u miso=%u freq=%lu\n",
                          static_cast<unsigned>(settings.csPin),
                          static_cast<unsigned>(settings.sckPin),
                          static_cast<unsigned>(settings.mosiPin),
                          static_cast<unsigned>(settings.misoPin),
                          static_cast<unsigned long>(frequencyHz));
            continue;
        }

        const uint64_t cardBytes = SD.cardSize();
        const size_t totalBytes = SD.totalBytes();
        if (cardBytes == 0 || totalBytes == 0) {
            DebugLog.printf("[storage] SD detected but filesystem unavailable cs=%u sck=%u mosi=%u miso=%u freq=%lu card=%llu total=%u\n",
                          static_cast<unsigned>(settings.csPin),
                          static_cast<unsigned>(settings.sckPin),
                          static_cast<unsigned>(settings.mosiPin),
                          static_cast<unsigned>(settings.misoPin),
                          static_cast<unsigned long>(frequencyHz),
                          static_cast<unsigned long long>(cardBytes),
                          static_cast<unsigned>(totalBytes));
            SD.end();
            continue;
        }

        sdMounted = true;
        resetSdMountRetryState();
        DebugLog.printf("[storage] SD mounted cs=%u sck=%u mosi=%u miso=%u freq=%lu card=%llu total=%u used=%u\n",
                      static_cast<unsigned>(settings.csPin),
                      static_cast<unsigned>(settings.sckPin),
                      static_cast<unsigned>(settings.mosiPin),
                      static_cast<unsigned>(settings.misoPin),
                      static_cast<unsigned long>(frequencyHz),
                      static_cast<unsigned long long>(cardBytes),
                      static_cast<unsigned>(totalBytes),
                      static_cast<unsigned>(SD.usedBytes()));
        refreshSdSummaryCache();
        break;
    }

    if (!sdMounted) {
        ++sdConsecutiveMountFailures;
        const unsigned long retryDelayMs = sdRetryDelayForFailureCount(sdConsecutiveMountFailures);
        nextSdMountAttemptAt = millis() + retryDelayMs;
        DebugLog.printf("[storage] SD mount failed cs=%u sck=%u mosi=%u miso=%u retry_in=%lu failure=%u\n",
                      static_cast<unsigned>(settings.csPin),
                      static_cast<unsigned>(settings.sckPin),
                      static_cast<unsigned>(settings.mosiPin),
                      static_cast<unsigned>(settings.misoPin),
                      static_cast<unsigned long>(retryDelayMs),
                      static_cast<unsigned>(sdConsecutiveMountFailures));
        StorageBackendSummary summary;
        summary.available = settings.enabled;
        cacheSdSummary(summary);
    }
}
}  // namespace

void beginStorageBackends(const SettingsBundle& settings) {
    mountFlashStorage();
    mountSdStorage(effectiveSdSettings(settings.sd));
}

void applyStorageSettings(const SettingsBundle& settings) {
    const SdSettings effective = effectiveSdSettings(settings.sd);
    if (!flashMounted) {
        mountFlashStorage();
    }
    if (!sameSdSettings(activeSdSettings, effective)) {
        mountSdStorage(effective);
    } else if (effective.enabled && !sdMounted) {
        mountSdStorage(effective);
    }
}

bool remountActiveStorageBackend(StorageTarget target) {
    if (target == StorageTarget::Flash) {
        if (flashMounted) {
            LittleFS.end();
            flashMounted = false;
        }
        mountFlashStorage();
        return flashMounted;
    }

    if (sdWriteInProgress() || sdReadInProgress()) {
        return false;
    }

    mountSdStorage(activeSdSettings);
    return sdMounted;
}

bool remountStorageBackend(StorageTarget target, const SettingsBundle& settings) {
    if (target == StorageTarget::Flash) {
        if (flashMounted) {
            LittleFS.end();
            flashMounted = false;
        }
        mountFlashStorage();
        return flashMounted;
    }

    if (sdWriteInProgress() || sdReadInProgress()) {
        return false;
    }

    mountSdStorage(effectiveSdSettings(settings.sd));
    return sdMounted;
}

void pollStorageBackends() {
    if (!activeSdSettings.enabled) {
        return;
    }

    if (sdWriteInProgress() || sdReadInProgress()) {
        return;
    }

    const unsigned long now = millis();
    if (static_cast<unsigned long>(now - lastSdHotplugPollAt) < kSdHotplugPollIntervalMs) {
        return;
    }
    lastSdHotplugPollAt = now;

    if (sdMounted) {
        if (!sdFilesystemHealthy()) {
            DebugLog.println("[storage] SD card removed or became unavailable");
            unmountSdStorage();
            resetSdMountRetryState();
        }
        return;
    }

    if (nextSdMountAttemptAt != 0 && static_cast<long>(now - nextSdMountAttemptAt) < 0) {
        return;
    }

    mountSdStorage(activeSdSettings);
}

StorageTarget parseStorageTarget(const String& rawTarget) {
    String target = rawTarget;
    target.trim();
    target.toLowerCase();
    return target == "sd" ? StorageTarget::Sd : StorageTarget::Flash;
}

const char* storageTargetId(StorageTarget target) {
    return target == StorageTarget::Sd ? "sd" : "flash";
}

const char* storageTargetLabel(StorageTarget target) {
    return target == StorageTarget::Sd ? "SD card" : "Flash filesystem";
}

StorageBackendSummary getStorageSummary(StorageTarget target) {
    StorageBackendSummary summary;

    if (target == StorageTarget::Flash) {
        const esp_partition_t* partition = flashFilesystemPartition();
        summary.available = partition != nullptr;
        summary.cardSizeBytes = partition != nullptr ? static_cast<uint64_t>(partition->size) : 0;
        summary.totalBytes = summary.cardSizeBytes;
        summary.freeBytes = summary.totalBytes;

        const size_t mountedTotal = LittleFS.totalBytes();
        if (mountedTotal > 0) {
            summary.mounted = true;
            summary.totalBytes = static_cast<uint64_t>(mountedTotal);
            summary.usedBytes = static_cast<uint64_t>(LittleFS.usedBytes());
            summary.freeBytes = summary.totalBytes > summary.usedBytes ? summary.totalBytes - summary.usedBytes : 0;
        }
        return summary;
    }

    summary.available = activeSdSettings.enabled;
    summary = cachedSdSummary();
    summary.available = activeSdSettings.enabled;
    summary.mounted = sdMounted;
    if (!sdMounted) {
        summary.usedBytes = 0;
        summary.freeBytes = summary.totalBytes;
    }
    return summary;
}

void beginStorageWrite(StorageTarget target) {
    if (target == StorageTarget::Sd) {
        incrementSdWriteDepth();
    }
}

void endStorageWrite(StorageTarget target) {
    if (target != StorageTarget::Sd) {
        return;
    }

    decrementSdWriteDepth();
    if (sdMounted && !sdWriteInProgress()) {
        refreshSdSummaryCache();
    }
}

void beginStorageRead(StorageTarget target) {
    if (target == StorageTarget::Sd) {
        incrementSdReadDepth();
    }
}

void endStorageRead(StorageTarget target) {
    if (target == StorageTarget::Sd) {
        decrementSdReadDepth();
    }
}

bool storageBusy(StorageTarget target) {
    return target == StorageTarget::Sd ? (sdWriteInProgress() || sdReadInProgress()) : false;
}

fs::FS* getStorageFs(StorageTarget target) {
    if (target == StorageTarget::Sd) {
        return sdMounted ? static_cast<fs::FS*>(&SD) : nullptr;
    }
    return LittleFS.totalBytes() > 0 ? static_cast<fs::FS*>(&LittleFS) : nullptr;
}

bool storageMounted(StorageTarget target) {
    if (target == StorageTarget::Sd) {
        return sdMounted;
    }
    return LittleFS.totalBytes() > 0;
}

bool storageConfigured(StorageTarget target) {
    return target == StorageTarget::Sd ? activeSdSettings.enabled : flashFilesystemPartition() != nullptr;
}

bool storageExists(StorageTarget target, const String& path) {
    fs::FS* fs = getStorageFs(target);
    return fs != nullptr && fs->exists(path);
}

File storageOpen(StorageTarget target, const String& path, const char* mode) {
    fs::FS* fs = getStorageFs(target);
    if (fs == nullptr) {
        return File();
    }
    return fs->open(path, mode);
}

bool storageRemove(StorageTarget target, const String& path) {
    fs::FS* fs = getStorageFs(target);
    return fs != nullptr && fs->remove(path);
}

bool sdStorageUsesPin(uint8_t pin) {
    return sdSettingsUsePin(activeSdSettings, pin);
}
