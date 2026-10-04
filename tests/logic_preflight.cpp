#include "logic_validation.h"
#include "logic_runtime.h"
#include <iostream>
#include <iterator>
int main() {
    std::string source((std::istreambuf_iterator<char>(std::cin)), {}), error;
    JsonDocument input, accepted;
    auto parsed = deserializeJson(input, source);
    if (parsed) { std::cout << parsed.c_str(); return 1; }
    if (!ElmaLogic::validateEditable(input.as<JsonVariantConst>(), input["devices"].as<JsonArrayConst>(), accepted, error)) {
        std::cout << error; return 1;
    }
    ElmaLogic::Runtime runtime;
    if (!runtime.begin(std::move(accepted), [](JsonObjectConst, JsonVariantConst, std::string&) { return true; }, error)) {
        std::cout << error; return 1;
    }
    // Exercise scheduling without sending commands to physical hardware.
    JsonDocument status;
    for (uint32_t now = 0; now <= 60000; now += 20) {
        runtime.tick(now, status.as<JsonVariantConst>());
        if (!runtime.error().empty()) { std::cout << runtime.error(); return 1; }
    }
    std::cout << "Firmware validator and 60-second scheduler check passed";
}
