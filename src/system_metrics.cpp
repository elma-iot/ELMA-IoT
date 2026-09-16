#include "system_metrics.h"
#include "chip_temperature.h"

#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_freertos_hooks.h>
#include <freertos/task.h>

#include "storage_backend.h"

namespace {
SystemMetricsSnapshot metricsSnapshot;
ChipTemperature chipTemperature;
bool metricsInitialized = false;
TaskHandle_t idleTaskHandles[portNUM_PROCESSORS] = {nullptr};
volatile uint32_t sampledTickCounts[portNUM_PROCESSORS] = {0};
volatile uint32_t idleTickCounts[portNUM_PROCESSORS] = {0};
uint32_t lastSampledTickCounts[portNUM_PROCESSORS] = {0};
uint32_t lastIdleTickCounts[portNUM_PROCESSORS] = {0};
bool tickHookRegistered[portNUM_PROCESSORS] = {false};

#ifndef APP_BOARD_PROFILE
#define APP_BOARD_PROFILE ""
#endif

#ifndef APP_COMPILED_BOARD_PROFILE_ID
#define APP_COMPILED_BOARD_PROFILE_ID 0
#endif

const char* compiledBoardProfile() {
#if APP_COMPILED_BOARD_PROFILE_ID == 1
    return "esp32-s3-super-mini";
#elif APP_COMPILED_BOARD_PROFILE_ID == 2
    return "esp32-s3-zero";
#elif APP_COMPILED_BOARD_PROFILE_ID == 3
    return "esp32-s3-psram";
#elif APP_COMPILED_BOARD_PROFILE_ID == 4
    return "esp32-spk-n16r8";
#elif APP_COMPILED_BOARD_PROFILE_ID == 5
    return "esp32-s3-devkit-c1";
#elif APP_COMPILED_BOARD_PROFILE_ID == 6
    return "esp32-s3-cam-module";
#elif APP_COMPILED_BOARD_PROFILE_ID == 7
    return "esp32-wrover";
#elif APP_COMPILED_BOARD_PROFILE_ID == 8
    return "esp32-wroom";
#elif APP_COMPILED_BOARD_PROFILE_ID == 9
    return "esp32-mini";
#elif APP_COMPILED_BOARD_PROFILE_ID == 10
    return "wemos-lolin32-mini";
#elif APP_COMPILED_BOARD_PROFILE_ID == 11
    return "esp32-c3";
#else
    return APP_BOARD_PROFILE;
#endif
}

uint64_t clampUsedBytes(uint64_t totalBytes, uint64_t freeBytes) {
    return totalBytes > freeBytes ? totalBytes - freeBytes : 0;
}

// Tick hooks can run while the SPI flash cache is disabled. Keep the complete
// application-side callback in IRAM and touch only DRAM-backed counters. The
// FreeRTOS current-task accessor is linked into IRAM by ESP-IDF.
void IRAM_ATTR tickHookCpu0() {
    ++sampledTickCounts[0];
    if (idleTaskHandles[0] != nullptr && xTaskGetCurrentTaskHandleForCPU(0) == idleTaskHandles[0]) {
        ++idleTickCounts[0];
    }
}

#if portNUM_PROCESSORS > 1
void IRAM_ATTR tickHookCpu1() {
    ++sampledTickCounts[1];
    if (idleTaskHandles[1] != nullptr && xTaskGetCurrentTaskHandleForCPU(1) == idleTaskHandles[1]) {
        ++idleTickCounts[1];
    }
}
#endif

void sampleCpuLoad(SystemMetricsSnapshot& snapshot) {
    uint32_t aggregateLoadPercent = 0;
    uint8_t sampledCoreCount = 0;

    for (size_t cpuIndex = 0; cpuIndex < portNUM_PROCESSORS; ++cpuIndex) {
        const uint32_t sampledTicks = sampledTickCounts[cpuIndex];
        const uint32_t idleTicks = idleTickCounts[cpuIndex];
        const uint32_t sampledDelta = sampledTicks - lastSampledTickCounts[cpuIndex];
        const uint32_t idleDelta = idleTicks - lastIdleTickCounts[cpuIndex];
        lastSampledTickCounts[cpuIndex] = sampledTicks;
        lastIdleTickCounts[cpuIndex] = idleTicks;

        if (!tickHookRegistered[cpuIndex] || sampledDelta == 0) {
            continue;
        }

        const uint32_t boundedIdleDelta = min<uint32_t>(idleDelta, sampledDelta);
        const uint8_t coreLoadPercent = static_cast<uint8_t>(
            100U - ((boundedIdleDelta * 100U + sampledDelta / 2U) / sampledDelta));
        if (cpuIndex < 2) {
            snapshot.cpuLoadCorePercent[cpuIndex] = coreLoadPercent;
        }
        aggregateLoadPercent += coreLoadPercent;
        ++sampledCoreCount;
    }

    if (sampledCoreCount == 0) {
        return;
    }

    snapshot.cpuLoadAvailable = true;
    snapshot.cpuLoadCoreCount = min<uint8_t>(sampledCoreCount, 2);
    snapshot.cpuLoadPercent = static_cast<uint8_t>(
        (aggregateLoadPercent + sampledCoreCount / 2U) / sampledCoreCount);
}

void populateStaticHardwareInfo(HardwareInfoSnapshot& hardware) {
    hardware.chipModel = ESP.getChipModel();
    hardware.boardProfile = compiledBoardProfile();
    hardware.chipRevision = ESP.getChipRevision();
    hardware.cpuCores = ESP.getChipCores();
    hardware.cpuFreqMHz = ESP.getCpuFreqMHz();
    hardware.flashSizeBytes = ESP.getFlashChipSize();
    hardware.sketchSizeBytes = ESP.getSketchSize();

    const esp_partition_t* runningPartition = esp_ota_get_running_partition();
    hardware.appPartitionSizeBytes = runningPartition != nullptr ? static_cast<uint32_t>(runningPartition->size) : 0;
}

void assignResourceSummary(ResourceMetricSnapshot& destination, const StorageBackendSummary& source) {
    destination.available = source.available;
    destination.mounted = source.mounted;
    destination.cardSizeBytes = source.cardSizeBytes;
    destination.totalBytes = source.totalBytes;
    destination.usedBytes = source.usedBytes;
    destination.freeBytes = source.freeBytes;
}

void populateStorageMetrics(SystemMetricsSnapshot& snapshot) {
    snapshot.spiffs = {};
    snapshot.sd = {};
    assignResourceSummary(snapshot.spiffs, getStorageSummary(StorageTarget::Flash));
    assignResourceSummary(snapshot.sd, getStorageSummary(StorageTarget::Sd));
}
}  // namespace

