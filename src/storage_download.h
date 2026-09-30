#pragma once
#include "storage_backend.h"
#include <memory>
#include <freertos/semphr.h>

// A response owns a reference; the reader task owns the other. Dropping a
// response cancels its reader without accessing a destroyed HTTP request.
class StorageDownload : public std::enable_shared_from_this<StorageDownload> {
public:
    static std::shared_ptr<StorageDownload> create(StorageTarget target, File file);
    ~StorageDownload();
    bool start();
    size_t read(uint8_t* output, size_t maximum);
private:
    static void worker(void* argument);
    StorageDownload(StorageTarget target, File file);
    StorageTarget target_;
    File file_;
    uint8_t* buffer_ = nullptr;
    SemaphoreHandle_t mutex_ = nullptr;
    size_t head_ = 0, count_ = 0;
    bool done_ = false, leased_ = false;
    static constexpr size_t capacity_ = 4096;
};
