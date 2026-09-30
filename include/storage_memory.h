#pragma once
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <ArduinoJson.h>

// Bulk CPU buffers only. Driver DMA buffers and RTOS stacks stay internal.
inline void* allocateStorageBuffer(size_t bytes, size_t internalLimit = 8192) {
    if (!bytes) return nullptr;
    if (psramFound()) {
        if (void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) return p;
    }
    constexpr size_t reserve = 32768;
    const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const size_t free = heap_caps_get_free_size(caps);
    if (bytes > internalLimit || free < reserve || bytes > free - reserve ||
        heap_caps_get_largest_free_block(caps) < bytes) return nullptr;
    return heap_caps_malloc(bytes, caps);
}

class StorageJsonAllocator : public ArduinoJson::Allocator {
public:
    void* allocate(size_t size) override {return allocateStorageBuffer(size);}
    void deallocate(void* p) override {heap_caps_free(p);}
    void* reallocate(void* p,size_t size) override {
        if(!p)return allocate(size);
        if(!size){deallocate(p);return nullptr;}
        void* next=allocate(size);if(!next)return nullptr;
        memcpy(next,p,min(size,heap_caps_get_allocated_size(p)));deallocate(p);return next;
    }
};
inline StorageJsonAllocator* storageJsonAllocator(){static StorageJsonAllocator allocator;return &allocator;}
