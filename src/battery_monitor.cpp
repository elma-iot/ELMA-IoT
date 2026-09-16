#include "device_log.h"
#include "battery_monitor.h"

namespace {
constexpr uint16_t kBatteryWindowLimit = 32;
constexpr float kChargingRiseThresholdVolts = 0.01f;
constexpr float kChargingFallThresholdVolts = 0.01f;

bool batteryDebugEnabled() {
#if defined(CORE_DEBUG_LEVEL) && CORE_DEBUG_LEVEL > 0
    return true;
#else
    return false;
#endif
}

uint16_t normalizeWindowSize(uint16_t size) {
    if (size < 1) {
        return 1;
    }
    if (size > kBatteryWindowLimit) {
        return kBatteryWindowLimit;
    }
    return size;
}
}  // namespace

void BatteryMonitor::begin(const BatterySettings& settings, uint8_t adcPin, AppState& appState) {
    appState_ = &appState;
    analogReadResolution(12);
    applySettings(settings, adcPin);
}

void BatteryMonitor::applySettings(const BatterySettings& settings, uint8_t adcPin) {
    adcPin_ = adcPin;
    settings_ = settings;
    chargingSensePin_ = settings_.chargingSensePin;
    if (adcPin_ > 0) {
        pinMode(adcPin_, INPUT);
        analogSetPinAttenuation(adcPin_, ADC_11db);
    }
    if (chargingSensePin_ > 0 && chargingSensePin_ != adcPin_ && adcPin_ > 0) {
        pinMode(chargingSensePin_, INPUT_PULLUP);
    }
    resetFilterState();
    latest_ = adcPin_ > 0 ? sampleNow() : BatteryReading{};
    if (appState_ != nullptr) {
        appState_->setBattery(latest_.filteredVoltage, latest_.rawAdcVoltage, latest_.rawAdc, latest_.charging);
    }
    lastSampleAt_ = millis();
}

void BatteryMonitor::resetFilterState() {
    movingAverageSum_ = 0.0f;
    movingAverageCount_ = 0;
    movingAverageIndex_ = 0;
    lastTrendVoltage_ = 0.0f;
    chargingState_ = false;
    chargingStateInitialized_ = false;
    for (uint16_t index = 0; index < kMaxWindowSize; ++index) {
        movingAverageSamples_[index] = 0.0f;
    }
}

BatteryReading BatteryMonitor::sampleNow() {
    if (adcPin_ == 0) {
        return BatteryReading{};
    }

    const uint16_t raw = static_cast<uint16_t>(analogRead(adcPin_));
    const float rawVoltage = (static_cast<float>(raw) / 4095.0f) * 3.3f;
    const float correctedVoltage = rawVoltage * settings_.calibrationMultiplier;

    const uint16_t windowSize = normalizeWindowSize(settings_.movingAverageWindowSize);
    if (movingAverageCount_ < windowSize) {
        movingAverageSamples_[movingAverageIndex_] = correctedVoltage;
        movingAverageSum_ += correctedVoltage;
        ++movingAverageCount_;
    } else {
        movingAverageSum_ -= movingAverageSamples_[movingAverageIndex_];
        movingAverageSamples_[movingAverageIndex_] = correctedVoltage;
        movingAverageSum_ += correctedVoltage;
    }
    movingAverageIndex_ = (movingAverageIndex_ + 1U) % windowSize;

    const float filteredVoltage = movingAverageCount_ == 0 ? correctedVoltage : (movingAverageSum_ / movingAverageCount_);

    if (chargingSensePin_ > 0 && chargingSensePin_ != adcPin_) {
        chargingState_ = digitalRead(chargingSensePin_) == LOW;
        chargingStateInitialized_ = true;
        lastTrendVoltage_ = filteredVoltage;
    } else {
        if (!chargingStateInitialized_) {
            lastTrendVoltage_ = filteredVoltage;
            chargingStateInitialized_ = true;
        } else {
            const float delta = filteredVoltage - lastTrendVoltage_;
            if (delta >= kChargingRiseThresholdVolts) {
                chargingState_ = true;
            } else if (delta <= -kChargingFallThresholdVolts) {
                chargingState_ = false;
            }
            lastTrendVoltage_ = filteredVoltage;
        }
    }

    if (batteryDebugEnabled()) {
        DebugLog.printf("[battery] raw=%u raw_v=%.3f corrected_v=%.3f filtered_v=%.3f window=%u pin=%u charge_pin=%u charging=%s\n",
                      raw,
                      rawVoltage,
                      correctedVoltage,
                      filteredVoltage,
                      windowSize,
                      adcPin_,
                      chargingSensePin_,
                      chargingState_ ? "yes" : "no");
    }

    BatteryReading reading;
    reading.filteredVoltage = filteredVoltage;
    reading.rawAdcVoltage = rawVoltage;
    reading.rawAdc = raw;
    reading.charging = chargingState_;
    return reading;
}

bool BatteryMonitor::loop(bool samplingAllowed) {
    if (adcPin_ == 0) {
        return false;
    }

    const unsigned long now = millis();
    if (now - lastSampleAt_ < settings_.updateIntervalMs) {
        return false;
    }

    if (!samplingAllowed) {
        return false;
    }

    lastSampleAt_ = now;
    latest_ = sampleNow();
    if (appState_ != nullptr) {
        appState_->setBattery(latest_.filteredVoltage, latest_.rawAdcVoltage, latest_.rawAdc, latest_.charging);
    }
    return true;
}

BatteryReading BatteryMonitor::latest() const {
    return latest_;
}

bool BatteryMonitor::enabled() const {
    return adcPin_ > 0;
}
