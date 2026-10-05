#include "alarm_clock_service.h"
#include "settings_manager.h"
#include <RTClib.h>
#include <Wire.h>
#include "shared_i2c.h"
#include <soc/soc_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <sys/time.h>

namespace {
SemaphoreHandle_t mutex=nullptr;
TwoWire& rtcWire=SharedI2c::wire();
RTC_DS3231 rtc;
bool configured=false,wireReady=false,begun=false;
int sda=-1,scl=-1;
uint32_t lastPoll=0,lastManual=0,rtcAt=0;
int64_t rtcEpoch=0,manualEpoch=0;
uint64_t manualElapsed=0;
std::string problem;
int helperPin(JsonVariantConst value){if(value.is<int>())return value.as<int>();const char* text=value|"";char* end=nullptr;long pin=strtol(text,&end,10);return *text&&end&&!*end?int(pin):-1;}
bool probe(){rtcWire.beginTransmission(0x68);return rtcWire.endTransmission()==0;}
struct Unlock {~Unlock(){xSemaphoreGive(mutex);}};
}
void configureAlarmClock(const SettingsBundle& settings) {
 if(!mutex)mutex=xSemaphoreCreateMutex();if(!mutex||xSemaphoreTake(mutex,pdMS_TO_TICKS(20))!=pdTRUE)return;Unlock unlock;
 JsonDocument profiles,bindings;deserializeJson(profiles,settings.ui.peripheralProfileSelections);deserializeJson(bindings,settings.ui.peripheralHelperBindings);
 int index=-1,count=0;size_t i=0;for(JsonVariantConst profile:profiles["sensors"].as<JsonArrayConst>()){if(profile=="ds3231-rtc"){index=int(i);++count;}++i;}
 int nextSda=-1,nextScl=-1;if(index>=0){const std::string slot="sensor:"+std::to_string(index);nextSda=helperPin(bindings[slot]["SDA"]);nextScl=helperPin(bindings[slot]["SCL"]);}
 bool blocked=count>1;
 const bool oled=settings.oled.enabled&&settings.oled.displayType!="wape"&&settings.oled.displayType!="panel";
 blocked=blocked||(oled&&(nextSda!=settings.oled.sdaPin||nextScl!=settings.oled.sclPin||settings.oled.i2cAddress==0x68));
 size_t sensorIndex=0;for(JsonVariantConst profile:profiles["sensors"].as<JsonArrayConst>()){
  if(profile=="bno055") {auto pins=bindings["sensor:"+std::to_string(sensorIndex)];blocked=blocked||helperPin(pins["SDA"])!=nextSda||helperPin(pins["SCL"])!=nextScl;}
  ++sensorIndex;
 }

 if(configured==(index>=0)&&sda==nextSda&&scl==nextScl&&wireReady&&!blocked)return;
 wireReady=false;begun=false;rtcEpoch=0;configured=index>=0;sda=nextSda;scl=nextScl;problem.clear();
 if(!configured)return;
 if(blocked){problem="RTC requires one module, distinct addresses and the same SDA/SCL pair as other external I2C devices";return;}
 if(sda<0||scl<0||sda==scl||sda>48||scl>48||!isSafeOutputPinForBoard(uint8_t(sda))||!isSafeOutputPinForBoard(uint8_t(scl))){problem="Configure two valid RTC GPIOs";return;}
 SharedI2c::Guard bus;if(!bus){problem="I2C bus busy";return;}
 wireReady=SharedI2c::begin(sda,scl);rtcWire.setTimeOut(20);lastPoll=millis()-1000;
 if(!wireReady)problem="RTC I2C initialization failed";
}
void pollAlarmClock(uint32_t now) {
 if(!mutex||xSemaphoreTake(mutex,0)!=pdTRUE)return;Unlock unlock;
 now=millis(); // Set clock may have run since the main loop captured its timestamp.
 if(manualEpoch)manualElapsed+=uint32_t(now-lastManual);lastManual=now;
 if(!wireReady||uint32_t(now-lastPoll)<1000)return;lastPoll=now;rtcEpoch=0;
 SharedI2c::Guard bus(0);if(!bus)return;
 if(!probe()){problem="DS3231 not responding";return;}
 if(!begun)begun=rtc.begin(&rtcWire);
 if(!begun||rtc.lostPower()){problem="RTC time invalid; use Set clock";return;}
 DateTime value=rtc.now();if(!value.isValid()||value.year()<2000||value.year()>2099){problem="RTC returned invalid date/time";return;}
 rtcEpoch=value.unixtime();rtcAt=now;problem.clear();
}
void alarmClockSnapshot(JsonObject target) {
 timeval now;gettimeofday(&now,nullptr);if(now.tv_sec>=946684800LL&&now.tv_sec<4102444800LL)target["utc"]=int64_t(now.tv_sec);
 if(!mutex||xSemaphoreTake(mutex,0)!=pdTRUE)return;Unlock unlock;
 if(manualEpoch)target["manual"]=manualEpoch+manualElapsed/1000;
 if(rtcEpoch)target["rtc"]=rtcEpoch+uint32_t(millis()-rtcAt)/1000;
 target["rtcConfigured"]=configured;target["error"]=problem;
}
bool setAlarmClock(const char* source,int64_t epoch,std::string& error) {
 if(epoch<946684800LL||epoch>=4102444800LL){error="Clock date must be in 2000-2099";return false;}
 if(!mutex||xSemaphoreTake(mutex,pdMS_TO_TICKS(20))!=pdTRUE){error="Clock busy";return false;}Unlock unlock;
 if(std::string(source)=="manual"){manualEpoch=epoch;manualElapsed=0;lastManual=millis();return true;}
 if(std::string(source)!="rtc"){error="UTC clock is managed by NTP; select Manual or DS3231 RTC";return false;}
 SharedI2c::Guard bus;if(!bus){error="I2C bus busy";return false;}
 if(!wireReady||!probe()){error="Configure and connect a DS3231 RTC first";return false;}
 if(!begun)begun=rtc.begin(&rtcWire);
 if(!begun){error="DS3231 initialization failed";return false;}
 rtc.adjust(DateTime(uint32_t(epoch)));DateTime check=rtc.now();
 if(!check.isValid()||rtc.lostPower()||check.unixtime()<epoch||check.unixtime()>epoch+2){error="RTC clock write could not be verified";return false;}
 rtcEpoch=check.unixtime();rtcAt=millis();lastPoll=rtcAt;problem.clear();return true;
}
