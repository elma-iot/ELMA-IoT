#pragma once
#include <ArduinoJson.h>
namespace Rs485 {bool available();void tick();bool command(JsonVariantConst args,String& error);void snapshot(JsonObject out,bool includeLog=true);}
