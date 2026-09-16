#include "device_log.h"
#include "motor_control.h"

#include <ArduinoJson.h>
#include <driver/gpio.h>

namespace {
constexpr uint32_t kDefaultRunDurationMs = 5000;
constexpr uint32_t kMinimumRunDurationMs = 100;
constexpr uint32_t kMaximumRunDurationMs = 600000;

const char* stopReasonName(MotorController::StopReason reason) {
    switch (reason) {
        case MotorController::StopReason::DurationLimit:
            return "time_limit_reached";
        case MotorController::StopReason::LimitSwitch:
            return "end_switch_activated";
        case MotorController::StopReason::Manual:
            return "manual_stop";
        case MotorController::StopReason::None:
        default:
            return "idle";
    }
}

const char* directionName(bool forward) {
    return forward ? "forward" : "backward";
}

String statusTextFor(bool active, bool forward, bool lastDirectionForward, MotorController::StopReason stopReason, uint32_t stopAtMs, uint32_t now) {
    if (active) {
        const uint32_t remainingMs = stopAtMs > now ? stopAtMs - now : 0;
        return String(directionName(forward)) + " running, " + String(remainingMs) + " ms left";
    }
    switch (stopReason) {
        case MotorController::StopReason::DurationLimit:
            return String(directionName(lastDirectionForward)) + " stopped, time limit reached";
        case MotorController::StopReason::LimitSwitch:
            return String(directionName(lastDirectionForward)) + " stopped, end switch activated";
        case MotorController::StopReason::Manual:
            return String(directionName(lastDirectionForward)) + " stopped";
        case MotorController::StopReason::None:
        default:
            return String("idle");
    }
}

String peripheralControlProfileFromUi(const SettingsBundle& settings, size_t index) {
    JsonDocument uiDoc;
    deserializeJson(uiDoc, settings.ui.peripheralProfileSelections.isEmpty() ? String("{}") : settings.ui.peripheralProfileSelections);
    JsonArray controls = uiDoc["controls"].as<JsonArray>();
    if (controls.isNull() || index >= controls.size()) {
        return String("none");
    }
    return String(static_cast<const char*>(controls[index] | "none"));
}

String peripheralHelperValue(const SettingsBundle& settings, const String& slotKey, const char* signalKey) {
    if (signalKey == nullptr || *signalKey == '\0') {
        return String();
    }
    JsonDocument bindingsDoc;
    deserializeJson(bindingsDoc, settings.ui.peripheralHelperBindings.isEmpty() ? String("{}") : settings.ui.peripheralHelperBindings);
    JsonVariant slot = bindingsDoc[slotKey];
    if (slot.isNull() || !slot.is<JsonObjectConst>()) {
        return String();
    }
    return String(static_cast<const char*>(slot[signalKey] | ""));
}

int8_t pinFromHelperValue(const SettingsBundle& settings, size_t controlIndex, const char* signalKey) {
    const String slotKey = String("control:") + String(controlIndex);
    const String rawValue = peripheralHelperValue(settings, slotKey, signalKey);
    if (rawValue.isEmpty()) {
        return -1;
    }
    const long numericValue = rawValue.toInt();
    return numericValue >= 0 && numericValue <= 127 ? static_cast<int8_t>(numericValue) : static_cast<int8_t>(-1);
}

bool isUnsafeMotorPin(int8_t pin) {
    if (pin < 0) {
        return true;
    }
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    switch (pin) {
        case 0:
        case 19:
        case 20:
        case 43:
        case 44:
        case 45:
        case 46:
            return true;
        default:
            return false;
    }
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
    return (pin >= 12 && pin <= 19) || pin == 2 || pin == 8 || pin == 9 || pin > 21;
#elif defined(CONFIG_IDF_TARGET_ESP32)
    switch (pin) {
        case 0:
        case 1:
        case 2:
        case 3:
            return true;
        default:
            return false;
    }
#else
    return false;
#endif
}

int8_t validatedMotorPin(int8_t pin, const char* signalKey) {
    if (!isUnsafeMotorPin(pin)) {
        return pin;
    }
    if (pin >= 0) {
        DebugLog.printf("[motor] ignoring unsafe %s assignment on GPIO%d\n", signalKey != nullptr ? signalKey : "signal", static_cast<int>(pin));
    }
    return -1;
}

uint32_t clampDurationMs(uint32_t durationMs) {
    if (durationMs < kMinimumRunDurationMs) {
        return kDefaultRunDurationMs;
    }
    return durationMs > kMaximumRunDurationMs ? kMaximumRunDurationMs : durationMs;
}
}

MotorController::MotorController() = default;

void MotorController::begin(LimitInputReader limitInputReader) {
    limitInputReader_ = limitInputReader;
}

