#pragma once
#if defined(APP_SPK_BOARD) && !defined(APP_DISABLE_AUDIO)
#include "led_microphone.h"
#include <Preferences.h>
inline void registerMicrophoneRoutes(AsyncWebServer& server,std::function<bool(AsyncWebServerRequest*)> authorized){
 Preferences preferences;preferences.begin("microphones",true);
 LedMicrophone::enabled()=preferences.getBool("enabled",true);
 LedMicrophone::gain()=constrain(preferences.getInt("gain",1),1,8);preferences.end();
 server.on("/api/microphones",HTTP_GET,[authorized](AsyncWebServerRequest* request){
  if(!authorized(request))return;JsonDocument doc;
  doc["enabled"]=LedMicrophone::enabled().load();doc["gain"]=LedMicrophone::gain().load();
  doc["ready"]=LedMicrophone::status().load()==1;doc["status"]=LedMicrophone::status().load();
  doc["sampleRate"]=32000;doc["channels"]=2;
  doc["left"]=LedMicrophone::peakLeft().load()/32768.f;doc["right"]=LedMicrophone::peakRight().load()/32768.f;
  String data;serializeJson(doc,data);auto* response=request->beginResponse(200,"application/json",data);response->addHeader("Cache-Control","no-store");request->send(response);
 });
 server.on("/api/microphones",HTTP_POST,[authorized](AsyncWebServerRequest* request){
  if(!authorized(request))return;
  if(!request->hasParam("enabled",true)||!request->hasParam("gain",true)){request->send(400,"application/json","{\"error\":\"Missing controls\"}");return;}
  String e=request->getParam("enabled",true)->value(),g=request->getParam("gain",true)->value();
  if((e!="0"&&e!="1")||g.length()!=1||g[0]<'1'||g[0]>'8'){request->send(400,"application/json","{\"error\":\"Invalid controls\"}");return;}
  Preferences p;p.begin("microphones",false);p.putBool("enabled",e=="1");p.putInt("gain",g.toInt());p.end();
  LedMicrophone::gain()=g.toInt();LedMicrophone::enabled()=e=="1";
  request->send(200,"application/json","{\"saved\":true}");
 });
}
#endif
