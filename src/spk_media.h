#pragma once
#if defined(APP_SPK_BOARD) && !defined(APP_DISABLE_AUDIO) && !defined(APP_DISABLE_WEB_UI)
#include <ESPAsyncWebServer.h>
#include <functional>
class AudioPlayer;
struct SettingsBundle;
void registerSpkMediaRoutes(AsyncWebServer&,std::function<bool(AsyncWebServerRequest*)>,std::function<bool()>);
void stopSpkMediaForUpdate();
void serviceSpkMediaAudio(AudioPlayer&,const SettingsBundle&);
bool triggerSpkSecurityCapture(bool picture,bool video,uint32_t seconds);
bool spkMediaRecording();
#endif
