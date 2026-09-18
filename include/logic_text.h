#pragma once
#include <ArduinoJson.h>
#include <string>
namespace ElmaLogic {
// Primitive values only: audio descriptors and peripheral references cannot become messages.
inline bool appendText(const std::string& text, JsonVariantConst value,
                       const std::string& separator, std::string& result) {
    result=text;
    if(!value.isNull()) {
        std::string suffix;
        if(value.is<const char*>())suffix=value.as<std::string>();
        else if(value.is<bool>()||value.is<double>())serializeJson(value,suffix);
        else return false;
        result+=separator;result+=suffix;
    }
    return result.size()<=1024;
}
}
