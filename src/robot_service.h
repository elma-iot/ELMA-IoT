#pragma once
#include <Arduino.h>
#ifndef APP_DISABLE_WEB_UI
#include <ESPAsyncWebServer.h>
#include <functional>
#endif
namespace MiniExco {
void begin();
void setBatteryVoltage(float value);
void tick(bool updating);
void stop();
#ifndef APP_DISABLE_WEB_UI
void routes(AsyncWebServer&,std::function<bool(AsyncWebServerRequest*)>,std::function<bool()>);
#endif
}
