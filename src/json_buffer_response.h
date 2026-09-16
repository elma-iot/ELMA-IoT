#pragma once

#include "bounded_buffer_response.h"
#include <cstdlib>
#include <cstring>

// Own one checked allocation for the whole JSON response. AsyncResponseStream
// repeatedly reallocates a cbuf as JSON is written, and aborts on allocation
// failure on a fragmented ESP32 heap (especially during OTA and playback).
class JsonBufferResponse final : public BoundedBufferResponse {
public:
    JsonBufferResponse(char* data, size_t length, int status)
        : BoundedBufferResponse(data, length, "application/json", status), data_(data) {}
    ~JsonBufferResponse() override { free(data_); }
private:
    char* data_;
};
