#pragma once
#include <ArduinoJson.h>
#include "settings_schema.h"
namespace Bno055 {
void configure(const SettingsBundle& settings);
void tick();
void snapshot(JsonObject root);
bool command(JsonVariantConst args,String& error);
}
