#include "storage_download.h"
#include "storage_memory.h"
#include <ESPAsyncWebServer.h>
#include <freertos/task.h>
#include <algorithm>
#include <new>

namespace {
portMUX_TYPE downloadMux = portMUX_INITIALIZER_UNLOCKED;
unsigned activeDownloads = 0;
}
StorageDownload::StorageDownload(StorageTarget target, File file):target_(target),file_(std::move(file)) {}
std::shared_ptr<StorageDownload> StorageDownload::create(StorageTarget target, File file) {
    portENTER_CRITICAL(&downloadMux);
    const bool accepted = activeDownloads < 2;
    if (accepted) ++activeDownloads;
    portEXIT_CRITICAL(&downloadMux);
    if (!accepted) return {};
    auto* raw = new(std::nothrow) StorageDownload(target,std::move(file));
    if (!raw) {portENTER_CRITICAL(&downloadMux);--activeDownloads;portEXIT_CRITICAL(&downloadMux);return {};}
    std::shared_ptr<StorageDownload> result(raw);
    raw->buffer_ = static_cast<uint8_t*>(allocateStorageBuffer(capacity_));
    raw->mutex_ = xSemaphoreCreateMutex();
    if (!raw->buffer_ || !raw->mutex_) return {};
    beginStorageRead(target);raw->leased_=true;
    return result;
}
StorageDownload::~StorageDownload() {
    file_.close();
    if (leased_) endStorageRead(target_);
    if (buffer_) heap_caps_free(buffer_);
    if (mutex_) vSemaphoreDelete(mutex_);
    portENTER_CRITICAL(&downloadMux);--activeDownloads;portEXIT_CRITICAL(&downloadMux);
}
bool StorageDownload::start() {
    auto* owner = new(std::nothrow) std::shared_ptr<StorageDownload>(shared_from_this());
    if (!owner) return false;
    if (xTaskCreate(worker,"storage-download",3072,owner,1,nullptr)!=pdPASS) {delete owner;return false;}
    return true;
}
void StorageDownload::worker(void* argument) {
    {
        auto* holder=static_cast<std::shared_ptr<StorageDownload>*>(argument);
        auto self=*holder;delete holder;
        uint8_t chunk[1024];
        uint32_t lastProgress=millis();
        size_t remaining=self->file_.size();
        for (;;) {
            if (self.use_count()==1) break; // Aborted/disconnected response.
            size_t free=0;
            if (xSemaphoreTake(self->mutex_,pdMS_TO_TICKS(10))==pdTRUE) {
                free=capacity_-self->count_;xSemaphoreGive(self->mutex_);
            }
            if (!free) {if(uint32_t(millis()-lastProgress)>30000)break;vTaskDelay(1);continue;}
            if(!remaining)break;
            const size_t got=self->file_.read(chunk,std::min(remaining,std::min(free,sizeof(chunk))));
            if (!got) break;
            remaining-=got;lastProgress=millis();
            // Only this task produces; free space cannot shrink while reading.
            xSemaphoreTake(self->mutex_,portMAX_DELAY);
            size_t tail=(self->head_+self->count_)%capacity_;
            size_t first=std::min(got,capacity_-tail);
            memcpy(self->buffer_+tail,chunk,first);memcpy(self->buffer_,chunk+first,got-first);
            self->count_+=got;xSemaphoreGive(self->mutex_);
            vTaskDelay(1);
        }
        self->file_.close();
        if(self->leased_){endStorageRead(self->target_);self->leased_=false;}
        xSemaphoreTake(self->mutex_,portMAX_DELAY);self->done_=true;xSemaphoreGive(self->mutex_);
    }
    vTaskDelete(nullptr);
}
size_t StorageDownload::read(uint8_t* output,size_t maximum) {
    if (!maximum || xSemaphoreTake(mutex_,0)!=pdTRUE) return RESPONSE_TRY_AGAIN;
    size_t size=std::min(maximum,count_);
    size_t first=std::min(size,capacity_-head_);
    memcpy(output,buffer_+head_,first);memcpy(output+first,buffer_,size-first);
    head_=(head_+size)%capacity_;count_-=size;
    bool done=done_;xSemaphoreGive(mutex_);
    return size ? size : done ? 0 : RESPONSE_TRY_AGAIN;
}
