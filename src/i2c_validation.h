#pragma once
#include "settings_manager.h"
#include "i2c_profiles.generated.h"
#include <vector>

inline bool validateI2cConfiguration(const SettingsBundle& settings,String& error) {
 JsonDocument catalog,profiles,bindings;
 deserializeJson(catalog,I2C_PROFILE_JSON);
 deserializeJson(profiles,settings.ui.peripheralProfileSelections);
 deserializeJson(bindings,settings.ui.peripheralHelperBindings);
 struct Device {String name;int sda,scl;std::vector<int> addresses;bool driver;};
 std::vector<Device> devices;
 const char* groups[]={"audioIn","display","sensor","expansion","communication"};
 const char* keys[]={"audioInProfiles","displayProfiles","sensors","expansions","communication"};
 auto number=[](JsonVariantConst value){if(value.isNull()||value=="")return -1; if(value.is<int>())return value.as<int>();const char* text=value|"";char* end=nullptr;long v=strtol(text,&end,0);return *text&&end&&!*end?int(v):-2;};
 for(int g=0;g<5;g++){int index=0;for(JsonVariantConst profile:profiles[keys[g]].as<JsonArrayConst>()) {
  const int slotIndex=index++;String name=String(groups[g])+":"+profile.as<String>();
  if(catalog[name].isNull())continue;
  auto pins=bindings[String(groups[g])+":"+String(slotIndex)];
  Device device{name,number(pins["SDA"]),number(pins["SCL"]),{},name=="sensor:ds3231-rtc"||name=="sensor:bno055"||name=="display:i2c-oled"};
  int address=number(pins["I2C_ADDRESS"]);
  if(name=="display:i2c-oled"&&slotIndex==0){device.sda=settings.oled.sdaPin;device.scl=settings.oled.sclPin;address=settings.oled.i2cAddress;}
  auto possible=catalog[name].as<JsonArrayConst>();
  if(address!=-1){
   bool allowed=possible.size()==0;for(int candidate:possible)allowed|=candidate==address;
   if(address<8||address>119||!allowed||(name=="expansion:pca9685"&&address==112)){error=name+": invalid I2C address";return false;}
   device.addresses.push_back(address);if(name=="expansion:pca9685")device.addresses.push_back(0x70);
  }else for(int candidate:possible)device.addresses.push_back(candidate);
  if(device.sda<0||device.scl<0||device.sda==device.scl){error=name+": assign two different SDA/SCL GPIOs";return false;}
  for(const auto& other:devices){
   if(device.driver&&name==other.name){error=name+": only one runtime instance is supported";return false;}
   const bool shared=device.sda==other.sda||device.sda==other.scl||device.scl==other.sda||device.scl==other.scl;
   const bool pair=device.sda==other.sda&&device.scl==other.scl;
   if((shared||device.driver&&other.driver)&&!pair){error=name+" / "+other.name+": use the same SDA/SCL pair for the shared external I2C bus";return false;}
   if(pair){bool conflict=device.addresses.empty()||other.addresses.empty();for(int a:device.addresses)for(int b:other.addresses)conflict|=a==b&&!(a==112&&name=="expansion:pca9685"&&other.name==name);
    if(conflict){error=name+" / "+other.name+": I2C address collision or unspecified hardware address";return false;}}
  }
  devices.push_back(device);
 }}
 return true;
}
