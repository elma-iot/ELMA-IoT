#include "motion_capture.h"
#if defined(APP_SPK_BOARD) && !defined(APP_DISABLE_AUDIO) && !defined(APP_DISABLE_WEB_UI)
#include "camera_service.h"
#include "interface_security.h"
#include "spk_media.h"
#include "bno055_service.h"
#include "storage_backend.h"
#include "person_detect_model_data.h"
#include "person_model_fixtures.h"
#include <TensorFlowLite_ESP32.h>
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include <img_converters.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <atomic>
#include <cmath>
namespace MotionCapture { namespace {
struct Config {bool enabled=false,human=true,picture=true,video=false,telemetry=false;uint16_t seconds=15;uint8_t humanSensitivity=50,telemetrySensitivity=50;};
Config config;SemaphoreHandle_t lock=nullptr;std::atomic<bool> detectorRunning{false};std::atomic<uint32_t> detectorStackFree{0};
std::atomic<bool> enabled{false},human{true},ota{false},humanEvent{false};std::atomic<int> sensitivity{50},score{0};
std::atomic<uint32_t> inferenceMs{0};std::atomic<bool> runtimeReady{false};
std::atomic<int> validationPersonScore{-1},validationOtherScore{-1};
bool telemetryAvailable=false,telemetryReady=false,telemetryBaseline=false;float previousQuaternion[4]{};
uint32_t nextTelemetry=0,lastCapture=0,captures=0;String lastReason,lastError;
Config readConfig(){xSemaphoreTake(lock,portMAX_DELAY);Config c=config;xSemaphoreGive(lock);return c;}
void error(const String& message){xSemaphoreTake(lock,portMAX_DELAY);lastError=message;xSemaphoreGive(lock);}
// Compact versioned storage leaves the small shared NVS partition available to device settings.
bool save(const Config& c){Preferences p;if(!p.begin("elma-motion",false))return false;
 uint8_t b[6]={1,uint8_t(c.enabled|(c.human<<1)|(c.picture<<2)|(c.video<<3)|(c.telemetry<<4)),uint8_t(c.seconds),uint8_t(c.seconds>>8),c.humanSensitivity,c.telemetrySensitivity};
 bool ok=p.putBytes("packed",b,sizeof(b))==sizeof(b);if(ok)p.remove("config");p.end();return ok;
}
void detector(void*) {
 // Bulk tensors and downscaled image live in PSRAM, not the TLS/DMA reserve.
 constexpr size_t arenaSize=160*1024;uint8_t* arena=nullptr;uint8_t* rgb=nullptr;size_t rgbCapacity=0;
 tflite::MicroErrorReporter reporter;tflite::MicroMutableOpResolver<5> resolver;
 resolver.AddAveragePool2D();resolver.AddConv2D();resolver.AddDepthwiseConv2D();resolver.AddReshape();resolver.AddSoftmax();
 tflite::MicroInterpreter* interpreter=nullptr;unsigned streak=0,absent=0;bool latched=false;
 for(;;){
  detectorStackFree=uxTaskGetStackHighWaterMark(nullptr);
  if(!enabled.load()||!human.load()||ota.load()){
   delete interpreter;heap_caps_free(arena);heap_caps_free(rgb);runtimeReady=false;score=0;detectorStackFree=0;detectorRunning=false;vTaskDelete(nullptr);return;
  }
  if(!enabled.load()||!human.load()||ota.load()||spkMediaRecording()){if(!enabled.load()){streak=absent=0;latched=false;}vTaskDelay(pdMS_TO_TICKS(500));continue;}
  if(!interpreter){
   arena=static_cast<uint8_t*>(heap_caps_malloc(arenaSize,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
   const auto* model=tflite::GetModel(g_person_detect_model_data);
   if(!arena||model->version()!=TFLITE_SCHEMA_VERSION){heap_caps_free(arena);arena=nullptr;error("Person detector memory/model unavailable");vTaskDelay(pdMS_TO_TICKS(5000));continue;}
   interpreter=new(std::nothrow)tflite::MicroInterpreter(model,resolver,arena,arenaSize,&reporter);
   if(!interpreter||interpreter->AllocateTensors()!=kTfLiteOk){delete interpreter;interpreter=nullptr;heap_caps_free(arena);arena=nullptr;error("Person detector tensor allocation failed");vTaskDelay(pdMS_TO_TICKS(5000));continue;}
   auto validate=[&](const uint8_t* image){auto* input=interpreter->input(0);if(input->bytes!=9216||input->type!=kTfLiteInt8)return -1;for(unsigned i=0;i<9216;i++)input->data.int8[i]=int(image[i])-128;if(interpreter->Invoke()!=kTfLiteOk)return -1;auto* out=interpreter->output(0);return constrain(int((int(out->data.int8[1])-out->params.zero_point)*out->params.scale*100),0,100);};
   validationPersonScore=validate(personFixture);vTaskDelay(1);validationOtherScore=validate(otherFixture);
   runtimeReady=validationPersonScore.load()>=55&&validationPersonScore.load()>validationOtherScore.load()+20;
   if(!runtimeReady.load()){error("Person model validation failed; human trigger disabled");}
  }
  if(!runtimeReady.load()){vTaskDelay(pdMS_TO_TICKS(5000));continue;}
  bool decoded=withCameraFrame([&](const uint8_t* jpeg,size_t length,uint16_t width,uint16_t height){
   int scale=0;while(scale<3&&(width>>scale)>192)++scale;
   size_t w=width>>scale,h=height>>scale,bytes=w*h*2;if(!w||!h)return false;
   if(bytes>rgbCapacity){heap_caps_free(rgb);rgb=static_cast<uint8_t*>(heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));rgbCapacity=rgb?bytes:0;}
   if(!rgb||!jpg2rgb565(jpeg,length,rgb,static_cast<jpg_scale_t>(scale)))return false;
   auto* input=interpreter->input(0);if(input->bytes!=96*96||input->type!=kTfLiteInt8)return false;
   for(unsigned y=0;y<96;y++)for(unsigned x=0;x<96;x++){size_t at=((y*h/96)*w+x*w/96)*2;uint16_t pixel=(uint16_t(rgb[at])<<8)|rgb[at+1];unsigned red=(pixel>>11)*255/31,green=((pixel>>5)&63)*255/63,blue=(pixel&31)*255/31;input->data.int8[y*96+x]=int((red*77+green*150+blue*29)>>8)-128;}
   return true;
  });
  if(decoded&&runtimeReady.load()){uint32_t start=millis();if(interpreter->Invoke()==kTfLiteOk){
    auto* output=interpreter->output(0);float probability=(int(output->data.int8[1])-output->params.zero_point)*output->params.scale;
    score=constrain(int(probability*100),0,100);inferenceMs=millis()-start;
    bool present=score.load()>=90-sensitivity.load()*35/100;
    if(present){absent=0;if(++streak>=2&&!latched){humanEvent=true;latched=true;}}
    else{streak=0;if(++absent>=3)latched=false;}
   }else error("Person inference failed");
  }
  // Keep radio, capture, audio and OTA responsive; no full-frame RGB buffer.
  vTaskDelay(pdMS_TO_TICKS(1000));
 }
}
void status(JsonObject d){Config c=readConfig();d["supported"]=true;d["stackFreeBytes"]=detectorStackFree.load();d["enabled"]=c.enabled;d["human"]=c.human;d["picture"]=c.picture;d["video"]=c.video;d["seconds"]=c.seconds;d["telemetry"]=c.telemetry;d["humanSensitivity"]=c.humanSensitivity;d["telemetrySensitivity"]=c.telemetrySensitivity;d["telemetryAvailable"]=telemetryAvailable;d["telemetryReady"]=telemetryReady;d["runtimeReady"]=runtimeReady.load();d["personScore"]=score.load();d["validationPersonScore"]=validationPersonScore.load();d["validationOtherScore"]=validationOtherScore.load();d["inferenceMs"]=inferenceMs.load();d["engine"]="TFLite Micro int8 person presence 96x96";d["folder"]="/security";d["cooldownSeconds"]=30;d["captures"]=captures;xSemaphoreTake(lock,portMAX_DELAY);d["lastTrigger"]=lastReason;d["error"]=lastError;xSemaphoreGive(lock);}
}
void begin(){if(lock)return;lock=xSemaphoreCreateMutex();if(!lock)return;Preferences p;if(p.begin("elma-motion",true)){JsonDocument d;if(!deserializeJson(d,p.getString("config","{}"))){config.enabled=d["enabled"]|false;config.human=d["human"]|true;config.picture=d["picture"]|true;config.video=d["video"]|false;config.telemetry=d["telemetry"]|false;config.seconds=constrain(d["seconds"]|15,1,300);config.humanSensitivity=constrain(d["humanSensitivity"]|50,0,100);config.telemetrySensitivity=constrain(d["telemetrySensitivity"]|50,0,100);}uint8_t b[6];if(p.getBytesLength("packed")==sizeof(b)&&p.getBytes("packed",b,sizeof(b))==sizeof(b)&&b[0]==1){config.enabled=b[1]&1;config.human=b[1]&2;config.picture=b[1]&4;config.video=b[1]&8;config.telemetry=b[1]&16;config.seconds=constrain(unsigned(b[2])|(unsigned(b[3])<<8),1,300);config.humanSensitivity=constrain(b[4],0,100);config.telemetrySensitivity=constrain(b[5],0,100);}p.end();}enabled=config.enabled;human=config.human;sensitivity=config.humanSensitivity;}
void tick(bool otaActive){if(!lock)return;ota=otaActive;Config c=readConfig();if(c.enabled&&c.human&&!otaActive&&!detectorRunning.load()){if(!psramFound())error("Person detection requires PSRAM");else{detectorRunning=true;if(xTaskCreatePinnedToCore(detector,"person-detect",8192,nullptr,1,nullptr,1)!=pdPASS){detectorRunning=false;error("Cannot start person detector");}}}
 bool telemetryEvent=false;
 if(int32_t(millis()-nextTelemetry)>=0){nextTelemetry=millis()+100;JsonDocument d;Bno055::snapshot(d.to<JsonObject>());telemetryAvailable=d["configured"]|false;telemetryReady=(d["ready"]|false)&&(d["ageMs"]|9999)<1000;
  if(telemetryReady){float q[4],norm=0,dot=0,acceleration=0;
   for(unsigned i=0;i<4;i++){q[i]=d["quaternion"][i]|0.f;norm+=q[i]*q[i];}
   if(norm<0.5f||!std::isfinite(norm)){telemetryBaseline=false;}
   else {norm=sqrtf(norm);for(unsigned i=0;i<4;i++){q[i]/=norm;dot+=q[i]*previousQuaternion[i];}
    for(unsigned i=0;i<3;i++){float value=d["linear"][i]|0.f;acceleration+=value*value;}
    float angle=2*acosf(std::min(1.f,fabsf(dot)))*180/3.14159265f;
    telemetryEvent=c.telemetry&&telemetryBaseline&&(angle>=30-c.telemetrySensitivity*.28f||sqrtf(acceleration)>=3-c.telemetrySensitivity*.028f);
    // Keep a reference orientation so slow rotation also triggers; reset after movement.
    if(!telemetryBaseline||telemetryEvent||!c.enabled||!c.telemetry)for(unsigned i=0;i<4;i++)previousQuaternion[i]=q[i];
    telemetryBaseline=true;
   }
  }else telemetryBaseline=false;
 }
 bool personEvent=humanEvent.exchange(false);
 if(c.enabled&&!otaActive&&(personEvent||telemetryEvent)&&(!lastCapture||millis()-lastCapture>=30000)){
  if(triggerSpkSecurityCapture(c.picture,c.video,c.seconds)){lastCapture=millis();++captures;xSemaphoreTake(lock,portMAX_DELAY);lastReason=personEvent?"person":"telemetry";lastError="";xSemaphoreGive(lock);}else error("Capture deferred: microphone, recording or SD unavailable");
 }
}
bool armed(){return enabled.load();}
void routes(AsyncWebServer& server,std::function<bool(AsyncWebServerRequest*)> auth){begin();
 server.on("/api/security/capture",HTTP_GET,[auth](AsyncWebServerRequest* r){if(!auth(r))return;JsonDocument d;status(d.to<JsonObject>());String s;serializeJson(d,s);r->send(200,"application/json",s);});
 server.on("/api/security/capture",HTTP_POST,[auth](AsyncWebServerRequest* r){if(!auth(r))return;if(r->hasHeader("Origin")&&r->header("Origin")!="http://"+r->host()&&r->header("Origin")!="https://"+r->host()){r->send(403);return;}Config c=readConfig();
  if(r->hasParam("action",true)&&r->getParam("action",true)->value()=="test"){
   if(triggerSpkSecurityCapture(c.picture,c.video,c.seconds))r->send(202,"application/json","{\"ok\":true}");
   else r->send(409,"application/json","{\"error\":\"Capture unavailable: camera, microphone or SD busy\"}");return;
  }
  auto boolean=[&](const char* name,bool& value){if(r->hasParam(name,true)){String text=r->getParam(name,true)->value();if(text!="0"&&text!="1")return false;value=text=="1";}return true;};
  if(!boolean("enabled",c.enabled)||!boolean("human",c.human)||!boolean("picture",c.picture)||!boolean("video",c.video)||!boolean("telemetry",c.telemetry)){r->send(400);return;}
  for(const char* name:{"seconds","humanSensitivity","telemetrySensitivity"})if(r->hasParam(name,true)){String text=r->getParam(name,true)->value();char* end;long value=strtol(text.c_str(),&end,10);if(text.isEmpty()||*end||value<0||value>(String(name)=="seconds"?300:100)||(String(name)=="seconds"&&value<1)){r->send(400);return;}if(String(name)=="seconds")c.seconds=value;else if(String(name)=="humanSensitivity")c.humanSensitivity=value;else c.telemetrySensitivity=value;}
  if(c.enabled&&((!c.picture&&!c.video)||(!c.human&&!c.telemetry)||(c.telemetry&&!telemetryAvailable)||!storageMounted(StorageTarget::Sd))){r->send(409,"application/json","{\"error\":\"Choose a capture type and available trigger, and mount SD\"}");return;}
  if(readConfig().enabled&&!c.enabled){
   JsonDocument response;String pin=r->hasParam("pin",true)?r->getParam("pin",true)->value():"";
   int code=InterfaceSecurity::device().authorizeProtectedChange(pin,response.to<JsonObject>());pin="";
   if(code!=200){String body;serializeJson(response,body);r->send(code,"application/json",body);return;}
  }
  if(!save(c)){r->send(507);return;}xSemaphoreTake(lock,portMAX_DELAY);config=c;lastError="";xSemaphoreGive(lock);enabled=c.enabled;human=c.human;sensitivity=c.humanSensitivity;humanEvent=false;r->send(200,"application/json","{\"ok\":true}");
 });
}
}
#endif
