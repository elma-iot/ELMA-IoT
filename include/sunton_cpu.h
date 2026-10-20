#pragma once
#include <Arduino.h>
#if APP_SUNTON_PANEL == 3
#include <esp32-hal-cpu.h>
#include <atomic>
// Classic ESP32: 80 and 160 MHz share the 320 MHz PLL. Never retune the
// PLL to/from 240 MHz while Wi-Fi is running (IDF 4.4 pm_impl.c).
inline std::atomic<uint32_t>& suntonWebActivity(){static std::atomic<uint32_t> at{0};return at;}
inline SemaphoreHandle_t suntonClockMutex(){
    static StaticSemaphore_t storage;
    static SemaphoreHandle_t lock=xSemaphoreCreateMutexStatic(&storage);
    return lock;
}
inline bool suntonSetCpu(uint32_t mhz){
    if(mhz!=80 && mhz!=160)return false;
    const auto lock=suntonClockMutex();
    if(xSemaphoreTake(lock,pdMS_TO_TICKS(100))!=pdTRUE)return false;
    // Recheck activity under the lock: a web boost must win over an idle decision.
    const uint32_t at=suntonWebActivity().load();
    if(mhz==80 && at && uint32_t(millis()-at)<10000)mhz=160;
    const bool ok=ESP.getCpuFreqMHz()==mhz || setCpuFrequencyMhz(mhz);
    xSemaphoreGive(lock);return ok;
}
inline void suntonWebBoost(){
    suntonWebActivity().store(millis());
    suntonSetCpu(160);
}
inline bool suntonWebBusy(){const uint32_t at=suntonWebActivity().load();return at && uint32_t(millis()-at)<10000;}
#endif
