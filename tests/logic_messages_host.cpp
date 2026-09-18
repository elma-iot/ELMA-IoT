#include "logic_runtime.h"
#include "logic_text.h"
#include <fstream>
#include <iterator>
#include <cassert>
#include <iostream>
int main(int argc,char**argv){assert(argc==2);std::ifstream f(argv[1]);std::string graph((std::istreambuf_iterator<char>(f)),{}),error;ElmaLogic::Runtime runtime;int calls=0;assert(runtime.begin(graph.c_str(),[&](JsonObjectConst node,JsonVariantConst args,std::string&){const std::string type=node["type"].as<std::string>();if(type=="mainboard.mqtt.publish"){assert(args["topic"]=="elma/test");assert(args["payload"]=="Temperature: 60");assert(args["qos"]==1);}else if(type=="peripheral.text"){assert(args["text"]=="Temperature: 60");assert(args["seconds"]==5);}else assert(false);++calls;return true;},error));JsonDocument status;runtime.tick(0,status.as<JsonVariantConst>());assert(calls==2);assert(runtime.error().empty());
JsonDocument value;std::string result;value.set(false);assert(ElmaLogic::appendText("Enabled",value.as<JsonVariantConst>(),": ",result));assert(result=="Enabled: false");value.set("ready");assert(ElmaLogic::appendText("State",value.as<JsonVariantConst>(),": ",result));assert(result=="State: ready");value.to<JsonObject>()["bad"]=1;assert(!ElmaLogic::appendText("",value.as<JsonVariantConst>(),"",result));value.clear();assert(!ElmaLogic::appendText(std::string(1025,'x'),value.as<JsonVariantConst>(),"",result));std::cout<<"PASS: actual runtime Text + live value, MQTT payload and OLED action; primitive serialization and size limits\n";}
