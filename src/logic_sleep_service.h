#pragma once
#include <ArduinoJson.h>
#include <functional>
#include <string>
void beginLogicSleep();
bool requestLogicSleep(JsonObjectConst node,JsonVariantConst args,std::string& error);
bool logicSleepPending();
void logicSleepSnapshot(JsonObject result);
void processLogicSleep(bool busy,const std::function<void()>& prepare);
