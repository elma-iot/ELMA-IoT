#include "logic_sleep_service.h"
#include "logic_sleep_contract.h"
#include "plot_storage.h"
#include "storage_backend.h"
#include <Arduino.h>
#include <esp_sleep.h>
#include <esp_attr.h>
#include <esp_wifi.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <freertos/semphr.h>

namespace {
struct Request {bool active=false,deep=false,timer=false;int pin=-1,level=0;uint64_t us=0;uint32_t at=0;char id[65]={};};
Request pending;
RTC_DATA_ATTR int retainedWakePin=-1;
SemaphoreHandle_t mutex(){static auto value=xSemaphoreCreateMutex();return value;}
struct Guard {bool held=false;Guard(){held=mutex()&&xSemaphoreTake(mutex(),pdMS_TO_TICKS(10))==pdTRUE;}~Guard(){if(held)xSemaphoreGive(mutex());}};
uint32_t sequence=0,wakeCount=0;int cause=0;bool initialized=false;std::string completed,errorText;
void initialize(){if(!initialized){
 cause=int(esp_sleep_get_wakeup_cause());wakeCount=cause!=ESP_SLEEP_WAKEUP_UNDEFINED;initialized=true;
#if SOC_PM_SUPPORT_EXT_WAKEUP
 // EXT0 leaves its pad in RTC mode. Release it before normal board setup.
 if(wakeCount&&retainedWakePin>=0&&esp_sleep_is_valid_wakeup_gpio(gpio_num_t(retainedWakePin)))rtc_gpio_deinit(gpio_num_t(retainedWakePin));
#endif
 retainedWakePin=-1;
}}
void finish(const Request& request,const std::string& error,bool woke){
 setPlotSleepGate(false);Guard guard;if(!guard.held)return;
 pending.active=false;completed=request.id;errorText=error;++sequence;
 if(woke){cause=int(esp_sleep_get_wakeup_cause());++wakeCount;}
}
void cleanup(int pin,bool deep){
 esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
 if(pin>=0){gpio_wakeup_disable(gpio_num_t(pin));
#if SOC_PM_SUPPORT_EXT_WAKEUP
  if(deep)rtc_gpio_deinit(gpio_num_t(pin));
#endif
  gpio_set_direction(gpio_num_t(pin),GPIO_MODE_INPUT);
 }
}
}
void beginLogicSleep(){Guard guard;if(guard.held)initialize();}
bool requestLogicSleep(JsonObjectConst node,JsonVariantConst args,std::string& error){
 auto p=node["parameters"];const std::string mode=p["mode"]|"";
 if(mode!="light"&&mode!="deep"){error="Unsupported sleep mode";return false;}
 Request next;next.deep=mode=="deep";next.timer=p["timerEnabled"]==true;
 if(next.timer){double seconds=args["seconds"]|0.0;if(!ElmaLogic::validSleepSeconds(seconds)){error="Wake delay must be 0.001–604800 seconds";return false;}next.us=uint64_t(seconds*1000000.0);}
 auto wake=args["wake"];
 if(!wake.isNull()){
  next.pin=wake["parameters"]["pin"]|-1;const char* level=wake["parameters"]["level"]|"";next.level=std::string(level)=="high";
  auto caps=wake["binding"]["allowedPins"][std::to_string(next.pin)];
  if(wake["type"]!="hardware.wake_gpio"||caps[mode]!=true||next.pin<0||next.pin>=GPIO_NUM_MAX||!GPIO_IS_VALID_GPIO(next.pin)||(std::string(level)!="high"&&std::string(level)!="low")||(next.deep&&!esp_sleep_is_valid_wakeup_gpio(gpio_num_t(next.pin)))){error="Wake GPIO is not authorized or supported by this chip";return false;}
 }
 if(!next.timer&&next.pin<0){error="Sleep requires a wake timer or Wake GPIO";return false;}
 Guard guard;if(!guard.held||pending.active){error="Another sleep request is pending";return false;}initialize();
 next.active=true;next.at=millis();snprintf(next.id,sizeof(next.id),"%s",node["id"]|"");pending=next;return true;
}
bool logicSleepPending(){Guard guard;return guard.held&&pending.active;}
void logicSleepSnapshot(JsonObject result){Guard guard;if(!guard.held)return;initialize();result["wakeCount"]=wakeCount;result["cause"]=cause;result["sequence"]=sequence;result["node"]=completed;result["error"]=errorText;result["pending"]=pending.active;}
void processLogicSleep(bool busy,const std::function<void()>& prepare){
 Request request;{Guard guard;if(!guard.held||!pending.active)return;request=pending;}
 if(uint32_t(millis()-request.at)<250)return; // Let the requesting UI/serial response complete.
 if(busy){finish(request,"Stop audio and finish firmware transfer before sleeping",false);return;}
 setPlotSleepGate(true);
 if(!plotSleepReady()||storageBusy(StorageTarget::Sd)||storageBusy(StorageTarget::Flash)){
  if(uint32_t(millis()-request.at)>3000)finish(request,"Storage busy; sleep rejected without discarding queued recordings",false);
  return;
 }
 cleanup(-1,false);esp_err_t result=ESP_OK;
 if(request.timer)result=esp_sleep_enable_timer_wakeup(request.us);
 if(result==ESP_OK&&request.pin>=0){
  gpio_num_t pin=gpio_num_t(request.pin);gpio_set_direction(pin,GPIO_MODE_INPUT);
  // External pull resistor is required; input-only ESP32 pins have no internal pulls.
  gpio_set_pull_mode(pin,GPIO_FLOATING);
  if(gpio_get_level(pin)==request.level){finish(request,"Wake GPIO is already active; release it before sleeping",false);cleanup(request.pin,request.deep);return;}
  if(!request.deep){result=gpio_wakeup_enable(pin,request.level?GPIO_INTR_HIGH_LEVEL:GPIO_INTR_LOW_LEVEL);if(result==ESP_OK)result=esp_sleep_enable_gpio_wakeup();}
  else {
#if SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP
   result=esp_deep_sleep_enable_gpio_wakeup(1ULL<<request.pin,request.level?ESP_GPIO_WAKEUP_GPIO_HIGH:ESP_GPIO_WAKEUP_GPIO_LOW);
#elif SOC_PM_SUPPORT_EXT_WAKEUP
   result=esp_sleep_enable_ext0_wakeup(pin,request.level);
#else
   result=ESP_ERR_NOT_SUPPORTED;
#endif
  }
 }
 if(result!=ESP_OK){cleanup(request.pin,request.deep);finish(request,std::string("Wake setup failed: ")+esp_err_to_name(result),false);return;}
 wifi_mode_t mode=WIFI_MODE_NULL;esp_wifi_get_mode(&mode);
 const bool radioStopped=esp_wifi_stop()==ESP_OK;
 auto restore=[&](){if(radioStopped){esp_wifi_start();if(mode==WIFI_MODE_STA||mode==WIFI_MODE_APSTA)esp_wifi_connect();}};
 if(storageBusy(StorageTarget::Sd)||storageBusy(StorageTarget::Flash)){restore();cleanup(request.pin,request.deep);finish(request,"Storage transfer still active",false);return;}
 if(prepare)prepare();
 if(request.deep){retainedWakePin=request.pin;esp_deep_sleep_start();}
 result=esp_light_sleep_start();
 restore();cleanup(request.pin,false);
 finish(request,result==ESP_OK?"":std::string("Light sleep failed: ")+esp_err_to_name(result),result==ESP_OK);
}
