#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <map>
#include <string>

// GPIO ownership is constrained by the compiler's trusted, free-pin table.
class LogicGpio {
public:
    bool action(JsonObjectConst node, JsonVariantConst args, std::string& error);
    void sample(JsonObjectConst node, JsonObject target);
    void reset();
private:
    struct PinState {int pin=-1,channel=-1;std::string mode;bool high=false;};
    std::map<std::string,PinState> pins_;
    PinState* prepare(JsonObjectConst node, std::string& error);
};
