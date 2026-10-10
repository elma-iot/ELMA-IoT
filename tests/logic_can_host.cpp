#include "logic_runtime.h"
#include "logic_validation.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <iostream>
int main(int argc,char** argv){
 assert(argc==2);std::ifstream file(argv[1]);std::string program((std::istreambuf_iterator<char>(file)),{}),error;
 JsonDocument input,accepted;assert(!deserializeJson(input,program));assert(ElmaLogic::validateEditable(input.as<JsonVariantConst>(),input["devices"],accepted,error));
 ElmaLogic::Runtime runtime;int configured=0,sent=0,plots=0,last=-1;
 assert(runtime.begin(program.c_str(),[&](JsonObjectConst n,JsonVariantConst a,std::string&){
  if(n["type"]=="hardware.can.configure"){assert(a["action"]=="configure"&&a["bitrate"]==500000&&a["listenOnly"]==false);configured++;}
  else if(n["type"]=="hardware.can.send"){assert(a["action"]=="send"&&a["identifier"]==256&&a["data"]=="00");sent++;}
  else {assert(n["type"]=="mainboard.plot");last=a["value"].as<int>();plots++;}return true;
 },error));
 JsonDocument state;auto can=state["can"].to<JsonObject>();can["rxSequence"]=0;runtime.tick(0,state.as<JsonVariantConst>());assert(configured==1&&sent==1&&plots==0);
 can["rxSequence"]=1;can["received"]["identifier"]=256;runtime.tick(1000,state.as<JsonVariantConst>());assert(plots==1&&last==256);
 runtime.tick(2000,state.as<JsonVariantConst>());assert(plots==1);
 can["rxSequence"]=2;can["received"]["identifier"]=257;runtime.tick(3000,state.as<JsonVariantConst>());assert(plots==1);
 can["rxSequence"]=3;can["received"]["identifier"]=256;runtime.tick(7000,state.as<JsonVariantConst>());assert(plots==2);
 assert(runtime.error().empty());std::cout<<"CAN graph validation, configure/send arguments and receive edge/filter routing passed\n";
}
