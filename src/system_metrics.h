#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

struct ResourceMetricSnapshot {
    bool available = false;
    bool mounted = false;
    uint64_t cardSizeBytes = 0;
    uint64_t totalBytes = 0;
    uint64_t usedBytes = 0;
    uint64_t freeBytes = 0;
};

struct HardwareInfoSnapshot {
    String chipModel;
    String boardProfile;
    uint16_t chipRevision = 0;
    uint8_t cpuCores = 0;
    uint32_t cpuFreqMHz = 0;
    uint32_t flashSizeBytes = 0;
    uint32_t appPartitionSizeBytes = 0;
    uint32_t sketchSizeBytes = 0;
};

struct SystemMetricsSnapshot {
    bool cpuLoadAvailable = false;
    uint8_t cpuLoadPercent = 0;
    uint8_t cpuLoadCoreCount = 0;
    uint8_t cpuLoadCorePercent[2] = {0, 0};
    bool chipTemperatureAvailable = false;
    float chipTemperatureC = 0.0f;
    uint32_t chipTemperatureSampledAt = 0;
    uint32_t freeHeapBytes = 0;
    uint32_t minFreeHeapBytes = 0;
    uint32_t largestHeapBlockBytes = 0;
    ResourceMetricSnapshot sram;
    ResourceMetricSnapshot psram;
    ResourceMetricSnapshot spiffs;
    ResourceMetricSnapshot sd;
    HardwareInfoSnapshot hardware;
};

void beginSystemMetrics();
void sampleCpuLoadMetrics();
void sampleSystemMetrics();
SystemMetricsSnapshot getSystemMetricsSnapshot();
void appendSystemMetricsJson(JsonObject root);
