#pragma once
#if APP_HAS_CAMERA && !defined(APP_DISABLE_WEB_UI)
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <functional>
void beginCameraService();
void registerCameraRoutes(AsyncWebServer& server,std::function<bool(AsyncWebServerRequest*)> authorized);
#endif

#if APP_HAS_CAMERA && !defined(APP_DISABLE_WEB_UI)
#include <vector>
bool copyCameraFrame(std::vector<uint8_t>& bytes,uint16_t& width,uint16_t& height);
#endif
