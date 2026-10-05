#pragma once
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// External OLED / RTC / BNO055 share one controller. VIEWE touch owns
// controller 0; Sunton touch and the camera SDK own controller 1. Hold the lock across whole operations,
// including repeated-start reads and driver initialization.
namespace SharedI2c {
inline TwoWire& wire() {
#if APP_HAS_ONBOARD_PANEL && !APP_SUNTON_PANEL
 return Wire1;
#else
 return Wire;
#endif
}
inline SemaphoreHandle_t mutex() { static SemaphoreHandle_t value=xSemaphoreCreateRecursiveMutex(); return value; }
class Guard {
 bool acquired_;
 public:
 explicit Guard(uint32_t wait=20):acquired_(mutex()&&xSemaphoreTakeRecursive(mutex(),pdMS_TO_TICKS(wait))==pdTRUE){}
 ~Guard(){if(acquired_)xSemaphoreGiveRecursive(mutex());}
 explicit operator bool() const{return acquired_;}
 Guard(const Guard&)=delete;Guard& operator=(const Guard&)=delete;
};
inline bool begin(int sda,int scl) {
 static int activeSda=-1,activeScl=-1;
 if(sda==activeSda&&scl==activeScl)return true;
 if(activeSda>=0)wire().end();
 activeSda=activeScl=-1;
 if(!wire().begin(sda,scl,100000))return false;
 wire().setTimeOut(20);activeSda=sda;activeScl=scl;return true;
}
}
