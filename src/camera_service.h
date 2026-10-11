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
#include "bulk_allocator.h"
using CameraFrameBytes=std::vector<uint8_t,BulkAllocator<uint8_t>>;
bool copyCameraFrame(CameraFrameBytes& bytes,uint16_t& width,uint16_t& height);
bool withCameraFrame(const std::function<bool(const uint8_t*,size_t,uint16_t,uint16_t)>& callback);
#endif
