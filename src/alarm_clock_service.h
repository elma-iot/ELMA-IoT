#pragma once
#include "settings_schema.h"
#include <ArduinoJson.h>
#include <string>
void configureAlarmClock(const SettingsBundle& settings);
void pollAlarmClock(uint32_t now);
void alarmClockSnapshot(JsonObject target);
bool setAlarmClock(const char* source,int64_t epoch,std::string& error);
