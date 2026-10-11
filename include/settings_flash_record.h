#pragma once

#include <Arduino.h>
#include <FS.h>

// Two independent records: an interrupted write cannot damage the active slot.
// CRC covers the exact JSON bytes, including credentials; no payload is logged.
namespace SettingsFlash {
constexpr char paths[2][24] = {"/.elma-settings-0", "/.elma-settings-1"};
constexpr uint32_t magic = 0x31434645; // EFC1
constexpr size_t maxBytes = 256 * 1024;
struct Header { uint32_t tag = 0, sequence = 0, length = 0, crc = 0; };
inline uint32_t crcUpdate(uint32_t crc, const uint8_t* bytes, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0);
    }
    return crc;
}
class CheckedWriter {
    File* file_;
    uint32_t crc_ = 0xffffffffU;
public:
    CheckedWriter() : file_(nullptr) {}
    explicit CheckedWriter(File& file) : file_(&file) {}
    size_t write(uint8_t byte) { return write(&byte, 1); }
    size_t write(const uint8_t* bytes, size_t size) {
        const size_t written = file_ ? file_->write(bytes, size) : size;
        crc_ = crcUpdate(crc_, bytes, written);
        return written;
    }
    uint32_t crc() const { return crc_ ^ 0xffffffffU; }
};
inline bool checksum(File& file, uint32_t length, uint32_t& result) {
    uint8_t buffer[512]; uint32_t remaining = length, crc = 0xffffffffU;
    while (remaining) {
        const size_t want = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        const size_t got = file.read(buffer, want);
        if (got != want) return false;
        crc = crcUpdate(crc, buffer, got); remaining -= got;
        delay(0);
    }
    result = crc ^ 0xffffffffU; return true;
}
inline bool inspect(fs::FS& fs, const char* path, Header& header) {
    File file = fs.open(path, "r");
    if (!file || file.isDirectory() || file.read(reinterpret_cast<uint8_t*>(&header), sizeof(header)) != sizeof(header)) return false;
    if (header.tag != magic || !header.sequence || !header.length || header.length > maxBytes || file.size() != sizeof(header) + header.length) return false;
    uint32_t crc = 0;
    return checksum(file, header.length, crc) && crc == header.crc;
}
}
