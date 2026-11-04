#include "amoled_sensors.h"
#if APP_WAVESHARE_AMOLED
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
// Onboard touch and IMU own controller 0. External OLED/RTC/BNO055 retain
// SharedI2c's controller 1 and may use a separate pair of exposed GPIOs.
namespace OnboardI2c {
TwoWire& wire(){return Wire;}
SemaphoreHandle_t mutex(){static auto lock=xSemaphoreCreateRecursiveMutex();return lock;}
class Guard {
 bool held_;
 public:
 Guard():held_(mutex()&&xSemaphoreTakeRecursive(mutex(),pdMS_TO_TICKS(20))==pdTRUE){}
 ~Guard(){if(held_)xSemaphoreGiveRecursive(mutex());}
 explicit operator bool()const{return held_;}
 Guard(const Guard&)=delete;Guard& operator=(const Guard&)=delete;
};
bool begin(int sda,int scl){
 static bool ready=false;if(ready)return true;
 ready=Wire.begin(sda,scl,100000);if(ready){Wire.setTimeOut(20);Wire.beginTransmission(0x38);Wire.write(0);Wire.write(0);Wire.endTransmission();}return ready;
}
}
#include <Arduino.h>
namespace {
uint32_t attempted=0,sampled=0;
bool started=false,valid=false;
uint8_t address=0;
float accel[3]={},gyro[3]={};
bool read(uint8_t addr,uint8_t reg,uint8_t* data,uint8_t count){
 auto& bus=OnboardI2c::wire();bus.beginTransmission(addr);bus.write(reg);
 if(bus.endTransmission(false)!=0)return false;
 if(bus.requestFrom(addr,count)!=count){while(bus.available())bus.read();return false;}
 for(uint8_t i=0;i<count;++i)data[i]=bus.read();return true;
}
bool write(uint8_t reg,uint8_t value){auto& bus=OnboardI2c::wire();bus.beginTransmission(address);bus.write(reg);bus.write(value);return bus.endTransmission()==0;}
}
#endif
namespace AmoledSensors {
void tick(){
#if APP_WAVESHARE_AMOLED
 const uint32_t now=millis();
 if(started && now-attempted<50)return;
 if(!started && attempted && now-attempted<5000)return;
 attempted=now;
 OnboardI2c::Guard lock;if(!lock||!OnboardI2c::begin(40,39))return;
 if(!started){
  address=0;uint8_t id=0;
  for(uint8_t candidate:{uint8_t(0x6b),uint8_t(0x6a)})if(read(candidate,0,&id,1)&&id==0x05){address=candidate;break;}
  if(!address)return;
  // Vendor register map: incrementing reads, +/-8g, +/-512 dps, 125 Hz.
  started=write(8,0)&&write(2,0x60)&&write(3,0x26)&&write(4,0x56)&&write(6,0x11)&&write(8,3);
  return;
 }
 uint8_t status=0,data[12];
 if(!read(address,0x2e,&status,1)){valid=false;started=false;return;}
 if((status&3)!=3)return;
 if(!read(address,0x35,data,12)){valid=false;return;}
 for(unsigned i=0;i<6;++i){const int16_t raw=int16_t(uint16_t(data[2*i])|uint16_t(data[2*i+1])<<8);if(i<3)accel[i]=raw/4096.0f;else gyro[i-3]=raw/64.0f;}
 sampled=now;valid=true;
#endif
}
bool touch(int16_t& x,int16_t& y){
#if APP_WAVESHARE_AMOLED
 OnboardI2c::Guard lock;if(!lock||!OnboardI2c::begin(40,39))return false;
 uint8_t points=0,data[4];
 if(!read(0x38,2,&points,1)||!(points&15)||!read(0x38,3,data,4))return false;
 const int rawY=((data[0]&15)<<8)|data[1],rawX=((data[2]&15)<<8)|data[3];
 if(rawX>=536||rawY>=240)return false;
 x=rawX;y=239-rawY;return true;
#else
 (void)x;(void)y;return false;
#endif
}
void snapshot(JsonObject target){
#if APP_WAVESHARE_AMOLED
 OnboardI2c::Guard lock;if(!lock){target["valid"]=false;return;}
 target["supported"]=true;target["controller"]="QMI8658";target["available"]=started;
 target["valid"]=valid&&millis()-sampled<1000;target["sampledAtMs"]=sampled;
 target["accelerationUnit"]="g";target["gyroscopeUnit"]="deg/s";
 if(valid&&millis()-sampled<1000){for(unsigned i=0;i<3;++i){target["acceleration"][i]=accel[i];target["gyroscope"][i]=gyro[i];const char* axis=i==0?"x":i==1?"y":"z";target["accelerationG"][axis]=accel[i];target["gyroscopeDps"][axis]=gyro[i];}}
#else
 target["supported"]=false;
#endif
}
}
