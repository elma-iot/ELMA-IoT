#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class DeviceLogger : public Print {
public:
    static constexpr size_t InternalBootBytes = 2048;
    static constexpr size_t ExternalBytes = 10 * 1024 * 1024;
    static constexpr size_t ViewBytes = 16 * 1024;
    void begin();
    void service(bool force = false);
    bool snapshot(JsonObject out, const String& since);
    size_t write(uint8_t byte) override { return write(&byte, 1); }
    size_t write(const uint8_t* data, size_t size) override;
    using Print::write;
    void capture(const char* data, size_t size);
private:
    static constexpr size_t RingBytes = 4096;
    char ring_[RingBytes] = {};
    char previous_[InternalBootBytes + 1] = {};
    size_t checkpointLimit_ = InternalBootBytes;
    char bootHeader_[160] = {};
    uint64_t sequence_ = 0;
    uint64_t externalSequence_ = 0;
    uint64_t savedSequence_ = 0;
    uint32_t bootId_ = 0;
    unsigned long lastServiceAt_ = 0;
    unsigned long lastCheckpointAt_ = 0;
    unsigned long nextSdRetryAt_ = 0;
    SemaphoreHandle_t ioMutex_ = nullptr;
    portMUX_TYPE ringMux_ = portMUX_INITIALIZER_UNLOCKED;
    bool started_ = false;
    bool nvsReady_ = false;
    bool externalActive_ = false;
    uint8_t slot_ = 0;
    String storageError_;
    String tail(size_t limit, uint64_t* end = nullptr, uint64_t after = 0);
    String currentBoot(uint64_t* end = nullptr);
};

extern DeviceLogger DebugLog;
