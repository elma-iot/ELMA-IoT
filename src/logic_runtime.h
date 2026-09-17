#pragma once

// Portable graph engine: no Arduino, network or audio driver dependencies.
#include <ArduinoJson.h>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ElmaLogic {
class Runtime {
public:
    using Action = std::function<bool(JsonObjectConst, JsonVariantConst, std::string&)>;
    bool begin(const char* payload, Action action, std::string& error);
    void tick(uint32_t now, JsonVariantConst status);
    void lifecycle(const char* event);
    void suspend();
    void pause(uint32_t now);
    void resume(uint32_t now);
    void restart();
    bool controlGroup(const char* id, const char* mode, uint32_t now);
    void observe(JsonVariantConst status);
    void telemetry(JsonObject target) const;
    bool paused() const { return paused_; }
    bool active() const { return graph_["nodes"].size() != 0; }
    const std::string& error() const { return error_; }
    JsonObjectConst graph() const { return graph_.as<JsonObjectConst>(); }
    JsonArrayConst nodes() const { return graph_["nodes"].as<JsonArrayConst>(); }

private:
    struct State {
        bool initialized = false, enabled = false, pending = false, startSent = false, frozen = false;
        uint32_t frozenAt = 0;
        uint32_t due = 0, interval = 0;
        unsigned remaining = 0;
    };
    struct Pulse { size_t node; std::string port; };
    JsonDocument graph_, cache_, previous_;
    JsonVariantConst status_;
    std::vector<State> states_;
    std::vector<Pulse> pulses_;
    Action action_;
    std::string error_;
    uint32_t now_ = 0;
    unsigned budget_ = 0, depth_ = 0;
    bool started_ = false, paused_ = false;
    uint32_t pausedAt_ = 0;
    struct Activity { uint32_t at = 0, sequence = 0; bool failed = false; };
    std::vector<Activity> activity_;
    JsonObjectConst node(size_t index) const { return graph_["nodes"][index].as<JsonObjectConst>(); }
    int index(const char* id) const;
    bool allowed(size_t n) const;
    void freezeStates(uint32_t now);
    bool linked(size_t n, const char* port) const;
    JsonVariantConst input(size_t n, const char* port);
    JsonVariantConst value(size_t n, const char* port);
    JsonVariantConst path(const char* name) const;
    uint32_t interval(size_t n);
    void emit(size_t n, const char* port = "out");
    void execute(size_t n, const char* port);
    void drain();
    bool changed(size_t n, JsonVariantConst current, const char* edge, JsonVariantConst equals = {});
};
}
