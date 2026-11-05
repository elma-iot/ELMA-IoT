#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <string>
#include <algorithm>
#include "runtime_peripheral_catalog.h"
#include "peripheral_pin_binding.h"
#if __has_include("elma_led_modules.h")
#include "elma_led_modules.h"
#endif

// Flash driver contracts, not the configuration used when the image was built.
// Only selected contracts are allocated in RAM. The same implementation is used
// by ESP32 and compact ESP8266/C2/C6 images from either application builder.
namespace RuntimePeripherals {
struct Group {const char* name;const char* field;};
static constexpr Group groups[]={{"audio","audioProfiles"},{"audioIn","audioInProfiles"},{"display","displayProfiles"},{"sensor","sensors"},{"input","inputs"},{"control","controls"},{"expansion","expansions"},{"storage","storage"},{"communication","communication"},{"power","power"}};
inline bool read(JsonDocument& doc,JsonVariantConst value){
 if(value.is<const char*>())return !deserializeJson(doc,value.as<const char*>());
 return value.isNull() ? (doc.to<JsonObject>(),true) : doc.set(value);
}
inline bool portable(const std::string& group,const std::string& profile){
 if(group=="control")return profile=="ws2812-neopixel-led-strip"||profile=="relay-module"||profile=="mosfet-switch"||profile=="solenoid-valve-driver"||profile=="pump-driver"||profile=="vibration-motor"||profile=="led-pwm-dimmer"||profile=="pwm-fan";
 if(group=="input")return profile=="physical-button"||profile=="toggle-switch"||profile=="ttp223-touch-button"||profile=="pir-motion-sensor"||profile=="reed-switch"||profile=="limit-switch"||profile=="hall-sensor"||profile=="water-leak-rain-sensor"||profile=="vibration-shock-sensor"||profile=="wake-button"||profile=="analog-potentiometer"||profile=="joystick-analog";
 return false;
}
inline unsigned maxSlots(const std::string& group){
#ifdef APP_ESP8266_COMPACT
 return group=="control"?4:group=="input"?4:0;
#else
 return group=="audioIn"||group=="storage"||group=="communication"?1:group=="sensor"||group=="input"?8:group=="control"||group=="audio"||group=="display"||group=="power"?4:0;
#endif
}
inline unsigned countLimit(const std::string& profile){for(const char* item:ELMA_RUNTIME_SINGLETONS)if(profile==item)return 1;return 8;}
inline unsigned pwmLimit(){
#if defined(CONFIG_IDF_TARGET_ESP32C2) || defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6)
 return 6;
#elif defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3)
 return 8;
#else
 return 16;
#endif
}
inline unsigned pwmCost(const std::string& profile){
 for(const auto& item:ELMA_RUNTIME_PWM_COSTS)if(profile==item.profile)return item.cost;
 return 0;
}
inline bool available(const std::string& group,const std::string& profile,unsigned index=0){
 if(profile=="none")return true;
 if(index>=maxSlots(group))return false;
 if(portable(group,profile))return true;
#ifndef APP_ESP8266_COMPACT
 if((group=="audio"||group=="control")&&profile=="buzzer")return true;
 if(group=="power")return profile=="dc-dc-step-down"||profile=="dc-dc-step-up";
 if(group=="sensor"){
  if(profile=="battery-voltage-divider-220k"||profile=="bno055")return true;
#if APP_SUNTON_PANEL == 3
  if(profile=="ldr")return true;
#endif
#if APP_WAVESHARE_AMOLED
  if(profile=="qmi8658")return true;
#endif
 }
 if(group=="control"&&profile=="drv8833-dual-motor-driver")return true;
#if !APP_DISABLE_AUDIO
#if ELMA_LED_EFFECT_MASK & 57344
 if(group=="audioIn")return profile=="i2s-microphone-generic"||profile=="inmp441-i2s-mic"||profile=="sph0645-ics43434-i2s-mic"
#if APP_SPK_BOARD
 ||profile=="spk-dual-mic"
#endif
 ;
#endif
 if(group=="audio")return profile=="buzzer"||(index==0&&(profile=="max98357a-i2s-amp"||profile=="pcm5102-i2s-dac"||profile=="uda1334a-i2s-dac"||profile=="es9023-i2s-dac"||profile=="pt8211-i2s-dac"||profile=="cs4344-i2s-dac"
#if APP_SPK_BOARD
 ||profile=="spk-ns4168"
#endif
#if defined(CONFIG_IDF_TARGET_ESP32)
 ||profile=="internal-dac-gpio25-26"||profile=="analog-line-out-via-dac"||profile=="pam8403-analog-amp"||profile=="tpa3110-tpa3116-analog-amp"
#if APP_SUNTON_PANEL
 ||profile=="sunton-speaker"
#endif
#endif
 ));
#endif
#if !APP_DISABLE_DISPLAY
 if(group=="display")return profile=="spi-tft"||(index==0&&(profile=="i2c-oled"||profile=="waveshare-screen"
#if APP_HAS_ONBOARD_PANEL || APP_WAVESHARE_AMOLED
 ||profile=="viewe-onboard-lcd"
#endif
#if APP_ROTARY_HMI
 ||profile=="viewe-rotary-onboard-lcd"
#endif
 ));
#endif
#if !APP_DISABLE_SD
 if(group=="storage")return profile=="microsd-spi"
#if APP_HAS_ONBOARD_PANEL
 ||profile=="viewe-sdmmc"
#endif
#if APP_WAVESHARE_AMOLED
 ||profile=="waveshare-amoled-sdmmc"
#endif
#if defined(CONFIG_IDF_TARGET_ESP32)
 ||profile=="camera-sdmmc"
#endif
 ;
#endif
#if defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3)
 if(group=="communication"&&profile=="mcp2551")return true;
#endif
#if APP_HAS_ONBOARD_PANEL && !APP_SUNTON_PANEL && !APP_ROTARY_HMI
 if(group=="communication"&&profile=="viewe-ms1285")return true;
#endif
#endif
 return false;
}
inline std::string profileAt(JsonObjectConst profiles,const Group& group,unsigned index){
 auto list=profiles[group.field];
 if(list.is<JsonArrayConst>())return list[index]|"none";
 if(index)return "none";
 if(std::string(group.name)=="audio")return profiles["audioProfile"]|"none";
 if(std::string(group.name)=="display")return profiles["displayProfile"]|"none";
 return "none";
}
inline unsigned pwmUsed(JsonObjectConst profiles,JsonObjectConst helpers,JsonObjectConst device){
 std::string led=device["statusLedType"]|device["ledType"]|"regular";int pin=device["statusLedPin"]|device["pin"]|-1;
 unsigned used=pin>=0&&pin<64?(led=="rgb"?3:led=="regular"?1:0):0;
#if APP_HAS_ONBOARD_PANEL
 ++used;
#endif
 for(const auto& group:groups){unsigned count=profiles[group.field].is<JsonArrayConst>()?profiles[group.field].size():1;
  for(unsigned i=0;i<count;i++){auto profile=profileAt(profiles,group,i);used+=pwmCost(profile);
   if(std::string(group.name)=="display"&&profile=="spi-tft"&&PeripheralPins::number(helpers[std::string(group.name)+":"+std::to_string(i)]["BL"])>=0)++used;
  }
 }
 return used;
}
inline unsigned graphPwm(JsonArrayConst nodes){
 bool pins[64]{};unsigned count=0;for(JsonObjectConst node:nodes)if(node["type"]=="hardware.gpio"&&node["parameters"]["mode"]=="pwm"){
  int pin=node["parameters"]["pin"]|-1;if(pin>=0&&pin<64&&!pins[pin]){pins[pin]=true;++count;}
 }return count;
}
template<class Error> inline bool validateResources(JsonObjectConst profiles,JsonObjectConst helpers,JsonObjectConst device,Error& error,unsigned extraPwm=0){
 if(pwmUsed(profiles,helpers,device)+extraPwm>pwmLimit()){error="Not enough PWM channels for these peripherals, Logics and the status LED; remove a PWM output or change the LED type";return false;}
 return true;
}
inline void capabilities(JsonObject output,bool includeSignals=false,JsonObjectConst profiles={},JsonObjectConst helpers={},JsonObjectConst device={},unsigned extraPwm=0){
 output["schema"]=1;output["dynamic"]=true;
 output["resourceLimits"]["pwmChannels"]=pwmLimit();
 output["resourceAvailable"]["pwmChannels"]=int(pwmLimit())-std::min(int(pwmLimit()),int(pwmUsed(profiles,helpers,device)+extraPwm));
 JsonObject enabled=output["profiles"].to<JsonObject>();
 for(const auto& group:groups){
  JsonArray list=enabled[group.name].to<JsonArray>();list.add("none");
  // A small filter extracts just the profile names from the flash catalogue.
  // Names are generated separately to avoid allocating all driver contracts.
  for(const char* key:ELMA_RUNTIME_PROFILE_KEYS){
   std::string name=key;auto colon=name.find(':');if(name.substr(0,colon)!=group.name)continue;
   std::string profile=name.substr(colon+1);if(available(group.name,profile))list.add(profile);
  }
  for(const char* key:{"audio:spk-ns4168","audio:sunton-speaker","audioIn:i2s-microphone-generic","audioIn:inmp441-i2s-mic","audioIn:sph0645-ics43434-i2s-mic","audioIn:spk-dual-mic","display:spi-tft","display:waveshare-screen","display:viewe-onboard-lcd","display:viewe-rotary-onboard-lcd","sensor:bno055","sensor:ldr","sensor:qmi8658","storage:viewe-sdmmc","storage:camera-sdmmc","storage:waveshare-amoled-sdmmc","communication:mcp2551","communication:viewe-ms1285","power:dc-dc-step-down","power:dc-dc-step-up"}){
   std::string name=key;auto colon=name.find(':');if(name.substr(0,colon)!=group.name)continue;
   auto profile=name.substr(colon+1);bool seen=false;for(JsonVariantConst old:list)if(old==profile.c_str())seen=true;
   if(!seen&&available(group.name,profile))list.add(profile);
  }
  output["maxInstances"][group.name]=maxSlots(group.name);
  for(JsonVariantConst item:list){std::string profile=item.as<std::string>();if(profile=="none")continue;
   unsigned count=0;for(unsigned i=0;i<maxSlots(group.name);i++)if(available(group.name,profile,i))count=i+1;
   output["profileSlots"][group.name][profile]=count;
   output["profileCountLimits"][group.name][profile]=countLimit(profile);
   if(pwmCost(profile))output["resourceCosts"][group.name][profile]=pwmCost(profile);
  }
 }
 if(includeSignals){JsonDocument filter,pins;
  for(const char* key:ELMA_RUNTIME_PROFILE_KEYS){std::string name=key;auto colon=name.find(':');if(available(name.substr(0,colon),name.substr(colon+1)))filter[key]=true;}
  if(!deserializeJson(pins,FPSTR(ELMA_RUNTIME_SIGNALS),DeserializationOption::Filter(filter)))output["pinSignals"].set(pins);
 }
}
inline void configurationCapabilities(JsonObject output,JsonObjectConst config,bool includeSignals=false,unsigned extraPwm=0){
 JsonDocument profiles,helpers;read(profiles,config["ui"]["peripheralProfiles"]);read(helpers,config["ui"]["peripheralHelperBindings"]);
 capabilities(output,includeSignals,profiles.as<JsonObjectConst>(),helpers.as<JsonObjectConst>(),config["device"],extraPwm);
}
template<class Error> inline bool validate(JsonObjectConst profiles,JsonObjectConst previous,Error& error){
 for(const auto& group:groups){
  unsigned count=profiles[group.field].is<JsonArrayConst>()?profiles[group.field].size():1;
  for(unsigned i=0;i<count;i++){
   std::string profile=profileAt(profiles,group,i);
   unsigned instances=0;for(unsigned j=0;j<count;j++)if(profileAt(profiles,group,j)==profile)++instances;
   if(profile!="none"&&instances>countLimit(profile)&&profiles[group.field]!=previous[group.field]){error="Only one instance of this peripheral driver is supported";return false;}
   // Old projects remain readable; an unsupported legacy entry cannot be added
   // or duplicated through the API. The UI marks it unavailable.
   if(!available(group.name,profile,i)&&profile!=profileAt(previous,group,i)){
    error=("Peripheral driver unavailable in this firmware: "+std::string(group.name)+":"+profile).c_str();return false;
   }
  }
 }
 return true;
}
inline bool validId(const std::string& id){
 if(id.empty()||id.size()>64)return false;
 for(char c:id)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c==':'||c=='-'||c=='_'))return false;
 return true;
}
template<class Factory,class Error> inline bool assignIds(JsonObjectConst profiles,JsonObject helpers,JsonArrayConst current,Factory createId,Error& error){
 JsonDocument seen;
 for(const auto& group:groups){
  unsigned count=profiles[group.field].is<JsonArrayConst>()?profiles[group.field].size():1,oldCount=0;
  for(JsonObjectConst old:current)if(old["binding"]["group"]==group.name)oldCount=std::max(oldCount,unsigned(old["binding"]["index"]|0)+1);
  for(unsigned i=0;i<count;i++){
   auto profile=profileAt(profiles,group,i);if(profile=="none")continue;
   std::string key=std::string(group.name)+":"+std::to_string(i);
   JsonObject slot=helpers[key].is<JsonObject>()?helpers[key].as<JsonObject>():helpers[key].to<JsonObject>();
   std::string id=slot["_runtimeProfile"]==profile.c_str()?slot["_runtimeId"]|"":"";
   if(!id.empty()&&!validId(id)){error="Invalid peripheral identity";return false;}
   if(id.empty())for(JsonObjectConst old:current)if(old["binding"]["group"]==group.name&&old["peripheral"]["profile"]==profile.c_str()){
    bool match=oldCount==count ? old["binding"]["index"]==i : true;
    if(oldCount!=count)for(JsonPairConst pin:old["binding"]["pins"].as<JsonObjectConst>())if(PeripheralPins::number(slot[pin.key().c_str()])!=PeripheralPins::number(pin.value()))match=false;
    if(match){id=old["peripheral"]["id"]|"";break;}
   }
   if(id.empty())id=createId();
   if(!validId(id)||seen[id]==true){error="Duplicate or invalid peripheral identity";return false;}
   seen[id]=true;slot["_runtimeId"]=id;slot["_runtimeProfile"]=profile;
  }
 }
 return !seen.overflowed();
}

