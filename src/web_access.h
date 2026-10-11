#pragma once
#include "settings_schema.h"
#include <ArduinoJson.h>
namespace WebAccess {
void tick(const SettingsBundle& settings, bool otaBusy);
String hostname(const SettingsBundle& settings);
void diagnostics(JsonObject output);
bool importCertificate(const String& certificate, const String& privateKey, String& error);
}
