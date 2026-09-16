#include "device_log.h"
#include "storage_backend.h"
#include "version.h"
#include <Preferences.h>
#include <esp_log.h>
#include <esp_system.h>
#include <memory>

DeviceLogger DebugLog;
namespace {
constexpr const char* LogPath = "/rebootlog.txt";
vprintf_like_t originalLogPrinter = nullptr;
int captureSdkLog(const char* format, va_list args) {
    char text[512];
    va_list copy;
    va_copy(copy, args);
    const int count = vsnprintf(text, sizeof(text), format, copy);
    va_end(copy);
    if (count > 0) {
        DebugLog.capture(text, min(static_cast<size_t>(count), sizeof(text) - 1));
        if (static_cast<size_t>(count) >= sizeof(text)) {
            constexpr char marker[] = " [log line truncated]\n";
            DebugLog.capture(marker, sizeof(marker) - 1);
        }
    }
    return originalLogPrinter ? originalLogPrinter(format, args) : count;
}
}

void DeviceLogger::begin() {
    if (started_) return;
    ioMutex_ = xSemaphoreCreateMutex();
    if (!ioMutex_) return;
    bootId_ = esp_random();
    snprintf(bootHeader_, sizeof(bootHeader_), "\n=== Boot %08lx | firmware %s | reset reason %d ===\n",
             static_cast<unsigned long>(bootId_), APP_VERSION, static_cast<int>(esp_reset_reason()));
    Preferences prefs;
    if (prefs.begin("rebootlog", false)) {
        const uint8_t oldSlot = prefs.getUChar("active", 0) & 1;
        const char* oldKey = oldSlot ? "boot1" : "boot0";
        const size_t length = prefs.getBytesLength(oldKey);
        if (length > 0 && length <= InternalBootBytes) prefs.getBytes(oldKey, previous_, length);
        slot_ = oldSlot ^ 1;
        const char* key = slot_ ? "boot1" : "boot0";
        // Only two bounded blobs; a boot never copies/accumulates older history.
        nvsReady_ = prefs.putBytes(key, bootHeader_, strlen(bootHeader_)) == strlen(bootHeader_)
                    && prefs.putUChar("active", slot_) == 1;
        prefs.end();
    }
    started_ = true;
    capture(bootHeader_, strlen(bootHeader_));
    originalLogPrinter = esp_log_set_vprintf(captureSdkLog);
}

size_t DeviceLogger::write(const uint8_t* data, size_t size) {
    capture(reinterpret_cast<const char*>(data), size);
    return Serial.write(data, size);
}

void DeviceLogger::capture(const char* data, size_t size) {
    if (!started_ || !data || !size) return;
    // Formatting and filesystem/NVS operations never run under this lock.
    // Bound each critical section even when a caller prints a large document.
    while (size) {
        const size_t count = min(size, static_cast<size_t>(256));
        portENTER_CRITICAL(&ringMux_);
        for (size_t i = 0; i < count; ++i) ring_[sequence_++ % RingBytes] = data[i];
        portEXIT_CRITICAL(&ringMux_);
        data += count;
        size -= count;
    }
}

String DeviceLogger::tail(size_t limit, uint64_t* end, uint64_t after) {
    const size_t capacity = min(limit, RingBytes);
    std::unique_ptr<char[]> buffer(new (std::nothrow) char[capacity + 1]);
    if (!buffer) return "";
    portENTER_CRITICAL(&ringMux_);
    const uint64_t sequence = sequence_;
    const uint64_t available = sequence >= after ? sequence - after : 0;
    const size_t count = min(static_cast<uint64_t>(capacity), available);
    for (size_t i = 0; i < count; ++i) buffer[i] = ring_[(sequence - count + i) % RingBytes];
    portEXIT_CRITICAL(&ringMux_);
    buffer[count] = 0;
    if (end) *end = sequence;
    return String(buffer.get(), count);
}

String DeviceLogger::currentBoot(uint64_t* end) {
    String text = tail(InternalBootBytes - strlen(bootHeader_), end);
    if (!text.startsWith(bootHeader_)) text = String(bootHeader_) + text;
    return text;
}

