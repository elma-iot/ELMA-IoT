#pragma once
#if defined(APP_SPK_BOARD) && !defined(APP_DISABLE_AUDIO) && !defined(APP_DISABLE_WEB_UI)
#include <ESPAsyncWebServer.h>
#include <functional>
namespace MotionCapture {
void begin();
void tick(bool otaActive);
void routes(AsyncWebServer&,std::function<bool(AsyncWebServerRequest*)>);
}
#endif
