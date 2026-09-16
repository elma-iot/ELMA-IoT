#pragma once

#include <stdint.h>
#include <math.h>

#if defined(CONFIG_IDF_TARGET_ESP32)
extern "C" uint8_t temprature_sens_read();
#endif

// Retain a recent valid sample across failed conversions, but never turn a
// stopped sensor into a permanently live temperature. No smoothing or clamping.
class ChipTemperature {
public:
    static constexpr uint32_t maxAgeMs = 30000;

    void sample(uint32_t now) {
        float value = NAN;
#if defined(CONFIG_IDF_TARGET_ESP32)
        const uint8_t raw = temprature_sens_read();
        // ESPHome's classic ESP32 reader also rejects raw 128. It is returned
        // intermittently on this device and must not become a 53.3 C reading.
        if (raw != 128) value = (static_cast<int>(raw) - 32) / 1.8f;
#elif defined(ARDUINO_ARCH_ESP32)
        value = temperatureRead();
#endif
        if (isfinite(value)) {
            valueC_ = value;
            sampledAt_ = now;
            hasSample_ = true;
        }
    }

    bool available(uint32_t now) const { return hasSample_ && ageMs(now) < maxAgeMs; }
    uint32_t ageMs(uint32_t now) const { return now - sampledAt_; }
    float valueC() const { return valueC_; }

private:
    bool hasSample_ = false;
    uint32_t sampledAt_ = 0;
    float valueC_ = 0;
};
