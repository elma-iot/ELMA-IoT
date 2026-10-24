#pragma once
#include <ArduinoJson.h>
#include <cstdlib>
#include <cstring>
#include <string>

// Update existing numeric slots only: no graph clone or scheduler restart.
namespace PeripheralPins {
inline int number(JsonVariantConst value,int fallback=-1){
 if(value.isNull())return fallback;
 long pin=-1;
 if(value.is<int>())pin=value.as<int>();
 else if(value.is<const char*>()){
  const char* text=value.as<const char*>();char* end=nullptr;
  pin=std::strtol(text,&end,10);if(end==text||*end)return -1;
 }
 return pin>=0&&pin<64?int(pin):-1;
}
inline int resolve(JsonObjectConst node,const char* signal,JsonObjectConst helpers,JsonObjectConst config){
 const std::string group=node["binding"]["group"]|"",profile=node["peripheral"]["profile"]|"";
 const int index=node["binding"]["index"]|0;
 const char* section=nullptr;const char* key=nullptr;
 if(index==0&&group=="audio"&&profile.find("buzzer")==std::string::npos&&profile.find("bluetooth")==std::string::npos){
  section="audio";if(!strcmp(signal,"WS"))key="wsPin";if(!strcmp(signal,"BCLK"))key="bclkPin";if(!strcmp(signal,"DIN"))key="doutPin";
 }else if(index==0&&group=="display"&&profile=="i2c-oled"){
  section="oled";if(!strcmp(signal,"SDA"))key="sdaPin";if(!strcmp(signal,"SCL"))key="sclPin";if(!strcmp(signal,"RST"))key="resetPin";
 }else if(index==0&&group=="storage"&&profile=="microsd-spi"){
  section="sd";if(!strcmp(signal,"CS"))key="csPin";if(!strcmp(signal,"SCK"))key="sckPin";if(!strcmp(signal,"MOSI"))key="mosiPin";if(!strcmp(signal,"MISO"))key="misoPin";
 }else if(group=="sensor"&&profile.find("voltage-divider")!=std::string::npos&&index==(config["_firstDivider"]|-1)&&!strcmp(signal,"SIGNAL")){
  int pin=number(config["battery"]["adcPin"]);return pin==0?-1:pin;
 }
 if(section&&key&&!config[section][key].isNull())return number(config[section][key]);
 JsonObjectConst slot=helpers[group+":"+std::to_string(index)];
 if(slot.containsKey(signal))return number(slot[signal]);
 return number(node["binding"]["pins"][signal]);
}
inline bool changed(JsonObjectConst node,JsonObjectConst helpers,JsonObjectConst config){
 if(node["binding"]["kind"]!="peripheral")return false;
 for(JsonPairConst p:node["binding"]["pins"].as<JsonObjectConst>())if(number(p.value())!=resolve(node,p.key().c_str(),helpers,config))return true;
 return false;
}
inline void apply(JsonObject node,JsonObjectConst helpers,JsonObjectConst config){
 for(JsonPair p:node["binding"]["pins"].as<JsonObject>())p.value().set(resolve(node,p.key().c_str(),helpers,config));
}
}
