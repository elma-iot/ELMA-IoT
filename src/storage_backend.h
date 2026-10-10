#pragma once

#include <Arduino.h>
#include <FS.h>

#include "settings_schema.h"

enum class StorageTarget {
    Flash,
    Sd,
};

struct StorageBackendSummary {
    bool available = false;
    bool mounted = false;
    bool needsFormat = false; // Card responds, but FAT reports no usable filesystem.
    uint64_t cardSizeBytes = 0;
    uint64_t totalBytes = 0;
    uint64_t usedBytes = 0;
    uint64_t freeBytes = 0;
};

void beginStorageBackends(const SettingsBundle& settings);
void applyStorageSettings(const SettingsBundle& settings);
void pollStorageBackends();
bool requestStorageBackgroundJob(void (*work)(void*),void* context);
bool requestSdFormatPrompt(String& error);
bool requestSdFormat(bool confirmed, String& error, const String& filesystem="FAT32");
enum class SdFormatState { Idle, Pending, Formatting, Complete, Failed };
SdFormatState sdFormatState();
bool sdFormatPromptNeeded();
void dismissSdFormatPrompt();
bool requestSdMount(bool mount, String& error);
bool sdStorageEjected();
bool remountActiveStorageBackend(StorageTarget target);
bool remountStorageBackend(StorageTarget target, const SettingsBundle& settings);
StorageTarget parseStorageTarget(const String& rawTarget);
const char* storageTargetId(StorageTarget target);
const char* storageTargetLabel(StorageTarget target);
StorageBackendSummary getStorageSummary(StorageTarget target);
void beginStorageWrite(StorageTarget target);
void endStorageWrite(StorageTarget target);
void beginStorageRead(StorageTarget target);
void endStorageRead(StorageTarget target);
bool storageBusy(StorageTarget target);
fs::FS* getStorageFs(StorageTarget target);
bool storageMounted(StorageTarget target);
bool storageConfigured(StorageTarget target);
bool storageExists(StorageTarget target, const String& path);
File storageOpen(StorageTarget target, const String& path, const char* mode = "r");
bool storageRemove(StorageTarget target, const String& path);
bool sdStorageUsesPin(uint8_t pin);
