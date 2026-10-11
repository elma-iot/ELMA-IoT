#include "spk_media.h"
#if defined(APP_SPK_BOARD) && !defined(APP_DISABLE_AUDIO) && !defined(APP_DISABLE_WEB_UI)
#include "led_microphone.h"
#include "storage_backend.h"
#include "camera_service.h"
#include "spk_avi.h"
#include <ArduinoJson.h>
#include <memory>
#include <vector>
#include <time.h>
#include <WiFi.h>
namespace {
SemaphoreHandle_t mutex;
std::atomic<int> command{0};std::atomic<bool> recording{false};std::atomic<uint32_t> listenUntil{0};
std::atomic<bool> videoRecording{false};
struct TalkBlock {uint8_t bytes[1024];uint16_t size=0;};QueueHandle_t talkQueue=nullptr;std::atomic<uint32_t> talkUntil{0};
String recordPath,error;uint32_t sequence=0;uint8_t preview[6400];size_t previewSize=0;
void wav(File& f,uint32_t bytes){
 uint8_t h[44]{};memcpy(h,"RIFF",4);memcpy(h+8,"WAVEfmt ",8);memcpy(h+36,"data",4);
 auto put=[&](int n,uint32_t v,int count){for(int i=0;i<count;i++)h[n+i]=v>>(i*8);};
 put(4,bytes+36,4);put(16,16,4);put(20,1,2);put(22,1,2);put(24,32000,4);put(28,64000,4);put(32,2,2);put(34,16,2);put(40,bytes,4);f.seek(0);f.write(h,44);
}
void worker(void*){
 File file;SpkAvi avi;std::vector<uint8_t> jpeg;uint32_t videoAt=0;uint32_t bytes=0,started=0;uint8_t pending[6400];size_t used=0;
 for(;;){
  int c=command.exchange(0);
  if((c==1||c==3)&&!file&&!avi.active()){
   xSemaphoreTake(mutex,portMAX_DELAY);error="";xSemaphoreGive(mutex);
   if(!storageMounted(StorageTarget::Sd)||storageBusy(StorageTarget::Sd)){xSemaphoreTake(mutex,portMAX_DELAY);error="Mount an idle SD card before recording";xSemaphoreGive(mutex);}
   else {beginStorageWrite(StorageTarget::Sd);auto* fs=getStorageFs(StorageTarget::Sd);if(fs&&!fs->exists("/record"))fs->mkdir("/record");
    String path="/record/mic-"+String(uint32_t(time(nullptr)))+"-"+String(millis())+(c==3?".avi":".wav");
    File opened=storageOpen(StorageTarget::Sd,path,"w");uint16_t width=0,height=0;bool cameraReady=c!=3||copyCameraFrame(jpeg,width,height);
    if(opened&&cameraReady){bytes=0;started=millis();if(c==3){avi.begin(opened,width,height);videoRecording=true;}else{file=opened;wav(file,0);file.seek(44);}recording=true;xSemaphoreTake(mutex,portMAX_DELAY);recordPath=path;xSemaphoreGive(mutex);}else {if(opened)opened.close();endStorageWrite(StorageTarget::Sd);xSemaphoreTake(mutex,portMAX_DELAY);error="Cannot create WAV recording";xSemaphoreGive(mutex);}
   }
  }
  if((file||avi.active())&&(c==2||!LedMicrophone::enabled()||LedMicrophone::status()!=1||millis()-started>1800000)){if(file){wav(file,bytes);file.close();}avi.end();videoRecording=false;endStorageWrite(StorageTarget::Sd);recording=false;}
  if(avi.active()&&uint32_t(millis()-videoAt)>=100){uint16_t width,height;if(copyCameraFrame(jpeg,width,height)&&!avi.video(jpeg.data(),jpeg.size()))command=2;videoAt=millis();}
  bool listening=int32_t(listenUntil.load()-millis())>0;
  if(file||avi.active()||listening)LedMicrophone::mediaUntil()=millis()+1000;
  LedMicrophone::PcmBlock block;
  if(LedMicrophone::pcm()&&xQueueReceive(LedMicrophone::pcm(),&block,pdMS_TO_TICKS(10))==pdTRUE){
   size_t count=block.count*2;
   if(file){if(file.write(reinterpret_cast<uint8_t*>(block.samples),count)!=count){command=2;xSemaphoreTake(mutex,portMAX_DELAY);error="SD write failed; recording stopped";xSemaphoreGive(mutex);}else bytes+=count;}
   if(avi.active()&&!avi.audio(reinterpret_cast<uint8_t*>(block.samples),count))command=2;
   if(listening){if(used+count<=sizeof(pending)){memcpy(pending+used,block.samples,count);used+=count;}
    if(used>=sizeof(pending)-128){xSemaphoreTake(mutex,portMAX_DELAY);memcpy(preview,pending,used);previewSize=used;sequence++;xSemaphoreGive(mutex);used=0;}
   }else used=0;
  }
 }
}
void json(AsyncWebServerRequest* r,JsonDocument& d,int code=200){String s;serializeJson(d,s);r->send(code,"application/json",s);}
}
void registerSpkMediaRoutes(AsyncWebServer& server,std::function<bool(AsyncWebServerRequest*)> auth){
 talkQueue=xQueueCreate(32,sizeof(TalkBlock));mutex=xSemaphoreCreateMutex();if(!mutex)return;if(xTaskCreate(worker,"spk-media",16384,nullptr,1,nullptr)!=pdPASS)return;
 server.on("/api/media/status",HTTP_GET,[auth](AsyncWebServerRequest* r){if(!auth(r))return;JsonDocument d;d["recording"]=recording.load();d["video"]=videoRecording.load();d["sd"]=storageMounted(StorageTarget::Sd);xSemaphoreTake(mutex,portMAX_DELAY);d["path"]=recordPath;d["error"]=error;xSemaphoreGive(mutex);json(r,d);});
 server.on("/api/media/record",HTTP_POST,[auth](AsyncWebServerRequest* r){if(!auth(r))return;JsonDocument d;
  String action=r->hasParam("action",true)?r->getParam("action",true)->value():"";
  if(action!="start"&&action!="video"&&action!="stop"){d["error"]="Invalid recording action";json(r,d,400);return;}
  if((action=="start"||action=="video")&&(!storageMounted(StorageTarget::Sd)||storageBusy(StorageTarget::Sd)||LedMicrophone::status()!=1||!LedMicrophone::enabled())){d["error"]="Enable a working microphone and mount an idle SD card";json(r,d,409);return;}
  command=action=="start"?1:action=="video"?3:2;d["accepted"]=true;json(r,d,202);
 });

 server.on("/api/media/talk/start",HTTP_POST,[auth](AsyncWebServerRequest* r){if(!auth(r))return;if(!talkQueue){r->send(503);return;}xQueueReset(talkQueue);talkUntil=millis()+5000;JsonDocument d;d["url"]="http://"+(WiFi.status()==WL_CONNECTED?WiFi.localIP():WiFi.softAPIP()).toString()+"/api/media/talk.wav";json(r,d);});
 server.on("/api/media/talk/stop",HTTP_POST,[auth](AsyncWebServerRequest* r){if(!auth(r))return;talkUntil=0;if(talkQueue)xQueueReset(talkQueue);r->send(200,"application/json","{\"ok\":true}");});
 server.on("/api/media/talk",HTTP_POST,[auth](AsyncWebServerRequest* r){if(!auth(r))return;r->send(talkUntil.load()?202:409,"application/json",talkUntil.load()?"{\"accepted\":true}":"{\"error\":\"Talk session ended\"}");},nullptr,
  [auth](AsyncWebServerRequest* r,uint8_t* data,size_t len,size_t index,size_t total){if(!auth(r)||!talkQueue||total>8192||total%2||len%2||int32_t(talkUntil.load()-millis())<=0)return;
   talkUntil=millis()+5000;for(size_t i=0;i<len;i+=1024){TalkBlock b;b.size=std::min(size_t(1024),len-i);memcpy(b.bytes,data+i,b.size);if(xQueueSend(talkQueue,&b,0)!=pdTRUE)break;}
  });
 server.on("/api/media/talk.wav",HTTP_GET,[auth](AsyncWebServerRequest* r){if(!auth(r))return;if(!talkQueue||int32_t(talkUntil.load()-millis())<=0){r->send(409);return;}
  struct Stream {TalkBlock block;size_t offset=0;};auto stream=std::make_shared<Stream>();
  auto* response=r->beginChunkedResponse("audio/wav",[stream](uint8_t* out,size_t max,size_t index)->size_t{
   if(int32_t(talkUntil.load()-millis())<=0)return 0;
   if(index<44){uint8_t h[44]={};memcpy(h,"RIFF",4);memcpy(h+8,"WAVEfmt ",8);memcpy(h+36,"data",4);auto put=[&](int n,uint32_t value,int count){for(int i=0;i<count;i++)h[n+i]=value>>(i*8);};put(4,0x7fffffff,4);put(16,16,4);put(20,1,2);put(22,1,2);put(24,16000,4);put(28,32000,4);put(32,2,2);put(34,16,2);put(40,0x7fffffd0,4);size_t n=std::min(max,size_t(44-index));memcpy(out,h+index,n);return n;}
   if(stream->offset==stream->block.size){if(xQueueReceive(talkQueue,&stream->block,0)!=pdTRUE)return RESPONSE_TRY_AGAIN;stream->offset=0;}size_t n=std::min(max,size_t(stream->block.size-stream->offset));memcpy(out,stream->block.bytes+stream->offset,n);stream->offset+=n;return n;
  });response->addHeader("Cache-Control","no-store");r->send(response);
 });
 server.on("/api/media/pcm",HTTP_GET,[auth](AsyncWebServerRequest* r){if(!auth(r))return;listenUntil=millis()+1500;
  auto data=std::make_shared<std::vector<uint8_t>>();uint32_t seq;xSemaphoreTake(mutex,portMAX_DELAY);data->assign(preview,preview+previewSize);seq=sequence;xSemaphoreGive(mutex);
  if(r->hasParam("after")&&r->getParam("after")->value().toInt()==seq){r->send(204);return;}
  auto* response=r->beginResponse("application/octet-stream",data->size(),[data](uint8_t* out,size_t max,size_t index){size_t n=index<data->size()?std::min(max,data->size()-index):0;if(n)memcpy(out,data->data()+index,n);return n;});response->addHeader("Cache-Control","no-store");response->addHeader("X-PCM-Sequence",String(seq));r->send(response);
 });
}
#endif
