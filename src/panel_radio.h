#pragma once
#if APP_HAS_ONBOARD_PANEL
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <new>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "storage_memory.h"
#include "panel_memory.h"

// Public Radio Browser directory; network I/O never runs in the display loop.
namespace PanelRadio {
inline SemaphoreHandle_t mutex(){static auto m=xSemaphoreCreateMutex();return m;}
inline String& result(){static String s="{}";return s;}
inline bool& busy(){static bool b=false;return b;}
inline void releaseIdleCache(){
 if(!mutex()||xSemaphoreTake(mutex(),0)!=pdTRUE)return;
 if(!busy())result()=String();
 xSemaphoreGive(mutex());
}
struct Request {String country,name;int offset=0;bool countries=false;};
inline String encode(const String& s){String out;const char* hex="0123456789ABCDEF";for(unsigned char c:s){if(isalnum(c)||c=='-'||c=='_')out+=char(c);else{out+='%';out+=hex[c>>4];out+=hex[c&15];}}return out;}
inline void fetch(Request* request){
 PanelJsonAllocator allocator;
 JsonDocument output(&allocator);
 output["countriesMode"]=request->countries;output["offset"]=request->offset;
 String path=request->countries?"/countries":"/stations/search?hidebroken=true&order=name&limit=20&offset="+String(request->offset)+"&country="+encode(request->country)+"&name="+encode(request->name);
 WiFiClient client;HTTPClient http;http.useHTTP10(true);http.setTimeout(5000);http.setConnectTimeout(4000);
 // This public directory contains no credentials. Stream filtered JSON to bound heap use.
 if(WiFi.status()!=WL_CONNECTED)output["error"]="Connect Wi-Fi to browse radio";
 else if(!http.begin(client,"http://all.api.radio-browser.info/json"+path)||http.GET()!=200)output["error"]="Radio directory unavailable; try again";
 else {
  JsonDocument filter;filter["name"]=true;
  if(!request->countries){filter["url_resolved"]=true;filter["url"]=true;}
  // Parse one station/country at a time. The countries endpoint can return
  // hundreds of rows; retaining the whole response exhausted no-PSRAM boards.
  auto& input=http.getStream();auto items=output["items"].to<JsonArray>();
  const int limit=request->countries?40:20;int skipped=0;
  if(!input.find("["))output["error"]="Could not read radio directory";
  else while(http.connected()||input.available()){
   int c=input.peek();
   if(c==' '||c=='\r'||c=='\n'||c=='\t'||c==','){input.read();continue;}
   if(c==']')break;
   JsonDocument row(&allocator);
   auto error=deserializeJson(row,input,DeserializationOption::Filter(filter));
   if(error){output["error"]="Radio lookup paused: insufficient memory or incomplete response";break;}
   if(request->countries&&skipped++<request->offset)continue;
   if(items.size()>=static_cast<size_t>(limit)){output["more"]=true;break;}
   auto item=items.add<JsonObject>();String name=row["name"]|"";name.replace("\n"," ");item["name"]=name.substring(0,100);
   if(!request->countries)item["url"]=row["url_resolved"]|row["url"]|"";
   if(output.overflowed()){output.clear();output["error"]="Radio lookup paused: memory busy";break;}
   taskYIELD();
  }
  if(!request->countries)output["more"]=items.size()==20;

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
 auto* job=new(std::nothrow) Request{args["country"]|"",args["name"]|"",max(0,args["offset"]|0),args["countries"]|false};
 if(!job){xSemaphoreTake(mutex(),portMAX_DELAY);busy()=false;xSemaphoreGive(mutex());error="Radio memory unavailable";return false;}
 if(xTaskCreatePinnedToCore(worker,"panelRadio",6144,job,1,nullptr,0)!=pdPASS){delete job;xSemaphoreTake(mutex(),portMAX_DELAY);busy()=false;xSemaphoreGive(mutex());error="Radio worker unavailable";return false;}return true;
}
inline void snapshot(JsonObject root){
 if(!mutex())return;
 JsonDocument doc(panelJsonAllocator());
 xSemaphoreTake(mutex(),portMAX_DELAY);
 // Parse the stable cache under its lock, avoiding a second large String.
 deserializeJson(doc,result());const bool pending=busy();
 xSemaphoreGive(mutex());
 root.set(doc.as<JsonObjectConst>());root["busy"]=pending;
}
}
#endif
