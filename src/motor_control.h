#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>

#include "settings_manager.h"

class MotorController {
  public:
    using LimitInputReader = std::function<bool(int8_t)>;
    enum class StopReason : uint8_t {
        None = 0,
        Manual,
        DurationLimit,
        LimitSwitch,
    };

    MotorController();

    void begin(LimitInputReader limitInputReader);
    void applySettings(const SettingsBundle& settings);
    void reclaimConfiguredPins();
    void prepareForRestart();
    void loop();
    bool available() const;
    int driverSlot() const {return driverSlot_;}
    bool runChannel(uint8_t channelIndex, bool forward, uint32_t durationMs, int8_t limitInputIndex, String& error);
    bool movePtz(uint8_t panChannel,uint8_t tiltChannel,int pan,int tilt,uint32_t durationMs,int8_t panLimit,int8_t tiltLimit,String& error);
    void appendStatus(JsonObject root) const;
    uint32_t stateVersion() const;

  private:
    struct ChannelPins {
        int8_t forwardPin = -1;
        int8_t backwardPin = -1;

        bool configured() const {
            return forwardPin >= 0 && backwardPin >= 0 && forwardPin != backwardPin;
        }
    };

    struct ChannelRuntime {
        bool active = false;
        bool forward = true;
      bool lastDirectionForward = true;
        uint32_t stopAtMs = 0;
        int8_t limitInputIndex = -1;
      StopReason lastStopReason = StopReason::None;
    };

    ChannelPins configuredChannels_[2];
    ChannelRuntime runtimeChannels_[2];
    LimitInputReader limitInputReader_;
    uint32_t stateVersion_ = 0;
    int driverSlot_ = -1;

    void releaseConfiguredPins();
    void configurePins();
    void stopChannel(uint8_t channelIndex, StopReason reason = StopReason::Manual);
    void stopAllChannels();
    bool limitInputActive(int8_t limitInputIndex) const;
};
