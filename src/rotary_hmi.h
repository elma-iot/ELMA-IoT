#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "rotary_hmi_model.h"
#include <functional>
class PanelDisplay;
class AppStateSnapshot;
namespace RoundHmi {
bool available();bool begin(PanelDisplay&,uint8_t rotation,const String& configuration);
void end();void loop();void networkStatus(bool wifi,bool mqtt);bool takeActivity();void snapshot(JsonObject);
void systemState(const AppStateSnapshot&,const std::function<void(const String&,JsonObject)>&,
                 const std::function<bool(const String&,JsonVariantConst,String&)>&);
void temporaryText(const String&,uint32_t durationMs);
bool command(JsonVariantConst,String& error);bool configure(const String&,String& error,bool validateOnly=false);
}
