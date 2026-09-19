#pragma once
#include <ArduinoJson.h>
#include <Arduino.h>
// Filesystem-independent persistence for OTA layouts without LittleFS.
bool loadLogicRecord(JsonDocument& record);
bool saveLogicRecord(JsonVariantConst record, String& error);
bool clearLogicRecord();
