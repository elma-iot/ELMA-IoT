#pragma once

#include <ESPAsyncWebServer.h>
#include <algorithm>

// ESP32 flash is memory mapped. Keep the source alive for the response lifetime
// and let AsyncTCP copy at most two packets, without a second window-sized heap
// allocation. Two packets avoid delayed-ACK stalls; ACKs replenish this limit.
class BoundedBufferResponse : public AsyncWebServerResponse {
public:
    BoundedBufferResponse(const char* data, size_t length, const char* type, int status = 200, bool immutableFlash = false)
        : data_(data), immutableFlash_(immutableFlash) {
        _code = status;
        _contentType = type;
        _contentLength = length;
        _sendContentLength = true;
        _chunked = false;
    }
    bool _sourceValid() const override { return data_ != nullptr; }
    void _respond(AsyncWebServerRequest* request) override {
        addHeader("Connection", "close", false);
        _assembleHead(head_, request->version());
        _state = RESPONSE_HEADERS;
        request->client()->setNoDelay(true);
        _ack(request, 0, 0);
    }
    size_t _ack(AsyncWebServerRequest* request, size_t length, uint32_t) override {
        _ackedLength += length;
        if (_state == RESPONSE_END) return 0;
        size_t total = 0;
        while (true) {
            if (_state == RESPONSE_HEADERS && headerOffset_ == head_.length()) {
                head_ = "";
                _state = RESPONSE_CONTENT;
            }
            if (_state == RESPONSE_CONTENT && _sentLength == _contentLength) {
                if (_ackedLength >= _writtenLength) _state = RESPONSE_END;
                return total;
            }
            const bool headers = _state == RESPONSE_HEADERS;
            const char* source = headers ? head_.c_str() + headerOffset_ : data_ + _sentLength;
            const size_t remaining = headers ? head_.length() - headerOffset_ : _contentLength - _sentLength;
            const size_t inFlight = _writtenLength - std::min(_ackedLength, _writtenLength);
            const size_t room = 2920 - std::min(size_t(2920), inFlight);
            const size_t count = std::min(size_t(1460), std::min(remaining, std::min(room, request->client()->space())));
            if (!count || (!headers && count < std::min(size_t(1460), remaining))) return total;
            // Only immortal flash assets may be borrowed by TCP. JSON and
            // headers must be copied so disconnect/timeout cleanup is safe.
            const size_t written = request->client()->write(source, count,
                !headers && immutableFlash_ ? 0 : ASYNC_WRITE_FLAG_COPY);
            _writtenLength += written;
            if (headers) headerOffset_ += written;
            else _sentLength += written;
            total += written;
            if (written < count) return total;
        }
    }

private:
    const char* data_;
    bool immutableFlash_;
    String head_;
    size_t headerOffset_ = 0;
};
