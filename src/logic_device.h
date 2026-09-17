#pragma once
#include "logic_runtime.h"
#include "app_state.h"
#include <freertos/semphr.h>

// Adapter owns polling/physical GPIO. Main supplies existing subsystem actions.
class LogicDevice {
public:
    using StatusWriter = std::function<void(JsonObject)>;
    bool begin(const char* program, AppState& state, StatusWriter status, ElmaLogic::Runtime::Action actions);
    void loop(uint32_t now, bool updating);
    void shuttingDown();
    bool request(JsonVariantConst command, JsonDocument& response, String& error);
    void snapshot(JsonDocument& response, bool graph);
    const char* mode() const { return mode_.c_str(); }
private:
    ElmaLogic::Runtime runtime_;
    AppState* state_ = nullptr;
    StatusWriter status_;
    ElmaLogic::Runtime::Action actions_;
    uint32_t lastPoll_ = 0;
    bool polled_ = false, shutdown_ = false;
    std::string reportedError_;
    SemaphoreHandle_t mutex_ = nullptr;
    JsonDocument devices_;
    std::string mode_ = "playing";
    uint32_t source_ = 0;
    bool updating_ = false, audioOwned_ = false;
    std::string audioOwner_;
    bool persist(JsonVariantConst graph, const std::string& mode, String& error);
    void applyMode(const std::string& mode);
    bool action(JsonObjectConst node, JsonVariantConst args, std::string& error);
};
