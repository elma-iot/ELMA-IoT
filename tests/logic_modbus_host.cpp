#include "logic_runtime.h"
#include "logic_validation.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <iostream>
int main(int argc,char** argv){
 assert(argc==2);std::ifstream file(argv[1]);std::string program((std::istreambuf_iterator<char>(file)),{}),error;
 JsonDocument input,accepted;assert(!deserializeJson(input,program));assert(ElmaLogic::validateEditable(input.as<JsonVariantConst>(),input["devices"],accepted,error));
 ElmaLogic::Runtime runtime;int reads=0,plots=0,last=-1;
 assert(runtime.begin(program.c_str(),[&](JsonObjectConst n,JsonVariantConst a,std::string&){
  if(n["type"]=="hardware.modbus.read"){assert(a["action"]=="read"&&a["unit"]==1&&a["baud"]==9600&&a["count"]==1);reads++;}
  else {assert(n["type"]=="mainboard.plot");assert(!a["value"].isNull());last=a["value"].as<int>();plots++;}return true;
 },error));
 JsonDocument state;auto rs=state["rs485"].to<JsonObject>();rs["rxSequence"]=0;rs["ready"]=false;runtime.tick(0,state.as<JsonVariantConst>());assert(reads==1&&plots==0);
 rs["rxSequence"]=1;rs["ready"]=true;rs["received"]["unit"]=1;rs["values"][0]=42;runtime.tick(1000,state.as<JsonVariantConst>());assert(plots==1&&last==42);
 runtime.tick(2000,state.as<JsonVariantConst>());assert(plots==1&&reads==1);
 rs["rxSequence"]=2;rs["received"]["unit"]=2;runtime.tick(3000,state.as<JsonVariantConst>());assert(plots==1);
 rs["rxSequence"]=3;rs["received"]["unit"]=1;rs["values"][0]=99;runtime.tick(7000,state.as<JsonVariantConst>());assert(plots==2&&last==99);
 assert(runtime.error().empty());std::cout<<"Modbus graph validation, queued read, receive edge/filter and value routing passed\n";
}
