#pragma once
#include <ArduinoJson.h>
#include <string>
namespace ElmaLogic {
bool validateEditable(JsonVariantConst input, JsonArrayConst devices, JsonDocument& output, std::string& error);
}
