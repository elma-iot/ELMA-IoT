#include "storage_integrity.h"
#ifndef APP_DISABLE_WEB_UI
#include "storage_backend.h"
#include "psram_allocator.h"
#include <ArduinoJson.h>
#include <esp_system.h>
#include <atomic>
#include <memory>
#include <algorithm>
#include <esp_heap_caps.h>
namespace {
struct Job {
    String id,path,error;StorageTarget target;size_t size=0;
    uint32_t crc=0;std::atomic<size_t> processed{0},total{0};std::atomic<bool> done{false},cancelled{false};
};
std::shared_ptr<Job> active;
SemaphoreHandle_t lock;
void reply(AsyncWebServerRequest* r,const std::shared_ptr<Job>& job) {
    JsonDocument d;d["id"]=job->id;const bool done=job->done.load();d["complete"]=done;d["processedBytes"]=job->processed.load();d["totalBytes"]=job->total.load();
    if(done){d["sizeBytes"]=job->size;char hex[9];snprintf(hex,sizeof(hex),"%08lx",static_cast<unsigned long>(job->crc));d["crc32"]=hex;if(job->error.length())d["error"]=job->error;}
    String body;serializeJson(d,body);auto* response=r->beginResponse(done?200:202,"application/json",body);response->addHeader("Cache-Control","no-store");r->send(response);
}
void calculate(void* context) {
    std::unique_ptr<std::shared_ptr<Job>> holder(static_cast<std::shared_ptr<Job>*>(context));auto job=*holder;
    auto bytes=std::unique_ptr<uint8_t,decltype(&heap_caps_free)>(static_cast<uint8_t*>(heap_caps_malloc(1024,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),heap_caps_free);
    // SD SPI reads use bounded internal memory, matching the download worker.
    File file=storageOpen(job->target,job->path,"r");
    if(!bytes || !file || file.isDirectory())job->error="File or checksum buffer unavailable";
    else {
        job->size=file.size();job->total=job->size;size_t read=0;uint32_t crc=0xffffffff;
        const uint32_t started=millis();
        while(read<job->size){if(job->cancelled || uint32_t(millis()-started)>30*60*1000){job->error="File verification cancelled or timed out";break;}size_t n=file.read(bytes.get(),std::min(size_t(1024),job->size-read));if(!n){job->error="File read interrupted";break;}crc=StorageIntegrity::update(crc,bytes.get(),n);read+=n;job->processed=read;vTaskDelay(1);}
        if(file.size()!=job->size)job->error="File changed while checking";
        job->crc=crc^0xffffffff;
    }
    if(file)file.close();endStorageRead(job->target);job->done=true;
}
}
void registerStorageIntegrityRoutes(AsyncWebServer& server,std::function<bool(AsyncWebServerRequest*)> auth) {
    lock=xSemaphoreCreateMutex();
    server.on("/api/storage/checksum",HTTP_DELETE,[auth](AsyncWebServerRequest* r){
        if(!auth(r))return;
        if(!lock){r->send(503);return;}xSemaphoreTake(lock,portMAX_DELAY);
        if(active&&r->hasParam("id")&&active->id==r->getParam("id")->value())active->cancelled=true;
        xSemaphoreGive(lock);r->send(202,"application/json","{\"cancelled\":true}");
    });
    server.on("/api/storage/checksum",HTTP_GET,[auth](AsyncWebServerRequest* r){
        if(!auth(r))return;
        if(!lock){r->send(503,"application/json","{\"error\":\"Checksum service unavailable\"}");return;}
        xSemaphoreTake(lock,portMAX_DELAY);
        struct Unlock{~Unlock(){xSemaphoreGive(lock);}} unlock;
        if(r->hasParam("id")){if(!active || active->id!=r->getParam("id")->value()){r->send(404,"application/json","{\"error\":\"Checksum job expired; retry\"}");return;}reply(r,active);return;}
        if(active&&!active->done.load()){r->send(429,"application/json","{\"error\":\"Another checksum is running; retry\"}");return;}
        if(!r->hasParam("path")){r->send(400);return;}
        auto job=std::make_shared<Job>();job->path=r->getParam("path")->value();job->target=parseStorageTarget(r->hasParam("target")?r->getParam("target")->value():"sd");
        if(!job->path.startsWith("/")||job->path.indexOf("..")>=0||job->path.indexOf('\\')>=0||job->path.indexOf(".elma-settings-")>=0 || job->path.length()>255){r->send(400);return;}
        if(!storageMounted(job->target)||storageWriteBlocked(job->target)){r->send(423,"application/json","{\"error\":\"Storage unavailable or busy; retry\"}");return;}
        if(!storageExists(job->target,job->path)){r->send(404);return;}
        char id[9];snprintf(id,sizeof(id),"%08lx",static_cast<unsigned long>(esp_random()));job->id=id;
        auto* context=new(std::nothrow) std::shared_ptr<Job>(job);if(!context){r->send(503);return;}
        beginStorageRead(job->target);
        if(!requestStorageBackgroundJob(calculate,context)){endStorageRead(job->target);delete context;r->send(503,"application/json","{\"error\":\"Storage worker busy; retry\"}");return;}
        active=job;reply(r,job);
    });
}
#endif
