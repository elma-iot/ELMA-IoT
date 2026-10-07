// Size-fit replacement retains flash files, Logics persistence and web file APIs.
#if APP_DISABLE_SD
#include "storage_backend.h"
#include <LittleFS.h>
#include <esp_partition.h>
namespace {
const esp_partition_t* flashPartition() {
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
}
bool mountFlash() {
    const auto* partition = flashPartition();
    if (!partition) return false;
    return LittleFS.begin(false, "/littlefs", 10, partition->label) || LittleFS.begin(true, "/littlefs", 10, partition->label);
}
}
void beginStorageBackends(const SettingsBundle&) { mountFlash(); }
void applyStorageSettings(const SettingsBundle&) {}
void pollStorageBackends() {}
bool requestSdFormat(bool, String& error) { error="SD storage is unavailable";return false; }
SdFormatState sdFormatState(){return SdFormatState::Idle;}
bool sdFormatPromptNeeded(){return false;}
void dismissSdFormatPrompt(){}
bool requestSdMount(bool,String& error){error="SD storage unavailable";return false;}
bool sdStorageEjected(){return false;}
bool remountActiveStorageBackend(StorageTarget target) { return target == StorageTarget::Flash && mountFlash(); }
bool remountStorageBackend(StorageTarget target, const SettingsBundle&) { return remountActiveStorageBackend(target); }
StorageTarget parseStorageTarget(const String& raw) { String value = raw; value.trim(); value.toLowerCase(); return value == "sd" ? StorageTarget::Sd : StorageTarget::Flash; }
const char* storageTargetId(StorageTarget target) { return target == StorageTarget::Sd ? "sd" : "flash"; }
const char* storageTargetLabel(StorageTarget target) { return target == StorageTarget::Sd ? "SD card" : "Flash filesystem"; }
StorageBackendSummary getStorageSummary(StorageTarget target) {
    StorageBackendSummary result;
    if (target == StorageTarget::Sd) return result;
    const auto* partition = flashPartition();
    result.available = partition != nullptr;
    result.cardSizeBytes = partition ? partition->size : 0;
    result.mounted = LittleFS.totalBytes() > 0;
    result.totalBytes = result.mounted ? LittleFS.totalBytes() : result.cardSizeBytes;
    result.usedBytes = result.mounted ? LittleFS.usedBytes() : 0;
    result.freeBytes = result.totalBytes > result.usedBytes ? result.totalBytes - result.usedBytes : 0;
    return result;
}
void beginStorageWrite(StorageTarget) {}
void endStorageWrite(StorageTarget) {}
void beginStorageRead(StorageTarget) {}
void endStorageRead(StorageTarget) {}
bool storageBusy(StorageTarget) { return false; }
fs::FS* getStorageFs(StorageTarget target) { return target == StorageTarget::Flash && LittleFS.totalBytes() > 0 ? static_cast<fs::FS*>(&LittleFS) : nullptr; }
bool storageMounted(StorageTarget target) { return getStorageFs(target) != nullptr; }
bool storageConfigured(StorageTarget target) { return target == StorageTarget::Flash && flashPartition() != nullptr; }
bool storageExists(StorageTarget target, const String& path) { auto* fs = getStorageFs(target); return fs && fs->exists(path); }
File storageOpen(StorageTarget target, const String& path, const char* mode) { auto* fs = getStorageFs(target); return fs ? fs->open(path, mode) : File(); }
bool storageRemove(StorageTarget target, const String& path) { auto* fs = getStorageFs(target); return fs && fs->remove(path); }
bool sdStorageUsesPin(uint8_t) { return false; }
#endif
