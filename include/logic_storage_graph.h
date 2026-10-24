#pragma once
#include <ArduinoJson.h>
#include <cstring>

namespace ElmaLogic {
// Firmware rebuilds contracts/bindings. Do not allocate their large editor
// metadata merely to remove it from the persistent record afterward.
inline void copyPersistedGraph(JsonVariantConst source, JsonObject target) {
    for (JsonPairConst pair : source.as<JsonObjectConst>()) {
        const char* key = pair.key().c_str();
        if (!strcmp(key, "devices")) continue;
        if (strcmp(key, "nodes")) { target[key].set(pair.value()); continue; }
        JsonArray nodes = target["nodes"].to<JsonArray>();
        for (JsonObjectConst node : pair.value().as<JsonArrayConst>()) {
            JsonObject copy = nodes.add<JsonObject>();
            for (JsonPairConst property : node) {
                const char* name = property.key().c_str();
                if (strcmp(name, "ports") && strcmp(name, "binding")) copy[name].set(property.value());
            }
        }
    }
}
}
