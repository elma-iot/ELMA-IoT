#include "logic_runtime.h"
#include "logic_validation.h"
#include <cassert>
#include <fstream>
#include <sstream>
#include <iostream>
int main(){
 std::ifstream file("Firmware/tests/led_boolean_mix.graph.json");std::stringstream raw;raw<<file.rdbuf();
 JsonDocument graph;assert(!deserializeJson(graph,raw.str()));
 auto edges=graph["connections"].as<JsonArray>();
 for(int i=int(edges.size())-1;i>=0;--i)if(edges[i]["source"]["port"]=="changed")edges.remove(i);
 auto start=graph["nodes"].as<JsonArray>().add<JsonObject>();start["id"]="startup";start["type"]="event.start";start["parameters"].to<JsonObject>();
 auto e=edges.add<JsonObject>();e["id"]="startup-on";e["source"]["node"]="startup";e["source"]["port"]="out";e["target"]["node"]="led";e["target"]["port"]="on";
 auto shutdown=graph["nodes"].as<JsonArray>().add<JsonObject>();shutdown["id"]="shutdown";shutdown["type"]="mainboard.device.shutting_down";shutdown["parameters"].to<JsonObject>();
 auto definition=graph["devices"].as<JsonArray>().add<JsonObject>();definition["type"]="mainboard.device.shutting_down";definition["parameters"].to<JsonObject>();definition["binding"]["kind"]="lifecycle";definition["binding"]["event"]="shutting_down";
 auto port=definition["ports"].to<JsonArray>().add<JsonObject>();port["id"]="out";port["type"]="execution";port["direction"]="output";
 auto off=edges.add<JsonObject>();off["id"]="shutdown-off";off["source"]["node"]="shutdown";off["source"]["port"]="out";off["target"]["node"]="led";off["target"]["port"]="off";
 auto group=graph["groups"].to<JsonArray>().add<JsonObject>();group["id"]="automation";group["name"]="Boot";group["color"]="#335577";group["mode"]="playing";
 auto members=group["nodes"].to<JsonArray>();for(JsonObjectConst n:graph["nodes"].as<JsonArrayConst>())members.add(n["id"]);
 for(bool initiallyAvailable:{true,false}){
  // Fresh runtime is a reboot: no browser observation or control request.
  JsonDocument compiled;std::string error;if(!ElmaLogic::validateEditable(graph,graph["devices"],compiled,error)){std::cerr<<error<<"\n";return 1;}
  ElmaLogic::Runtime runtime;unsigned calls=0,red=999;bool on=false;
  assert(runtime.begin(std::move(compiled),[&](JsonObjectConst,JsonVariantConst args,std::string&){++calls;red=args["red"]|999u;on=args["action"]!="off";return true;},error));
  JsonDocument status;if(initiallyAvailable){status["network"]["apMode"]=false;status["network"]["wifiConnected"]=false;}
  runtime.tick(0,status);if(initiallyAvailable){assert(on&&red==0&&calls==1);}else assert(calls==0);
  status["network"]["apMode"]=false;status["network"]["wifiConnected"]=true;
  runtime.tick(20,status);assert(on&&red==150);assert(runtime.error().empty());
  unsigned unchanged=calls;runtime.tick(40,status);assert(calls==unchanged);
  // Stop/Start is no longer necessary when Wi-Fi status changes later.
  status["network"]["wifiConnected"]=false;runtime.tick(60,status);assert(red==0);
  status["network"]["wifiConnected"]=true;runtime.tick(80,status);assert(red==150);
  runtime.lifecycle("shutting_down");assert(!on);unchanged=calls;
  status["network"]["wifiConnected"]=false;runtime.tick(100,status);assert(calls==unchanged&&!on);
  assert(runtime.controlGroup("automation","stopped",120));runtime.tick(140,status);assert(calls==unchanged);
  assert(runtime.controlGroup("automation","playing",160));runtime.tick(180,status);assert(on);
 }
 for(const char* mode:{"stopped","paused"}){
  group["mode"]=mode;JsonDocument compiled;std::string error;if(!ElmaLogic::validateEditable(graph,graph["devices"],compiled,error)){std::cerr<<error<<"\n";return 1;}
  ElmaLogic::Runtime runtime;unsigned calls=0;assert(runtime.begin(std::move(compiled),[&](JsonObjectConst,JsonVariantConst,std::string&){++calls;return true;},error));
  JsonDocument status;status["network"]["wifiConnected"]=true;status["network"]["apMode"]=false;runtime.tick(20,status);assert(calls==0);
 }
 std::cout<<"PASS boot On follows wired Wi-Fi/RGB without browser or Stop/Start; unavailable startup retries; Off/Stop/Pause stay inactive\n";
}