void beginSystemMetrics() {
    if (metricsInitialized) {
        return;
    }

    metricsSnapshot = {};
    populateStaticHardwareInfo(metricsSnapshot.hardware);

    idleTaskHandles[0] = xTaskGetIdleTaskHandleForCPU(0);
    tickHookRegistered[0] = idleTaskHandles[0] != nullptr &&
        esp_register_freertos_tick_hook_for_cpu(tickHookCpu0, 0) == ESP_OK;
#if portNUM_PROCESSORS > 1
    idleTaskHandles[1] = xTaskGetIdleTaskHandleForCPU(1);
    tickHookRegistered[1] = idleTaskHandles[1] != nullptr &&
        esp_register_freertos_tick_hook_for_cpu(tickHookCpu1, 1) == ESP_OK;
#endif

    metricsInitialized = true;
    sampleSystemMetrics();
}

void sampleSystemMetrics() {
    if (!metricsInitialized) {
        beginSystemMetrics();
    }

    SystemMetricsSnapshot next = metricsSnapshot;
    // CPU frequency is governed at runtime, so unlike the other hardware
    // fields it must be refreshed instead of remaining at its boot value.
    next.hardware.cpuFreqMHz = ESP.getCpuFreqMHz();
    const uint32_t sampledAt = millis();
    chipTemperature.sample(sampledAt);
    next.chipTemperatureC = chipTemperature.valueC();
    next.chipTemperatureAvailable = chipTemperature.available(sampledAt);
    next.chipTemperatureSampledAt = sampledAt - chipTemperature.ageMs(sampledAt);
    next.freeHeapBytes = ESP.getFreeHeap();
    next.minFreeHeapBytes = ESP.getMinFreeHeap();
    next.largestHeapBlockBytes = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);

    next.sram.available = true;
    next.sram.totalBytes = ESP.getHeapSize();
    next.sram.freeBytes = next.freeHeapBytes;
    next.sram.usedBytes = clampUsedBytes(next.sram.totalBytes, next.sram.freeBytes);

    const bool psramAvailable = psramFound();
    next.psram.available = psramAvailable;
    next.psram.mounted = psramAvailable;
    next.psram.totalBytes = psramAvailable ? ESP.getPsramSize() : 0;
    next.psram.freeBytes = psramAvailable ? ESP.getFreePsram() : 0;
    next.psram.usedBytes = clampUsedBytes(next.psram.totalBytes, next.psram.freeBytes);

    populateStorageMetrics(next);
    metricsSnapshot = next;
}

