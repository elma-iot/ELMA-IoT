#include "spk_media.h"
#if defined(APP_SPK_BOARD) && !defined(APP_DISABLE_AUDIO) && !defined(APP_DISABLE_WEB_UI)
#include "led_microphone.h"
#include "storage_backend.h"
#include "camera_service.h"
#include "spk_avi.h"
#include "audio_player.h"
#include "settings_schema.h"
#include "psram_allocator.h"
#include <ArduinoJson.h>
#include <memory>
#include <vector>
#include <time.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
namespace {
SemaphoreHandle_t mutex,framesMutex;
struct FramesGuard {FramesGuard(){xSemaphoreTakeRecursive(framesMutex,portMAX_DELAY);}~FramesGuard(){xSemaphoreGiveRecursive(framesMutex);}};
std::atomic<int> command{0};std::atomic<bool> recording{false};std::atomic<uint32_t> listenUntil{0};
std::atomic<bool> videoRecording{false};
std::atomic<bool> mediaAudioReady{false},securityPicture{false},recordRequested{false},photoRequested{false};
std::atomic<uint32_t> listenAudioUntil{0};std::atomic<uint32_t> securitySeconds{15};
std::atomic<unsigned> cueRequest{0},cueComplete{0};std::atomic<bool> cueSuccess{false};
String photoPath;
struct TalkBlock {uint8_t bytes[1024];uint16_t size=0;};QueueHandle_t talkQueue=nullptr;std::atomic<uint32_t> talkUntil{0};
StaticQueue_t talkQueueState;String talkToken;
AsyncWebSocket mediaSocket("/api/media/ws");
std::function<bool()> streamAllowed;
std::atomic<unsigned> mediaClients{0},talkPcmFrames{0},talkPcmErrors{0};
struct MediaFrame {uint32_t id=0;uint8_t* bytes=nullptr;size_t received=0;uint32_t lastSent=UINT32_MAX;uint32_t subscribedUntil=0;};MediaFrame frames[2];
void pushTalk(const uint8_t* data,size_t length){
 while(uxQueueMessagesWaiting(talkQueue)>3){TalkBlock stale;xQueueReceive(talkQueue,&stale,0);}
 for(size_t i=0;i<length;i+=1024){TalkBlock block;block.size=std::min(size_t(1024),length-i);memcpy(block.bytes,data+i,block.size);if(xQueueSend(talkQueue,&block,0)!=pdTRUE)break;}
}
String recordPath,error;uint32_t sequence=0;uint8_t preview[512];size_t previewSize=0;
void wav(File& f,uint32_t bytes,uint32_t rate){
 uint8_t h[44]{};memcpy(h,"RIFF",4);memcpy(h+8,"WAVEfmt ",8);memcpy(h+36,"data",4);
 auto put=[&](int n,uint32_t v,int count){for(int i=0;i<count;i++)h[n+i]=v>>(i*8);};
 put(4,bytes+36,4);put(16,16,4);put(20,1,2);put(22,1,2);put(24,rate,4);put(28,rate*2,4);put(32,2,2);put(34,16,2);put(40,bytes,4);f.seek(0);f.write(h,44);
}
void worker(void*){
 File file;SpkAvi avi;CameraFrameBytes jpeg;uint32_t videoAt=0;uint32_t bytes=0,started=0,recordLimit=1800000,recordRate=16000;struct Scratch {uint8_t pending[512],recorded[4096];};
 auto* scratch=allocatePreferPsram<Scratch>();if(!scratch){vTaskDelete(nullptr);return;}
 auto& pending=scratch->pending;auto& recorded=scratch->recorded;size_t used=0,recordedSize=0;
 auto flushAudio=[&](){if(!recordedSize)return;bool ok=true;if(file){ok=file.write(recorded,recordedSize)==recordedSize;if(ok)bytes+=recordedSize;}else if(avi.active())ok=avi.audio(recorded,recordedSize);recordedSize=0;if(!ok){command=2;xSemaphoreTake(mutex,portMAX_DELAY);error="SD write failed; recording stopped";xSemaphoreGive(mutex);}};
 for(;;){
  int c=command.exchange(0);
  bool security=c==5||c==6,captureCuePlayed=false;
  if(c==4 || c==6 || security){
   bool picture=c==4||c==6||securityPicture.load();
   if(picture && storageMounted(StorageTarget::Sd) && !storageWriteBlocked(StorageTarget::Sd)){
    withCameraFrame([](const uint8_t*,size_t,uint16_t,uint16_t){return false;});
    unsigned cue=cueRequest.fetch_add(1)+1;uint32_t wait=millis();while(cueComplete.load()!=cue&&millis()-wait<2000)vTaskDelay(1);
    jpeg.clear();uint16_t w,h;uint32_t warm=millis();while(!copyCameraFrame(jpeg,w,h)&&millis()-warm<5000)vTaskDelay(pdMS_TO_TICKS(20));
    captureCuePlayed=cueComplete.load()==cue&&cueSuccess.load();
    if(captureCuePlayed&&!jpeg.empty()){
     String dir=security?"/security":"/record";beginStorageWrite(StorageTarget::Sd);auto* fs=getStorageFs(StorageTarget::Sd);if(fs&&!fs->exists(dir))fs->mkdir(dir);
     String path=dir+"/photo-"+String(uint32_t(time(nullptr)))+"-"+String(millis())+".jpg";File photo=storageOpen(StorageTarget::Sd,path,"w");bool ok=photo&&photo.write(jpeg.data(),jpeg.size())==jpeg.size();if(photo)photo.close();endStorageWrite(StorageTarget::Sd);
     xSemaphoreTake(mutex,portMAX_DELAY);if(ok){photoPath=path;error="";}else error="Photo SD write failed";xSemaphoreGive(mutex);
    }else{xSemaphoreTake(mutex,portMAX_DELAY);error=cueComplete.load()!=cue?"Capture aborted: shutter timed out":!cueSuccess.load()?"Capture aborted: speaker/shutter unavailable":"Capture aborted: camera frame unavailable";xSemaphoreGive(mutex);}
   }else if(picture){xSemaphoreTake(mutex,portMAX_DELAY);error="Photo unavailable: SD removed or busy";xSemaphoreGive(mutex);}
   photoRequested=false;if(c==4||c==6)c=0;else c=3;
  }
  if((c==1||c==3)&&!file&&!avi.active()){
   uint32_t wait=millis();while(!mediaAudioReady.load()&&millis()-wait<1500)vTaskDelay(1);
   if(c==3&&!captureCuePlayed){unsigned cue=cueRequest.fetch_add(1)+1;uint32_t wait=millis();while(cueComplete.load()!=cue&&millis()-wait<2000)vTaskDelay(1);captureCuePlayed=cueComplete.load()==cue&&cueSuccess.load();if(!captureCuePlayed){recordRequested=false;xSemaphoreTake(mutex,portMAX_DELAY);error="Video aborted: audible capture cue unavailable";xSemaphoreGive(mutex);c=0;}}
   xSemaphoreTake(mutex,portMAX_DELAY);if(c)error="";xSemaphoreGive(mutex);
   if(!c){}else if(!mediaAudioReady.load()||!storageMounted(StorageTarget::Sd)||storageWriteBlocked(StorageTarget::Sd)){xSemaphoreTake(mutex,portMAX_DELAY);error="Mount an idle SD card before recording";recordRequested=false;xSemaphoreGive(mutex);}
   else {beginStorageWrite(StorageTarget::Sd);auto* fs=getStorageFs(StorageTarget::Sd);String dir=security?"/security":"/record";if(fs&&!fs->exists(dir))fs->mkdir(dir);
    String path=dir+"/mic-"+String(uint32_t(time(nullptr)))+"-"+String(millis())+(c==3?".avi":".wav");
    File opened=storageOpen(StorageTarget::Sd,path,"w");uint16_t width=0,height=0;bool cameraReady=c!=3;if(c==3){uint32_t warm=millis();while(!(cameraReady=copyCameraFrame(jpeg,width,height))&&millis()-warm<5000)vTaskDelay(pdMS_TO_TICKS(20));}
    if(opened&&cameraReady){recordedSize=0;if(LedMicrophone::pcm())xQueueReset(LedMicrophone::pcm());LedMicrophone::pcmDropped()=0;bytes=0;recordRate=LedMicrophone::sampleRate().load();started=millis();recordLimit=security?securitySeconds.load()*1000:1800000;if(c==3){avi.begin(opened,width,height,recordRate);videoRecording=true;}else{file=opened;wav(file,0,recordRate);file.seek(44);}recording=true;xSemaphoreTake(mutex,portMAX_DELAY);recordPath=path;xSemaphoreGive(mutex);}else {if(opened)opened.close();endStorageWrite(StorageTarget::Sd);xSemaphoreTake(mutex,portMAX_DELAY);error="Cannot create audio/video recording";recordRequested=false;xSemaphoreGive(mutex);}
   }
  }
  if((file||avi.active())&&(c==2||recordRate!=LedMicrophone::sampleRate().load()||!LedMicrophone::enabled()||LedMicrophone::status()!=1||millis()-started>recordLimit)){flushAudio();if(file){wav(file,bytes,recordRate);file.close();}avi.end();videoRecording=false;endStorageWrite(StorageTarget::Sd);recording=false;recordRequested=false;}
  if(avi.active()&&uint32_t(millis()-videoAt)>=100){uint16_t width,height;if(copyCameraFrame(jpeg,width,height)&&!avi.video(jpeg.data(),jpeg.size()))command=2;videoAt=millis();}
  bool allowed=streamAllowed && streamAllowed();
  if(!allowed)talkUntil=0;
  bool listening=allowed && int32_t(listenUntil.load()-millis())>0;
  if(file||avi.active()||listening)LedMicrophone::mediaUntil()=millis()+1000;
  LedMicrophone::PcmBlock block;
  if(LedMicrophone::pcm()&&xQueueReceive(LedMicrophone::pcm(),&block,pdMS_TO_TICKS(10))==pdTRUE){
   unsigned batch=0;do {size_t count=block.count*2;
   if(file||avi.active()){if(recordedSize+count>sizeof(recorded))flushAudio();memcpy(recorded+recordedSize,block.samples,count);recordedSize+=count;if(recordedSize==sizeof(recorded))flushAudio();}
   if(listening){if(used+count<=sizeof(pending)){memcpy(pending+used,block.samples,count);used+=count;}
    if(used>=sizeof(pending)){xSemaphoreTake(mutex,portMAX_DELAY);memcpy(preview,pending,used);previewSize=used;sequence++;xSemaphoreGive(mutex);
     used=0;}
    FramesGuard framesGuard;for(auto& client:frames)if(client.id&&int32_t(client.subscribedUntil-millis())>0){auto* ws=mediaSocket.client(client.id);if(ws&&ws->canSend()&&ws->queueLen()<2&&client.lastSent!=sequence){xSemaphoreTake(mutex,portMAX_DELAY);ws->binary(preview,previewSize);client.lastSent=sequence;xSemaphoreGive(mutex);}}
   }else used=0;
   }while(++batch<64&&xQueueReceive(LedMicrophone::pcm(),&block,0)==pdTRUE);
  }
 }
}
void json(AsyncWebServerRequest* r,JsonDocument& d,int code=200){String s;serializeJson(d,s);r->send(code,"application/json",s);}
}
void registerSpkMediaRoutes(AsyncWebServer& server,std::function<bool(AsyncWebServerRequest*)> auth,std::function<bool()> allowed){
 auto* queueMemory=static_cast<uint8_t*>(heap_caps_malloc(8*sizeof(TalkBlock),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 talkQueue=queueMemory?xQueueCreateStatic(8,sizeof(TalkBlock),queueMemory,&talkQueueState):xQueueCreate(8,sizeof(TalkBlock));
 streamAllowed=allowed;mutex=xSemaphoreCreateMutex();framesMutex=xSemaphoreCreateRecursiveMutex();if(!mutex||!framesMutex)return;if(xTaskCreate(worker,"spk-media",6144,nullptr,1,nullptr)!=pdPASS)return;
 mediaSocket.addMiddleware([auth](AsyncWebServerRequest* r,ArMiddlewareNext next){
  if(!auth(r))return;
  if(r->hasHeader("Origin")){String origin=r->header("Origin"),host=r->host();if(origin!="http://"+host && origin!="https://"+host){r->send(403);return;}}
  mediaSocket.cleanupClients(2);mediaSocket.cleanupClients(2);
  if(mediaSocket.count()>=2){r->send(503);return;}next();
 });
 mediaSocket.onEvent([allowed](AsyncWebSocket*,AsyncWebSocketClient* client,AwsEventType event,void* arg,uint8_t* data,size_t length){
  FramesGuard framesGuard;
  if(event==WS_EVT_CONNECT){
   for(auto& frame:frames)if(!frame.id){frame.bytes=static_cast<uint8_t*>(heap_caps_malloc(8192,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));if(!frame.bytes){client->close(1013,"Memory busy");return;}frame.id=client->id();frame.received=0;++mediaClients;return;}
   client->close(1013,"Audio client limit");return;
  }
  if(event==WS_EVT_DISCONNECT){for(auto& frame:frames)if(frame.id==client->id()){heap_caps_free(frame.bytes);frame={};--mediaClients;}return;}
  if(event!=WS_EVT_DATA)return;
  auto* frame=static_cast<AwsFrameInfo*>(arg);
  if(!allowed()){client->close(1008,"Device locked");return;}
  if(frame->opcode==WS_TEXT && frame->final && !frame->index && ((length==3&&!memcmp(data,"pcm",3))||(length==6&&!memcmp(data,"listen",6)))){
   if(length==6)listenAudioUntil=millis()+1500;
   listenUntil=millis()+1500;for(auto& subscription:frames)if(subscription.id==client->id())subscription.subscribedUntil=millis()+1500;
   return;
  }
  if(!talkQueue||int32_t(talkUntil.load()-millis())<=0){client->close(1008,"Talk is inactive");return;}
  if(!frame->final||frame->opcode!=WS_BINARY||frame->len>8192||frame->len%2||frame->index+length>frame->len){client->close(1009,"Invalid PCM frame");return;}
  for(auto& incoming:frames)if(incoming.id==client->id()){
   if(!frame->index)incoming.received=0;
   if(incoming.received!=frame->index){client->close(1009,"Out of order PCM");return;}
   memcpy(incoming.bytes+incoming.received,data,length);incoming.received+=length;
   if(incoming.received==frame->len){talkUntil=millis()+5000;pushTalk(incoming.bytes,incoming.received);}return;
  }
 });
 server.addHandler(&mediaSocket);
 server.on("/api/media/status",HTTP_GET,[auth](AsyncWebServerRequest* r){if(!auth(r))return;JsonDocument d;d["recording"]=recording.load();d["video"]=videoRecording.load();d["directPcm"]=true;d["micDroppedBlocks"]=LedMicrophone::pcmDropped().load();d["talkPcmFrames"]=talkPcmFrames.load();d["talkPcmErrors"]=talkPcmErrors.load();d["capturingPhoto"]=photoRequested.load();d["recordingPending"]=recordRequested.load()&&!recording.load();d["sampleRate"]=LedMicrophone::sampleRate().load();d["pcmBlockMs"]=256000/LedMicrophone::sampleRate().load();d["talkQueueMs"]=128;d["audioHeld"]=mediaAudioReady.load();d["talking"]=int32_t(talkUntil.load()-millis())>0;d["listening"]=int32_t(listenAudioUntil.load()-millis())>0;d["sd"]=storageMounted(StorageTarget::Sd);xSemaphoreTake(mutex,portMAX_DELAY);d["path"]=recordPath;d["photoPath"]=photoPath;d["error"]=error;xSemaphoreGive(mutex);json(r,d);});
 server.on("/api/media/record",HTTP_POST,[auth](AsyncWebServerRequest* r){if(!auth(r))return;JsonDocument d;
  String action=r->hasParam("action",true)?r->getParam("action",true)->value():"";
  if(action!="start"&&action!="video"&&action!="stop"){d["error"]="Invalid recording action";json(r,d,400);return;}
  if((action=="start"||action=="video")&&(!storageMounted(StorageTarget::Sd)||storageWriteBlocked(StorageTarget::Sd)||LedMicrophone::status()!=1||!LedMicrophone::enabled())){d["error"]="Enable a working microphone and mount an idle SD card";json(r,d,409);return;}
  if(action!="stop"&&(recording.load()||command.load())){r->send(409);return;}if(action!="stop")recordRequested=true;command=action=="start"?1:action=="video"?3:2;d["accepted"]=true;json(r,d,202);
 });

 server.on("/api/media/photo",HTTP_POST,[auth](AsyncWebServerRequest* r){if(!auth(r))return;if(recording.load()||recordRequested.load()||photoRequested.load()||command.load()||!storageMounted(StorageTarget::Sd)||storageWriteBlocked(StorageTarget::Sd)){r->send(409,"application/json","{\"error\":\"Mount an idle SD card before capturing\"}");return;}photoRequested=true;command=4;r->send(202,"application/json","{\"accepted\":true}");});
 server.on("/api/media/talk/start",HTTP_POST,[auth](AsyncWebServerRequest* r){if(!auth(r))return;if(!talkQueue){r->send(503);return;}xQueueReset(talkQueue);talkPcmFrames=0;talkPcmErrors=0;xSemaphoreTake(mutex,portMAX_DELAY);error="";xSemaphoreGive(mutex);talkUntil=millis()+5000;
  uint8_t random[16];esp_fill_random(random,sizeof(random));char token[33];for(size_t i=0;i<16;i++)snprintf(token+i*2,3,"%02x",random[i]);talkToken=token;
  JsonDocument d;d["direct"]=true;d["url"]="http://127.0.0.1/api/media/talk.wav?session="+talkToken;json(r,d);});
 server.on("/api/media/listen/stop",HTTP_POST,[auth](AsyncWebServerRequest* r){if(!auth(r))return;listenUntil=0;listenAudioUntil=0;FramesGuard guard;for(auto& frame:frames)frame.subscribedUntil=0;r->send(200,"application/json","{\"ok\":true}");});
 server.on("/api/media/talk/stop",HTTP_POST,[auth](AsyncWebServerRequest* r){if(!auth(r))return;talkUntil=0;if(talkQueue)xQueueReset(talkQueue);r->send(200,"application/json","{\"ok\":true}");});
 server.on("/api/media/talk",HTTP_POST,[auth](AsyncWebServerRequest* r){if(!auth(r))return;r->send(talkUntil.load()?202:409,"application/json",talkUntil.load()?"{\"accepted\":true}":"{\"error\":\"Talk session ended\"}");},nullptr,
  [auth](AsyncWebServerRequest* r,uint8_t* data,size_t len,size_t index,size_t total){if(!auth(r)||!talkQueue||total>8192||total%2||len%2||int32_t(talkUntil.load()-millis())<=0)return;
   talkUntil=millis()+5000;for(size_t i=0;i<len;i+=1024){TalkBlock b;b.size=std::min(size_t(1024),len-i);memcpy(b.bytes,data+i,b.size);if(xQueueSend(talkQueue,&b,0)!=pdTRUE)break;}
  });
 server.on("/api/media/talk.wav",HTTP_GET,[auth,allowed](AsyncWebServerRequest* r){
  bool tokenValid=allowed() && talkToken.length() && r->hasParam("session") && r->getParam("session")->value()==talkToken && int32_t(talkUntil.load()-millis())>0;
  if(!tokenValid && !auth(r))return;if(!talkQueue||int32_t(talkUntil.load()-millis())<=0){r->send(409);return;}
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

bool spkMediaRecording(){return recording.load();}
bool triggerSpkSecurityCapture(bool picture,bool video,uint32_t seconds){
 if(!picture&&!video||recording.load()||command.load()||!storageMounted(StorageTarget::Sd)||storageWriteBlocked(StorageTarget::Sd))return false;
 if(video&&(!LedMicrophone::enabled()||LedMicrophone::status()!=1))return false;
 securityPicture=picture;securitySeconds=constrain(seconds,uint32_t(1),uint32_t(300));if(video)recordRequested=true;if(picture)photoRequested=true;command=video?5:6;return true;
}
void stopSpkMediaForUpdate(){command=2;recordRequested=false;photoRequested=false;talkUntil=0;listenUntil=0;listenAudioUntil=0;if(talkQueue)xQueueReset(talkQueue);}
void serviceSpkMediaAudio(AudioPlayer& audio,const SettingsBundle& settings){
 int pending=command.load();bool hold=photoRequested.load()||recordRequested.load()||recording.load()||pending==1||pending==3||pending==5||int32_t(talkUntil.load()-millis())>0||int32_t(listenAudioUntil.load()-millis())>0;
 audio.holdForMedia(hold);mediaAudioReady=hold;
 unsigned cue=cueRequest.load();if(cue!=cueComplete.load()){cueSuccess=audio.captureCue(settings.effects.captureFile,settings.effects.captureVolumePercent);cueComplete=cue;}
 if(hold&&int32_t(talkUntil.load()-millis())>0&&talkQueue){TalkBlock block;for(unsigned i=0;i<2&&xQueueReceive(talkQueue,&block,0)==pdTRUE;i++){if(audio.writeMediaPcm(reinterpret_cast<int16_t*>(block.bytes),block.size/2))++talkPcmFrames;else{++talkPcmErrors;talkUntil=0;xSemaphoreTake(mutex,portMAX_DELAY);error="Speaker PCM write failed; check enabled audio output";xSemaphoreGive(mutex);}}}
}
#endif

