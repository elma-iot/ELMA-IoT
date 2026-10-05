#pragma once
#if APP_HAS_ONBOARD_PANEL
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "storage_memory.h"

// Public Radio Browser directory; network I/O never runs in the display loop.
namespace PanelRadio {
inline SemaphoreHandle_t mutex(){static auto m=xSemaphoreCreateMutex();return m;}
inline String& result(){static String s="{}";return s;}
inline bool& busy(){static bool b=false;return b;}
struct Request {String country,name;int offset=0;bool countries=false;};
inline String encode(const String& s){String out;const char* hex="0123456789ABCDEF";for(unsigned char c:s){if(isalnum(c)||c=='-'||c=='_')out+=char(c);else{out+='%';out+=hex[c>>4];out+=hex[c&15];}}return out;}
inline void fetch(Request* request){
 JsonDocument output(storageJsonAllocator());
 output["countriesMode"]=request->countries;output["offset"]=request->offset;
 String path=request->countries?"/countries":"/stations/search?hidebroken=true&order=name&limit=20&offset="+String(request->offset)+"&country="+encode(request->country)+"&name="+encode(request->name);
 WiFiClient client;HTTPClient http;http.useHTTP10(true);http.setTimeout(5000);http.setConnectTimeout(4000);
 // This public directory contains no credentials. Stream filtered JSON to bound heap use.
 if(WiFi.status()!=WL_CONNECTED)output["error"]="Connect Wi-Fi to browse radio";
 else if(!http.begin(client,"http://all.api.radio-browser.info/json"+path)||http.GET()!=200)output["error"]="Radio directory unavailable; try again";
 else {
  JsonDocument filter;filter[0]["name"]=true;
  if(!request->countries){filter[0]["url_resolved"]=true;filter[0]["url"]=true;}
  JsonDocument data(storageJsonAllocator());auto error=deserializeJson(data,http.getStream(),DeserializationOption::Filter(filter));
  if(error)output["error"]="Could not read radio directory";
  else {auto items=output["items"].to<JsonArray>();int count=0;for(JsonObjectConst row:data.as<JsonArrayConst>()){
   if(++count>(request->countries?300:20))break;
   auto item=items.add<JsonObject>();String name=row["name"]|"";name.replace("\n"," ");item["name"]=name.substring(0,100);
   if(!request->countries)item["url"]=row["url_resolved"]|row["url"]|"";
  }output["more"]=!request->countries&&items.size()==20;}
 }
 http.end();String encoded;serializeJson(output,encoded);
 xSemaphoreTake(mutex(),portMAX_DELAY);result()=encoded;busy()=false;xSemaphoreGive(mutex());
}
inline void worker(void* pointer){
 auto* request=static_cast<Request*>(pointer);
 // Return through fetch first so JSON, HTTP and String allocations are freed
 // before FreeRTOS deletes the task (which does not unwind C++ stack objects).
 fetch(request);delete request;vTaskDelete(nullptr);
}
inline bool request(JsonVariantConst args,String& error){
 if(!mutex()){error="Radio memory unavailable";return false;}
 xSemaphoreTake(mutex(),portMAX_DELAY);if(busy()){xSemaphoreGive(mutex());error="Radio lookup in progress";return false;}busy()=true;xSemaphoreGive(mutex());
 auto* job=new Request{args["country"]|"",args["name"]|"",max(0,args["offset"]|0),args["countries"]|false};
 if(xTaskCreatePinnedToCore(worker,"panelRadio",6144,job,1,nullptr,0)!=pdPASS){delete job;xSemaphoreTake(mutex(),portMAX_DELAY);busy()=false;xSemaphoreGive(mutex());error="Radio worker unavailable";return false;}return true;
}
inline void snapshot(JsonObject root){if(!mutex())return;xSemaphoreTake(mutex(),portMAX_DELAY);String text=result();bool pending=busy();xSemaphoreGive(mutex());JsonDocument doc(storageJsonAllocator());deserializeJson(doc,text);root.set(doc.as<JsonObjectConst>());root["busy"]=pending;}
}
#endif