void sampleCpuLoadMetrics() {
    if (!metricsInitialized) {
        beginSystemMetrics();
    }

    SystemMetricsSnapshot next = metricsSnapshot;
    next.hardware.cpuFreqMHz = ESP.getCpuFreqMHz();
    sampleCpuLoad(next);
    metricsSnapshot = next;
}

SystemMetricsSnapshot getSystemMetricsSnapshot() {
    return metricsSnapshot;
}

void appendSystemMetricsJson(JsonObject root) {
    const SystemMetricsSnapshot snapshot = getSystemMetricsSnapshot();

    JsonObject hardware = root["hardware"].to<JsonObject>();
    hardware["chipModel"] = snapshot.hardware.chipModel;
    hardware["boardProfile"] = snapshot.hardware.boardProfile;
    hardware["chipRevision"] = snapshot.hardware.chipRevision;
    hardware["cpuCores"] = snapshot.hardware.cpuCores;
    hardware["cpuFreqMHz"] = snapshot.hardware.cpuFreqMHz;
    hardware["flashSizeBytes"] = snapshot.hardware.flashSizeBytes;
    hardware["appPartitionSizeBytes"] = snapshot.hardware.appPartitionSizeBytes;
    hardware["sketchSizeBytes"] = snapshot.hardware.sketchSizeBytes;

    JsonObject system = root["system"].to<JsonObject>();
    system["freeHeap"] = snapshot.freeHeapBytes;
    system["minFreeHeapBytes"] = snapshot.minFreeHeapBytes;
    system["largestHeapBlockBytes"] = snapshot.largestHeapBlockBytes;
    system["cpuLoadAvailable"] = snapshot.cpuLoadAvailable;
    system["cpuLoadPercent"] = snapshot.cpuLoadPercent;
    JsonArray cpuLoadCores = system["cpuLoadCorePercent"].to<JsonArray>();
    for (uint8_t coreIndex = 0; coreIndex < snapshot.cpuLoadCoreCount; ++coreIndex) {
        cpuLoadCores.add(snapshot.cpuLoadCorePercent[coreIndex]);
    }
    system["chipTemperatureAvailable"] = snapshot.chipTemperatureAvailable;
#if defined(CONFIG_IDF_TARGET_ESP32)
    system["chipTemperatureEstimated"] = true;
#else
    system["chipTemperatureEstimated"] = false;
#endif
    system["chipTemperatureReason"] = snapshot.chipTemperatureAvailable ? "" : "Waiting for a valid internal sensor reading; invalid or expired samples are not displayed.";
    system["chipTemperatureAgeMs"] = snapshot.chipTemperatureAvailable ? millis() - snapshot.chipTemperatureSampledAt : 0;
    if (snapshot.chipTemperatureAvailable) {
        system["chipTemperatureC"] = snapshot.chipTemperatureC;
    } else {
        system["chipTemperatureC"] = nullptr;
    }

    JsonObject sram = system["sram"].to<JsonObject>();
    sram["available"] = snapshot.sram.available;
    sram["mounted"] = snapshot.sram.mounted;
    sram["cardSizeBytes"] = snapshot.sram.cardSizeBytes;
    sram["totalBytes"] = snapshot.sram.totalBytes;
    sram["usedBytes"] = snapshot.sram.usedBytes;
    sram["freeBytes"] = snapshot.sram.freeBytes;

    JsonObject psram = system["psram"].to<JsonObject>();
    psram["available"] = snapshot.psram.available;
    psram["mounted"] = snapshot.psram.mounted;
    psram["cardSizeBytes"] = snapshot.psram.cardSizeBytes;
    psram["totalBytes"] = snapshot.psram.totalBytes;
    psram["usedBytes"] = snapshot.psram.usedBytes;
    psram["freeBytes"] = snapshot.psram.freeBytes;

    JsonObject spiffs = system["spiffs"].to<JsonObject>();
    spiffs["available"] = snapshot.spiffs.available;
    spiffs["mounted"] = snapshot.spiffs.mounted;
    spiffs["cardSizeBytes"] = snapshot.spiffs.cardSizeBytes;
    spiffs["totalBytes"] = snapshot.spiffs.totalBytes;
    spiffs["usedBytes"] = snapshot.spiffs.usedBytes;
    spiffs["freeBytes"] = snapshot.spiffs.freeBytes;

    JsonObject sd = system["sd"].to<JsonObject>();
    sd["available"] = snapshot.sd.available;
    sd["mounted"] = snapshot.sd.mounted;
    sd["cardSizeBytes"] = snapshot.sd.cardSizeBytes;
    sd["totalBytes"] = snapshot.sd.totalBytes;
    sd["usedBytes"] = snapshot.sd.usedBytes;
    sd["freeBytes"] = snapshot.sd.freeBytes;
}
