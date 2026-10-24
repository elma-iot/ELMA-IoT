#include "peripheral_pin_binding.h"
#include "logic_runtime.h"
#include <cassert>
#include <iostream>
int main(){
 JsonDocument node,helpers,config;
 for(const char* group:{"audio","audioIn","display","sensor","input","power","control","expansion","storage","communication"}){
  node.clear();helpers.clear();node["peripheral"]["profile"]="test-profile";
  node["binding"]["kind"]="peripheral";node["binding"]["group"]=group;node["binding"]["index"]=0;node["binding"]["pins"]["SIG"]=12;
  helpers[std::string(group)+":0"]["SIG"]="0";
  assert(PeripheralPins::changed(node.as<JsonObjectConst>(),helpers.as<JsonObjectConst>(),config.as<JsonObjectConst>()));
  PeripheralPins::apply(node.as<JsonObject>(),helpers.as<JsonObjectConst>(),config.as<JsonObjectConst>());assert(node["binding"]["pins"]["SIG"]==0);
 }
 node.clear();helpers.clear();node["binding"]["kind"]="peripheral";node["binding"]["group"]="audio";node["binding"]["index"]=0;node["peripheral"]["profile"]="pcm5102-i2s-dac";node["binding"]["pins"]["WS"]=12;
 helpers["audio:0"]["WS"]=17;config["audio"]["wsPin"]=0;
 PeripheralPins::apply(node.as<JsonObject>(),helpers.as<JsonObjectConst>(),config.as<JsonObjectConst>());assert(node["binding"]["pins"]["WS"]==0);
 node["binding"]["group"]="sensor";node["binding"]["index"]=1;node["peripheral"]["profile"]="battery-voltage-divider-220k";node["binding"]["pins"].clear();node["binding"]["pins"]["SIGNAL"]=32;
 config["_firstDivider"]=0;config["battery"]["adcPin"]=34;helpers["sensor:1"]["SIGNAL"]=35;
 PeripheralPins::apply(node.as<JsonObject>(),helpers.as<JsonObjectConst>(),config.as<JsonObjectConst>());assert(node["binding"]["pins"]["SIGNAL"]==35);
 helpers["sensor:1"]["SIGNAL"]="";PeripheralPins::apply(node.as<JsonObject>(),helpers.as<JsonObjectConst>(),config.as<JsonObjectConst>());assert(node["binding"]["pins"]["SIGNAL"]==-1);
 const char* program=R"({"schemaVersion":1,"nodes":[{"id":"s","type":"event.start"},{"id":"d","type":"timing.delay","parameters":{"seconds":0.1}},{"id":"r","type":"peripheral.on","peripheral":{"id":"relay"},"binding":{"kind":"peripheral","group":"control","index":0,"pins":{"SIG":12}}}],"connections":[{"source":{"node":"s","port":"out"},"target":{"node":"d","port":"in"}},{"source":{"node":"d","port":"out"},"target":{"node":"r","port":"in"}}],"groups":[{"id":"g","mode":"playing","nodes":["s","d","r"]}]})";
 ElmaLogic::Runtime runtime;std::string error;unsigned calls=0;
 assert(runtime.begin(program,[&](JsonObjectConst n,JsonVariantConst,std::string&){assert(n["binding"]["pins"]["SIG"]==0);++calls;return true;},error));
 JsonDocument status,pins;pins["SIG"]=0;
 runtime.tick(0,status.as<JsonVariantConst>());runtime.tick(50,status.as<JsonVariantConst>());
 runtime.setPeripheralPins("relay",pins.as<JsonObjectConst>());
 assert(runtime.nodeRunning("r"));assert(runtime.graph()["groups"][0]["mode"]=="playing");
 runtime.tick(99,status.as<JsonVariantConst>());assert(calls==0);
 runtime.tick(100,status.as<JsonVariantConst>());assert(calls==1);
 runtime.tick(200,status.as<JsonVariantConst>());assert(calls==1);
 std::cout<<"PASS all peripheral groups, GPIO0, primary mappings and timer/group continuity\n";
}
