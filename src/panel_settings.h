#pragma once
#include <ArduinoJson.h>
#include <string>
#include <cstdlib>
namespace PanelSettings {
inline JsonVariantConst get(JsonVariantConst value,const std::string& path) {
 size_t start=0;while(start<path.size()){size_t end=path.find('/',start);std::string key=path.substr(start,end-start);value=value.is<JsonArrayConst>()?value[std::strtoul(key.c_str(),nullptr,10)]:value[key];if(end==std::string::npos)break;start=end+1;}return value;
}
inline bool set(JsonVariant value,const std::string& path,JsonVariantConst incoming) {
 size_t start=0;while(start<path.size()){size_t end=path.find('/',start);std::string key=path.substr(start,end-start);
 if(key.empty())return false;
 if(end==std::string::npos){
   if(value.is<JsonArray>())return value[std::strtoul(key.c_str(),nullptr,10)].set(incoming);
   return value[key].set(incoming);
 }
 if(!value.is<JsonArray>()&&value[key].isNull())value[key].to<JsonObject>();
 if(value.is<JsonArray>())value=value[std::strtoul(key.c_str(),nullptr,10)].as<JsonVariant>();
 else value=value[key].as<JsonVariant>();
 start=end+1;}return false;
}
// An untouched field follows live values. A local edit survives a remote update
// and remains dirty until the exact submitted value is acknowledged.
inline bool acknowledged(const std::string& submitted,const std::string& incoming){return submitted==incoming;}
}
