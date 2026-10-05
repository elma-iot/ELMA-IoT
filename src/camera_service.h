#pragma once
#if APP_HAS_CAMERA && !defined(APP_DISABLE_WEB_UI)
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <functional>
void beginCameraService();
void registerCameraRoutes(AsyncWebServer& server,std::function<bool(AsyncWebServerRequest*)> authorized);
#endif