void DeviceLogger::service(bool force) {
    if (!started_) return;
    const unsigned long now = millis();
    if (!force && now - lastServiceAt_ < 3000UL) return;
    if (xSemaphoreTake(ioMutex_, 0) != pdTRUE) return;
    lastServiceAt_ = now;
    const bool mounted = storageMounted(StorageTarget::Sd);
    if (!mounted) externalActive_ = false;
    if (mounted && !storageBusy(StorageTarget::Sd) &&
        (force || !nextSdRetryAt_ || static_cast<long>(now - nextSdRetryAt_) >= 0)) {
        uint64_t end = externalSequence_;
        String pending = tail(RingBytes, &end, externalSequence_);
        if (pending.length()) {
            if (end - externalSequence_ > RingBytes) pending = String("\n[log] Older queued output discarded while storage was unavailable.\n") + pending;
            if (!externalActive_ && !pending.startsWith(bootHeader_)) pending = String(bootHeader_) + pending;
            beginStorageWrite(StorageTarget::Sd);
            File file = storageOpen(StorageTarget::Sd, LogPath, "a");
            if (file && file.size() + pending.length() > ExternalBytes) {
                file.close();
                file = storageOpen(StorageTarget::Sd, LogPath, "w");
            }
            const bool ok = file && file.write(reinterpret_cast<const uint8_t*>(pending.c_str()), pending.length()) == pending.length();
            if (file) { file.flush(); file.close(); }
            endStorageWrite(StorageTarget::Sd);
            externalActive_ = ok;
            if (ok) {
                externalSequence_ = end;
                nextSdRetryAt_ = 0;
                storageError_ = "";
            } else {
                nextSdRetryAt_ = now + 30000UL;
                storageError_ = "SD log write failed; using bounded internal logging and retrying.";
            }
        }
    }
    // One checkpoint per minute, plus explicit startup/restart checkpoints.
    // Serial producers only enqueue: they never block on a flash write.
    if ((!externalActive_ || force) && (force || now - lastCheckpointAt_ >= 60000UL)) {
        uint64_t end = 0;
        String text = currentBoot(&end);
        if (end != savedSequence_ && text.length()) {
            Preferences prefs;
            if (prefs.begin("rebootlog", false)) {
                const char* key = slot_ ? "boot1" : "boot0";
                auto trimCheckpoint = [&]() {
                    if (text.length() > checkpointLimit_) {
                        text = String(bootHeader_) + text.substring(text.length() - (checkpointLimit_ - strlen(bootHeader_)));
                    }
                };
                trimCheckpoint();
                nvsReady_ = prefs.putBytes(key, text.c_str(), text.length()) == text.length();
                // Configuration shares this small NVS partition. Keep the
                // other boot intact and adapt this boot's tail to available
                // space instead of silently losing all persistent logging.
                if (!nvsReady_) {
                    prefs.remove(key);
                    while (!nvsReady_ && checkpointLimit_ > 256) {
                        checkpointLimit_ /= 2;
                        trimCheckpoint();
                        nvsReady_ = prefs.putBytes(key, text.c_str(), text.length()) == text.length();
                    }
                }
                nvsReady_ = nvsReady_ && (prefs.getUChar("active", 2) == slot_ || prefs.putUChar("active", slot_) == 1);
                prefs.end();
                if (nvsReady_) savedSequence_ = end;
            } else nvsReady_ = false;
        }
        lastCheckpointAt_ = now;
    }
    xSemaphoreGive(ioMutex_);
}

bool DeviceLogger::snapshot(JsonObject out, const String& since) {
    if (!started_ || xSemaphoreTake(ioMutex_, 0) != pdTRUE) return false;
    uint64_t end = 0;
    String pending = tail(RingBytes, &end, externalSequence_);
    const bool external = externalActive_ && storageMounted(StorageTarget::Sd);
    const String revision = String(bootId_) + ":" + String(static_cast<unsigned long>(end)) + ":" + String(external);
    out["revision"] = revision;
    out["source"] = external ? "sd" : (nvsReady_ ? "internal" : "ram");
    out["limitBytes"] = external ? ExternalBytes : 2 * InternalBootBytes;
    if (!external) out["checkpointBytes"] = checkpointLimit_;
    out["notice"] = external
        ? "Latest 16 KiB shown. Full log: /rebootlog.txt on SD; overwritten at 10 MiB."
        : "Current and previous boot, up to 2 KiB each. Checkpointed every 60 seconds; sudden power loss can lose the latest uncheckpointed output.";
    if (storageError_.length()) out["storageError"] = storageError_;
    if (!nvsReady_ && !external) out["storageError"] = "Internal log storage is unavailable or full; current output is held in RAM.";
    if (since == revision) {
        out["unchanged"] = true;
        xSemaphoreGive(ioMutex_);
        return true;
    }
    String text;
    if (external) {
        if (storageBusy(StorageTarget::Sd)) { xSemaphoreGive(ioMutex_); return false; }
        beginStorageRead(StorageTarget::Sd);
        File file = storageOpen(StorageTarget::Sd, LogPath, "r");
        if (file) {
            const size_t size = file.size();
            const size_t count = min(size, ViewBytes - pending.length());
            file.seek(size - count);
            text.reserve(count + pending.length());
            char chunk[256];
            size_t remaining = count;
            while (remaining) {
                const int got = file.read(reinterpret_cast<uint8_t*>(chunk), min(remaining, sizeof(chunk)));
                if (got <= 0) break;
                text.concat(chunk, got);
                remaining -= got;
            }
            out["fileBytes"] = size;
            file.close();
        }
        endStorageRead(StorageTarget::Sd);
        text += pending;
    } else {
        text = String(previous_) + currentBoot();
    }
    out["text"] = text;
    xSemaphoreGive(ioMutex_);
    return true;
}
