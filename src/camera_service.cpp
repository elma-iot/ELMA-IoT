#include "camera_service.h"
#if APP_HAS_CAMERA && !defined(APP_DISABLE_WEB_UI)
#include <esp_camera.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <memory>
#include <atomic>
#include "device_log.h"
namespace {
struct Frame {uint8_t* bytes=nullptr;size_t size=0;~Frame(){free(bytes);}};
struct Command {uint8_t field;int value;};
const char* names[]={"framesize","quality","brightness","contrast","saturation","hmirror","vflip","awb","aec","agc"};
const int minimum[]={0,10,-2,-2,-2,0,0,0,0,0},maximum[]={2,40,2,2,2,1,1,1,1,1};
int values[]={1,15,0,0,0,0,0,1,1,1};
SemaphoreHandle_t lock=nullptr;QueueHandle_t commands=nullptr;
std::shared_ptr<Frame> latest;std::atomic<uint32_t> requestedAt{0};std::atomic<bool> ready{false};std::atomic<int> failure{0};
void apply(sensor_t* sensor,int field,int value){
 switch(field){case 0:sensor->set_framesize(sensor,value==0?FRAMESIZE_QQVGA:value==1?FRAMESIZE_QVGA:FRAMESIZE_VGA);break;
 case 1:sensor->set_quality(sensor,value);break;case 2:sensor->set_brightness(sensor,value);break;
 case 3:sensor->set_contrast(sensor,value);break;case 4:sensor->set_saturation(sensor,value);break;
 case 5:sensor->set_hmirror(sensor,value);break;case 6:sensor->set_vflip(sensor,value);break;
 case 7:sensor->set_whitebal(sensor,value);break;case 8:sensor->set_exposure_ctrl(sensor,value);break;
 case 9:sensor->set_gain_ctrl(sensor,value);break;}
}
void task(void*){
 // Espressif CameraWebServer CAMERA_MODEL_AI_THINKER mapping, Arduino 2.0.17.
 camera_config_t config={};config.ledc_channel=LEDC_CHANNEL_0;config.ledc_timer=LEDC_TIMER_0;
 config.pin_d0=5;config.pin_d1=18;config.pin_d2=19;config.pin_d3=21;config.pin_d4=36;config.pin_d5=39;config.pin_d6=34;config.pin_d7=35;
 config.pin_xclk=0;config.pin_pclk=22;config.pin_vsync=25;config.pin_href=23;
 config.pin_sscb_sda=26;config.pin_sscb_scl=27;config.pin_pwdn=32;config.pin_reset=-1;
#if defined(APP_SPK_BOARD)
 // MINIEXCO v2.00.12 and ESP32S3-SPK V1.0 schematic (external quad PSRAM).
 config.pin_d0=7;config.pin_d1=5;config.pin_d2=4;config.pin_d3=6;
 config.pin_d4=8;config.pin_d5=42;config.pin_d6=48;config.pin_d7=47;
 config.pin_xclk=33;config.pin_pclk=41;config.pin_vsync=35;config.pin_href=34;
 config.pin_sscb_sda=37;config.pin_sscb_scl=36;config.pin_pwdn=-1;config.pin_reset=-1;
#endif
 // Allocate for the largest selectable frame before applying the saved preview size.
 config.xclk_freq_hz=20000000;config.pixel_format=PIXFORMAT_JPEG;config.frame_size=psramFound()?FRAMESIZE_VGA:FRAMESIZE_QVGA;
#if defined(APP_SPK_BOARD)
 config.xclk_freq_hz=10000000;
#endif
 config.jpeg_quality=15;config.fb_count=1;config.fb_location=psramFound()?CAMERA_FB_IN_PSRAM:CAMERA_FB_IN_DRAM;
 config.grab_mode=CAMERA_GRAB_WHEN_EMPTY;
 int error=esp_camera_init(&config);failure=error;
 if(error){DebugLog.printf("[camera] Init failed: 0x%x\n",error);vTaskDelete(nullptr);return;}
 auto* sensor=esp_camera_sensor_get();Preferences prefs;prefs.begin("camera",false);
 for(int i=0;i<10;i++){values[i]=constrain(prefs.getInt(names[i],values[i]),minimum[i],maximum[i]);if(i==0&&!psramFound())values[i]=min(values[i],1);apply(sensor,i,values[i]);}
 ready=true;
 for(;;){
  Command c;
  while(xQueueReceive(commands,&c,0)==pdTRUE){apply(sensor,c.field,c.value);xSemaphoreTake(lock,portMAX_DELAY);values[c.field]=c.value;latest.reset();xSemaphoreGive(lock);prefs.putInt(names[c.field],c.value);}
  uint32_t requested=requestedAt.load();
  if(requested && uint32_t(millis()-requested)<2500){
   auto* fb=esp_camera_fb_get();
   if(fb){
    auto next=std::make_shared<Frame>();next->size=fb->len;
    next->bytes=static_cast<uint8_t*>(heap_caps_malloc(fb->len,psramFound()?MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT:MALLOC_CAP_8BIT));
    if(next->bytes){memcpy(next->bytes,fb->buf,fb->len);xSemaphoreTake(lock,portMAX_DELAY);latest=next;xSemaphoreGive(lock);}
    esp_camera_fb_return(fb);
   }
  }else {xSemaphoreTake(lock,portMAX_DELAY);latest.reset();xSemaphoreGive(lock);}
  vTaskDelay(pdMS_TO_TICKS(160)); // Bounded preview rate; capture never blocks the main loop or HTTP task.
 }
}
void json(AsyncWebServerRequest* request,JsonDocument& doc,int code=200){String data;serializeJson(doc,data);auto* response=request->beginResponse(code,"application/json",data);response->addHeader("Cache-Control","no-store");request->send(response);}
}
void beginCameraService(){
 lock=xSemaphoreCreateMutex();commands=xQueueCreate(8,sizeof(Command));
 if(!lock||!commands||xTaskCreate(task,"camera",6144,nullptr,1,nullptr)!=pdPASS)failure=-1;
}
void registerCameraRoutes(AsyncWebServer& server,std::function<bool(AsyncWebServerRequest*)> authorized){
 server.on("/api/camera/status",HTTP_GET,[authorized](AsyncWebServerRequest* request){
  if(!authorized(request))return;JsonDocument doc;doc["ready"]=ready.load();doc["errorCode"]=failure.load();doc["psram"]=psramFound();
  if(ready.load()&&lock){xSemaphoreTake(lock,portMAX_DELAY);for(int i=0;i<10;i++)doc[names[i]]=values[i];xSemaphoreGive(lock);}json(request,doc);
 });
 server.on("/api/camera/control",HTTP_POST,[authorized](AsyncWebServerRequest* request){
  if(!authorized(request))return;JsonDocument doc;
  if(!ready){doc["error"]="Camera is not ready";json(request,doc,503);return;}
  if(!request->hasParam("name",true)||!request->hasParam("value",true)){doc["error"]="Missing camera control";json(request,doc,400);return;}
  String name=request->getParam("name",true)->value(),raw=request->getParam("value",true)->value();char* end=nullptr;long value=strtol(raw.c_str(),&end,10);int field=-1;
  for(int i=0;i<10;i++)if(name==names[i])field=i;
  if(field<0||raw.isEmpty()||*end||value<minimum[field]||value>maximum[field]||(field==0&&value==2&&!psramFound())){doc["error"]="Unsupported camera control or value";json(request,doc,400);return;}
  Command c{uint8_t(field),int(value)};
  if(xQueueSend(commands,&c,0)!=pdTRUE){doc["error"]="Camera busy; retry";json(request,doc,503);return;}
  doc["accepted"]=true;json(request,doc,202);
 });
 server.on("/api/camera/frame",HTTP_GET,[authorized](AsyncWebServerRequest* request){
  if(!authorized(request))return;requestedAt=millis();std::shared_ptr<Frame> frame;
  if(lock){xSemaphoreTake(lock,portMAX_DELAY);frame=latest;xSemaphoreGive(lock);}
  if(!frame||!frame->bytes){request->send(503,"text/plain","Camera is warming up or unavailable");return;}
  auto* response=request->beginResponse("image/jpeg",frame->size,[frame](uint8_t* output,size_t max,size_t index){size_t count=index<frame->size?std::min(max,frame->size-index):0;if(count)memcpy(output,frame->bytes+index,count);return count;});
  response->addHeader("Cache-Control","no-store");request->send(response);
 });
}
#endif