template<class Error> inline bool reconcile(JsonDocument& destination,JsonArrayConst current,JsonObjectConst profiles,JsonObjectConst helpers,JsonObjectConst config,Error& error){
 JsonDocument filter,templates;
 for(const auto& group:groups){unsigned count=profiles[group.field].is<JsonArrayConst>()?profiles[group.field].size():1;
  for(unsigned i=0;i<count;i++){std::string profile=profileAt(profiles,group,i);if(profile!="none"&&available(group.name,profile,i))filter["profiles"][std::string(group.name)+":"+profile]=true;}
 }
 if(deserializeJson(templates,FPSTR(ELMA_RUNTIME_PERIPHERALS),DeserializationOption::Filter(filter))){error="Cannot allocate peripheral driver contracts";return false;}
 JsonDocument contracts;filter.clear();
 for(JsonPairConst entry:templates["profiles"].as<JsonObjectConst>())filter["contracts"][entry.value()["contract"].as<const char*>()]=true;
 if(deserializeJson(contracts,FPSTR(ELMA_RUNTIME_PERIPHERALS),DeserializationOption::Filter(filter))){error="Cannot allocate peripheral action contracts";return false;}
 JsonArray result=destination.to<JsonArray>();
 for(JsonObjectConst old:current)if(old["binding"]["kind"]!="peripheral")result.add(old);
 for(const auto& group:groups){unsigned count=profiles[group.field].is<JsonArrayConst>()?profiles[group.field].size():1;
  for(unsigned i=0;i<count;i++){
   std::string profile=profileAt(profiles,group,i);if(profile=="none")continue;
   std::string id="web:"+std::string(group.name)+":"+std::to_string(i)+":"+profile;
   JsonObjectConst definition=templates["profiles"][std::string(group.name)+":"+profile];
   JsonArrayConst source=contracts["contracts"][definition["contract"]|""].as<JsonArrayConst>();
   for(JsonObjectConst old:current)if(old["binding"]["group"]==group.name&&old["binding"]["index"]==i&&old["peripheral"]["profile"]==profile){id=old["peripheral"]["id"]|id;break;}
   auto slot=helpers[std::string(group.name)+":"+std::to_string(i)];
   if(slot["_runtimeProfile"]==profile.c_str()){
    std::string saved=slot["_runtimeId"]|"";if(validId(saved))id=saved;
   }
   if(source.isNull()){
    for(JsonObjectConst old:current)if(old["binding"]["group"]==group.name&&old["binding"]["index"]==i&&old["peripheral"]["profile"]==profile)result.add(old);
    continue;
   }
   for(JsonObjectConst contract:source){
    JsonObject node=result.add<JsonObject>();node.set(contract);
    node["title"]=std::string(definition["title"]|"")+" · "+std::string(contract["title"]|"");node["category"]="Active peripherals";node["helpId"].set(definition["helpId"]);
    node["binding"].set(definition["binding"]);
    node["peripheral"]["group"]=group.name;node["peripheral"]["profile"]=profile;
    node["peripheral"]["id"]=id;node["peripheral"]["slotId"]=std::string(group.name)+"-"+std::to_string(i);
    node["binding"]["index"]=i;PeripheralPins::apply(node,helpers,config);
    for(JsonVariantConst required:node["binding"]["requiredPins"].as<JsonArrayConst>()){
     const char* signal=required|"";
     if(PeripheralPins::number(node["binding"]["pins"][signal])<0){error=("Assign a GPIO for "+profile+" "+signal).c_str();return false;}
    }
    if(profile=="ws2812-neopixel-led-strip"){
#if defined(CONFIG_IDF_TARGET_ESP32C2)
     node["binding"]["maxCount"]=32;
#elif defined(ESP8266)
     node["binding"]["maxCount"]=128;
#else
     node["binding"]["maxCount"]=256;
#endif
    }
   }
  }
 }
 if(destination.overflowed()){error="Insufficient memory for selected peripherals";return false;}
 return true;
}
}