void MotorController::releaseConfiguredPins() {
    for (ChannelPins& channel : configuredChannels_) {
        if (channel.forwardPin >= 0) {
            digitalWrite(channel.forwardPin, LOW);
            pinMode(channel.forwardPin, OUTPUT);
            gpio_hold_dis(static_cast<gpio_num_t>(channel.forwardPin));
            digitalWrite(channel.forwardPin, LOW);
            pinMode(channel.forwardPin, INPUT);
        }
        if (channel.backwardPin >= 0) {
            digitalWrite(channel.backwardPin, LOW);
            pinMode(channel.backwardPin, OUTPUT);
            gpio_hold_dis(static_cast<gpio_num_t>(channel.backwardPin));
            digitalWrite(channel.backwardPin, LOW);
            pinMode(channel.backwardPin, INPUT);
        }
    }
}

void MotorController::configurePins() {
    for (const ChannelPins& channel : configuredChannels_) {
        if (channel.forwardPin >= 0) {
            digitalWrite(channel.forwardPin, LOW);
            pinMode(channel.forwardPin, OUTPUT);
            gpio_hold_dis(static_cast<gpio_num_t>(channel.forwardPin));
            digitalWrite(channel.forwardPin, LOW);
        }
        if (channel.backwardPin >= 0) {
            digitalWrite(channel.backwardPin, LOW);
            pinMode(channel.backwardPin, OUTPUT);
            gpio_hold_dis(static_cast<gpio_num_t>(channel.backwardPin));
            digitalWrite(channel.backwardPin, LOW);
        }
    }
}

void MotorController::prepareForRestart() {
    stopAllChannels();
    for (const ChannelPins& channel : configuredChannels_) {
        const int8_t pins[] = {channel.forwardPin, channel.backwardPin};
        for (const int8_t pin : pins) {
            if (pin < 0) {
                continue;
            }
            digitalWrite(pin, LOW);
            pinMode(pin, OUTPUT);
            digitalWrite(pin, LOW);
            gpio_hold_en(static_cast<gpio_num_t>(pin));
        }
    }
}

void MotorController::applySettings(const SettingsBundle& settings) {
    ChannelPins nextChannels[2] = {};
    const auto channelPinsEqual = [](const ChannelPins& left, const ChannelPins& right) {
        return left.forwardPin == right.forwardPin && left.backwardPin == right.backwardPin;
    };
    const auto buildChannelPins = [&](size_t controlIndex, const char* forwardSignalKey, const char* backwardSignalKey) {
        ChannelPins channel;
        channel.forwardPin = validatedMotorPin(pinFromHelperValue(settings, controlIndex, forwardSignalKey), forwardSignalKey);
        channel.backwardPin = validatedMotorPin(pinFromHelperValue(settings, controlIndex, backwardSignalKey), backwardSignalKey);
        return channel;
    };

    size_t drv8833ControlIndex = SIZE_MAX;
    for (size_t index = 0; index < 16; ++index) {
        const String profile = peripheralControlProfileFromUi(settings, index);
        if (profile.equalsIgnoreCase("drv8833-dual-motor-driver")) {
            drv8833ControlIndex = index;
            break;
        }
        if (profile == "none" && index > 0) {
            break;
        }
    }

    if (drv8833ControlIndex != SIZE_MAX) {
        nextChannels[0] = buildChannelPins(drv8833ControlIndex, "IN1", "IN2");
        nextChannels[1] = buildChannelPins(drv8833ControlIndex, "IN3", "IN4");
    }

    if (channelPinsEqual(configuredChannels_[0], nextChannels[0]) && channelPinsEqual(configuredChannels_[1], nextChannels[1])) {
        // Runtime settings application initializes button fallbacks before it
        // reaches the motor controller. A disabled button can share a legacy
        // default GPIO with a configured bridge input, so restore OUTPUT mode
        // even when the motor mapping itself did not change.
        configurePins();
        return;
    }

    stopAllChannels();
    releaseConfiguredPins();
    configuredChannels_[0] = nextChannels[0];
    configuredChannels_[1] = nextChannels[1];
    configurePins();
}

void MotorController::reclaimConfiguredPins() {
    // Input initialization may touch a disabled input's legacy/default GPIO.
    // Reassert the configured bridge pins afterward so a default button pin
    // cannot silently turn one DRV8833 direction back into an input.
    configurePins();
}

bool MotorController::available() const {
    return configuredChannels_[0].configured() || configuredChannels_[1].configured();
}

bool MotorController::limitInputActive(int8_t limitInputIndex) const {
    return limitInputReader_ && limitInputIndex >= 0 ? limitInputReader_(limitInputIndex) : false;
}

void MotorController::stopChannel(uint8_t channelIndex, StopReason reason) {
    if (channelIndex >= 2) {
        return;
    }
    const ChannelPins& channelPins = configuredChannels_[channelIndex];
    ChannelRuntime& runtime = runtimeChannels_[channelIndex];
    if (channelPins.forwardPin >= 0) {
        digitalWrite(channelPins.forwardPin, LOW);
    }
    if (channelPins.backwardPin >= 0) {
        digitalWrite(channelPins.backwardPin, LOW);
    }
    if (runtime.active) {
        runtime.active = false;
        runtime.stopAtMs = 0;
        runtime.limitInputIndex = -1;
        runtime.lastStopReason = reason;
        stateVersion_ += 1;
    }
}

