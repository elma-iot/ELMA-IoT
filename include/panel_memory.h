#pragma once
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <cstring>

// LCD data is latency-sensitive and much smaller than storage listings. Keep a
// bounded aggregate budget without applying the storage service's 32 KB floor.
class PanelJsonAllocator final : public ArduinoJson::Allocator {
    size_t used_=0;
public:
    void* allocate(size_t size) override {
        if(size>16384 || used_+size>24576)return nullptr;
        void* p=heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        // Reserve space for concurrent TCP receive/send buffers on no-PSRAM LCDs.
#if APP_SUNTON_PANEL
        const size_t reserve=20480;
#else
        const size_t reserve=8192;
#endif
        if(!p && heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)>size+reserve)
            p=heap_caps_malloc(size,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
        if(p)used_+=heap_caps_get_allocated_size(p);
        return p;
    }
    void deallocate(void* p) override {if(p){used_-=heap_caps_get_allocated_size(p);heap_caps_free(p);}}
    void* reallocate(void* p,size_t size) override {
        if(!p)return allocate(size);
        if(!size){deallocate(p);return nullptr;}
        // ArduinoJson assumes shrinking a string cannot fail. Retain the
        // existing block instead of applying the new-allocation reserve.
        if(size<=heap_caps_get_allocated_size(p))return p;
        void* next=allocate(size);if(!next)return nullptr;
        memcpy(next,p,min(size,heap_caps_get_allocated_size(p)));deallocate(p);return next;
    }
};
inline PanelJsonAllocator* panelJsonAllocator(){static PanelJsonAllocator allocator;return &allocator;}
