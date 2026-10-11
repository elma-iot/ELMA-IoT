#include "camera_service.h"
#if APP_HAS_CAMERA && !defined(APP_DISABLE_WEB_UI)
#include <esp_camera.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <memory>
#include <atomic>
#include "device_log.h"
namespace {
struct Frame {uint8_t* bytes=nullptr;size_t size=0;uint16_t width=0,height=0;~Frame(){free(bytes);}};
struct Command {uint8_t field;int value;};
const char* names[]={"framesize","quality","brightness","contrast","saturation","hmirror","vflip","awb","aec","agc","awb_gain","wb_mode","aec2","ae_level","aec_value","agc_gain","gainceiling","bpc","wpc","raw_gma","lenc","dcw","colorbar","special_effect","sharpness"};
const int minimum[]={0,4,-2,-2,-2,0,0,0,0,0,0,0,0,-2,0,0,0,0,0,0,0,0,0,0,-2},maximum[]={7,63,2,2,2,1,1,1,1,1,1,4,1,2,1200,30,6,1,1,1,1,1,1,6,2};
int values[]={1,15,0,0,0,0,0,1,1,1,1,0,0,0,300,0,0,0,1,1,1,1,0,0,0};
SemaphoreHandle_t lock=nullptr;QueueHandle_t commands=nullptr;
std::shared_ptr<Frame> latest;std::atomic<uint32_t> requestedAt{0};std::atomic<bool> ready{false};std::atomic<int> failure{0};
const framesize_t sizes[]={FRAMESIZE_QQVGA,FRAMESIZE_QVGA,FRAMESIZE_VGA,FRAMESIZE_SVGA,FRAMESIZE_XGA,FRAMESIZE_HD,FRAMESIZE_SXGA,FRAMESIZE_UXGA};
int apply(sensor_t* sensor,int field,int value){
 switch(field){case 0:return sensor->set_framesize ? sensor->set_framesize(sensor,sizes[value]) : -1;
 case 1:return sensor->set_quality ? sensor->set_quality(sensor,value) : -1;case 2:return sensor->set_brightness ? sensor->set_brightness(sensor,value) : -1;
 case 3:return sensor->set_contrast ? sensor->set_contrast(sensor,value) : -1;case 4:return sensor->set_saturation ? sensor->set_saturation(sensor,value) : -1;
 case 5:return sensor->set_hmirror ? sensor->set_hmirror(sensor,value) : -1;case 6:return sensor->set_vflip ? sensor->set_vflip(sensor,value) : -1;
 case 7:return sensor->set_whitebal ? sensor->set_whitebal(sensor,value) : -1;case 8:return sensor->set_exposure_ctrl ? sensor->set_exposure_ctrl(sensor,value) : -1;
 case 9:return sensor->set_gain_ctrl ? sensor->set_gain_ctrl(sensor,value) : -1;case 10:return sensor->set_awb_gain ? sensor->set_awb_gain(sensor,value) : -1;case 11:return sensor->set_wb_mode ? sensor->set_wb_mode(sensor,value) : -1;case 12:return sensor->set_aec2 ? sensor->set_aec2(sensor,value) : -1;case 13:return sensor->set_ae_level ? sensor->set_ae_level(sensor,value) : -1;case 14:return sensor->set_aec_value ? sensor->set_aec_value(sensor,value) : -1;case 15:return sensor->set_agc_gain ? sensor->set_agc_gain(sensor,value) : -1;case 16:return sensor->set_gainceiling ? sensor->set_gainceiling(sensor,gainceiling_t(value)) : -1;case 17:return sensor->set_bpc ? sensor->set_bpc(sensor,value) : -1;case 18:return sensor->set_wpc ? sensor->set_wpc(sensor,value) : -1;case 19:return sensor->set_raw_gma ? sensor->set_raw_gma(sensor,value) : -1;case 20:return sensor->set_lenc ? sensor->set_lenc(sensor,value) : -1;case 21:return sensor->set_dcw ? sensor->set_dcw(sensor,value) : -1;case 22:return sensor->set_colorbar ? sensor->set_colorbar(sensor,value) : -1;case 23:return sensor->set_special_effect ? sensor->set_special_effect(sensor,value) : -1;case 24:return sensor->set_sharpness ? sensor->set_sharpness(sensor,value) : -1;}
 return -1;
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
 config.xclk_freq_hz=20000000;config.pixel_format=PIXFORMAT_JPEG;config.frame_size=psramFound()?FRAMESIZE_UXGA:FRAMESIZE_QVGA;
#if defined(APP_SPK_BOARD)
 config.xclk_freq_hz=20000000;
#endif
 config.jpeg_quality=15;config.fb_count=psramFound()?2:1;config.fb_location=psramFound()?CAMERA_FB_IN_PSRAM:CAMERA_FB_IN_DRAM;
 config.grab_mode=psramFound()?CAMERA_GRAB_LATEST:CAMERA_GRAB_WHEN_EMPTY;
 int error=esp_camera_init(&config);failure=error;
 if(error){DebugLog.printf("[camera] Init failed: 0x%x\n",error);vTaskDelete(nullptr);return;}
 auto* sensor=esp_camera_sensor_get();Preferences prefs;prefs.begin("camera",false);
 for(int i=0;i<25;i++){values[i]=constrain(prefs.getInt(names[i],values[i]),minimum[i],maximum[i]);if(i==0&&!psramFound())values[i]=min(values[i],1);apply(sensor,i,values[i]);}
 ready=true;
 for(;;){
  Command c;
  while(xQueueReceive(commands,&c,0)==pdTRUE){int result=apply(sensor,c.field,c.value);failure=result;if(result)continue;xSemaphoreTake(lock,portMAX_DELAY);values[c.field]=c.value;latest.reset();xSemaphoreGive(lock);prefs.putInt(names[c.field],c.value);}
  uint32_t requested=requestedAt.load();
  if(requested && uint32_t(millis()-requested)<2500){
   auto* fb=esp_camera_fb_get();
   if(fb){
    auto next=std::make_shared<Frame>();next->size=fb->len;next->width=fb->width;next->height=fb->height;
    next->bytes=static_cast<uint8_t*>(heap_caps_malloc(fb->len,psramFound()?MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT:MALLOC_CAP_8BIT));
    if(next->bytes){memcpy(next->bytes,fb->buf,fb->len);xSemaphoreTake(lock,portMAX_DELAY);latest=next;xSemaphoreGive(lock);}
    esp_camera_fb_return(fb);
   }
  }else {xSemaphoreTake(lock,portMAX_DELAY);latest.reset();xSemaphoreGive(lock);}
  vTaskDelay(pdMS_TO_TICKS(10)); // Bounded preview rate; capture never blocks the main loop or HTTP task.
 }
}
void json(AsyncWebServerRequest* request,JsonDocument& doc,int code=200){String data;serializeJson(doc,data);auto* response=request->beginResponse(code,"application/json",data);response->addHeader("Cache-Control","no-store");request->send(response);}
}
bool copyCameraFrame(std::vector<uint8_t>& bytes,uint16_t& width,uint16_t& height){
 requestedAt=millis();std::shared_ptr<Frame> frame;if(!lock)return false;xSemaphoreTake(lock,portMAX_DELAY);frame=latest;xSemaphoreGive(lock);
 if(!frame||!frame->bytes)return false;bytes.assign(frame->bytes,frame->bytes+frame->size);width=frame->width;height=frame->height;return true;
}
void beginCameraService(){
 lock=xSemaphoreCreateMutex();commands=xQueueCreate(8,sizeof(Command));
 if(!lock||!commands||xTaskCreate(task,"camera",6144,nullptr,1,nullptr)!=pdPASS)failure=-1;
}
void registerCameraRoutes(AsyncWebServer& server,std::function<bool(AsyncWebServerRequest*)> authorized){
 server.on("/api/camera/status",HTTP_GET,[authorized](AsyncWebServerRequest* request){
  if(!authorized(request))return;JsonDocument doc;doc["ready"]=ready.load();doc["errorCode"]=failure.load();doc["psram"]=psramFound();
  if(ready.load()&&lock){xSemaphoreTake(lock,portMAX_DELAY);for(int i=0;i<25;i++)doc[names[i]]=values[i];xSemaphoreGive(lock);}json(request,doc);
 });
 server.on("/api/camera/control",HTTP_POST,[authorized](AsyncWebServerRequest* request){
  if(!authorized(request))return;JsonDocument doc;
  if(!ready){doc["error"]="Camera is not ready";json(request,doc,503);return;}
  if(!request->hasParam("name",true)||!request->hasParam("value",true)){doc["error"]="Missing camera control";json(request,doc,400);return;}
  String name=request->getParam("name",true)->value(),raw=request->getParam("value",true)->value();char* end=nullptr;long value=strtol(raw.c_str(),&end,10);int field=-1;
  for(int i=0;i<25;i++)if(name==names[i])field=i;
  if(field<0||raw.isEmpty()||*end||value<minimum[field]||value>maximum[field]||(field==0&&value>1&&!psramFound())){doc["error"]="Unsupported camera control or value";json(request,doc,400);return;}
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