void MotorController::stopAllChannels() {
    stopChannel(0);
    stopChannel(1);
}

bool MotorController::runChannel(uint8_t channelIndex, bool forward, uint32_t durationMs, int8_t limitInputIndex, String& error) {
    if (!available()) {
        error = "DRV8833 control is not configured.";
        return false;
    }

    if (channelIndex >= 2) {
        error = "Unknown motor channel.";
        return false;
    }

    const ChannelPins& channelPins = configuredChannels_[channelIndex];
    if (!channelPins.configured()) {
        error = "Selected motor channel is not wired.";
        return false;
    }

    if (limitInputIndex >= 0 && limitInputActive(limitInputIndex)) {
        error = "Selected limit switch is already active.";
        return false;
    }

    if (runtimeChannels_[channelIndex].active) {
        stopChannel(channelIndex, StopReason::Manual);
    }
    const int8_t activePin = forward ? channelPins.forwardPin : channelPins.backwardPin;
    const int8_t inactivePin = forward ? channelPins.backwardPin : channelPins.forwardPin;
    digitalWrite(inactivePin, LOW);
    delayMicroseconds(20);
    digitalWrite(activePin, HIGH);
    delayMicroseconds(5);
    if (digitalRead(activePin) != HIGH) {
        digitalWrite(activePin, LOW);
        error = String("Motor GPIO") + String(activePin) + " did not reach HIGH; check wiring or a short circuit.";
        DebugLog.printf("[motor] rejected channel=%u direction=%s: GPIO%d remained LOW\n",
                      static_cast<unsigned>(channelIndex),
                      directionName(forward),
                      static_cast<int>(activePin));
        return false;
    }

    DebugLog.printf("[motor] channel=%u direction=%s active=GPIO%d inactive=GPIO%d duration=%lu ms\n",
                  static_cast<unsigned>(channelIndex),
                  directionName(forward),
                  static_cast<int>(activePin),
                  static_cast<int>(inactivePin),
                  static_cast<unsigned long>(clampDurationMs(durationMs)));

    ChannelRuntime& runtime = runtimeChannels_[channelIndex];
    runtime.active = true;
    runtime.forward = forward;
    runtime.lastDirectionForward = forward;
    runtime.stopAtMs = millis() + clampDurationMs(durationMs);
    runtime.limitInputIndex = limitInputIndex;
    runtime.lastStopReason = StopReason::None;
    stateVersion_ += 1;
    error = "";
    return true;
}

void MotorController::loop() {
    if (!available()) {
        return;
    }

    const uint32_t now = millis();
    for (uint8_t channelIndex = 0; channelIndex < 2; ++channelIndex) {
        const ChannelRuntime& runtime = runtimeChannels_[channelIndex];
        if (!runtime.active) {
            continue;
        }
        const bool durationElapsed = static_cast<int32_t>(now - runtime.stopAtMs) >= 0;
        const bool limitTriggered = limitInputActive(runtime.limitInputIndex);
        if (durationElapsed || limitTriggered) {
            stopChannel(channelIndex, limitTriggered ? StopReason::LimitSwitch : StopReason::DurationLimit);
        }
    }
}

void MotorController::appendStatus(JsonObject root) const {
    root["available"] = available();
    if (!available()) {
        return;
    }

    JsonArray channels = root["channels"].to<JsonArray>();
    const uint32_t now = millis();
    for (uint8_t channelIndex = 0; channelIndex < 2; ++channelIndex) {
        const ChannelPins& pins = configuredChannels_[channelIndex];
        const ChannelRuntime& runtime = runtimeChannels_[channelIndex];
        JsonObject channel = channels.add<JsonObject>();
        channel["name"] = channelIndex == 0 ? "A" : "B";
        channel["configured"] = pins.configured();
        channel["active"] = runtime.active;
        channel["direction"] = runtime.active ? (runtime.forward ? "forward" : "backward") : "stopped";
        channel["lastDirection"] = directionName(runtime.lastDirectionForward);
        channel["stopReason"] = stopReasonName(runtime.lastStopReason);
        channel["forwardPin"] = pins.forwardPin;
        channel["backwardPin"] = pins.backwardPin;
        channel["limitInputIndex"] = runtime.limitInputIndex;
        channel["remainingMs"] = runtime.active && runtime.stopAtMs > now ? runtime.stopAtMs - now : 0;
        channel["statusText"] = statusTextFor(runtime.active, runtime.forward, runtime.lastDirectionForward, runtime.lastStopReason, runtime.stopAtMs, now);
    }
}

uint32_t MotorController::stateVersion() const {
    return stateVersion_;
}
