#pragma once
#include <ArduinoJson.h>
namespace AmoledSensors {
void tick();
void snapshot(JsonObject target);
bool touch(int16_t& x,int16_t& y);
}
