#include "logic_validation.h"
#include <fstream>
#include <iterator>
#include <cassert>
#include <iostream>
int main(int argc,char**argv){assert(argc==2);std::ifstream f(argv[1]);std::string raw((std::istreambuf_iterator<char>(f)),{}),error;JsonDocument input,output;assert(!deserializeJson(input,raw));auto devices=input["devices"].as<JsonArrayConst>();assert(ElmaLogic::validateEditable(input.as<JsonVariantConst>(),devices,output,error));assert(output["nodes"].size()==6);assert(output["connections"].size()==7);
for(JsonObject node:input["nodes"].as<JsonArray>())if(node["type"]=="mainboard.mqtt.publish"){node["parameters"]["topic"]="elma/#";break;}assert(!ElmaLogic::validateEditable(input.as<JsonVariantConst>(),devices,output,error));assert(error.find("topic")!=std::string::npos);
assert(!deserializeJson(input,raw));devices=input["devices"].as<JsonArrayConst>();auto edge=input["connections"][0];edge["target"]["node"]=input["nodes"][2]["id"];edge["target"]["port"]="append";assert(!ElmaLogic::validateEditable(input.as<JsonVariantConst>(),devices,output,error));std::cout<<"PASS: actual firmware edit validator accepts typed MQTT OLED relay graph, rejects wildcard topic and execution-to-value wiring\n";}
