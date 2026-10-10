#pragma once
#include "settings_manager.h"
namespace CanBus {
void configure(const SettingsBundle& settings);
bool available();
void tick();
bool command(JsonVariantConst args, String& error);
void snapshot(JsonObject out);
}
