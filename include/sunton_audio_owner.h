#pragma once
#include <Arduino.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#if APP_SUNTON_PANEL == 3
inline SemaphoreHandle_t suntonAudioMutex(){static auto mutex=xSemaphoreCreateRecursiveMutex();return mutex;}
inline TaskHandle_t suntonAudioTask=nullptr;
class SuntonAudioLock {
    SemaphoreHandle_t mutex_;
public:
    SuntonAudioLock():mutex_(suntonAudioMutex()){if(mutex_)xSemaphoreTakeRecursive(mutex_,portMAX_DELAY);}
    ~SuntonAudioLock(){if(mutex_)xSemaphoreGiveRecursive(mutex_);}
};
#else
class SuntonAudioLock {};
#endif
