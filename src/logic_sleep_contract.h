#pragma once
#include <ArduinoJson.h>
#include <cmath>
#include <string>
namespace ElmaLogic {
inline bool validSleepSeconds(double seconds){return std::isfinite(seconds)&&seconds>=.001&&seconds<=604800;}
inline bool validSleepHardware(JsonObject node,std::string& error){
 const std::string type=node["type"]|"";auto p=node["parameters"];
 if(type=="hardware.wake_gpio"){
  if(!p["pin"].is<int>()||node["binding"]["allowedPins"][std::to_string(p["pin"].as<int>())].isNull()||(p["level"]!="high"&&p["level"]!="low")){error="Select an available wake GPIO and High/Low level";return false;}
 }else if(type=="hardware.sleep"){
  bool supported=false;for(JsonVariantConst mode:node["binding"]["modes"].as<JsonArrayConst>())if(mode==p["mode"])supported=true;
  if(!supported){error="Sleep mode unavailable on this chip";return false;}
  for(JsonObject port:node["ports"].as<JsonArray>())port["enabled"]=!(port["id"]=="out"&&p["mode"]=="deep")&&!(port["id"]=="seconds"&&p["timerEnabled"]==false);
 }
 return true;
}
inline bool validSleepLinks(JsonArrayConst nodes,JsonArrayConst links,std::string& error){
 for(JsonObjectConst node:nodes)if(node["type"]=="hardware.sleep"){
  auto p=node["parameters"];bool wake=false,secondsLinked=false;
  for(JsonObjectConst link:links)if(link["target"]["node"]==node["id"]){
   if(link["target"]["port"]=="seconds")secondsLinked=true;
   if(link["target"]["port"]=="wake"){
    for(JsonObjectConst source:nodes)if(source["id"]==link["source"]["node"]){
     auto caps=source["binding"]["allowedPins"][std::to_string(source["parameters"]["pin"].as<int>())];
     if(source["type"]!="hardware.wake_gpio"||caps[p["mode"].as<std::string>()]!=true){error="Connected GPIO cannot wake this board in the selected mode";return false;}wake=true;
    }
   }
  }
  if(!wake&&p["timerEnabled"]!=true){error="Sleep needs a timer or Wake GPIO";return false;}
  if(p["timerEnabled"]==true&&!secondsLinked&&!validSleepSeconds(p["seconds"]|0.0)){error="Wake delay must be 0.001–604800 seconds";return false;}
 }
 return true;
}
}
