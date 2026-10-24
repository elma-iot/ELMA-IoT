#include "logic_runtime.h"
#include "logic_validation.h"
#include <fstream>
#include <sstream>
#include <cassert>
int main(){
 std::ifstream file("Firmware/tests/led_boolean_mix.graph.json");std::stringstream buffer;buffer<<file.rdbuf();
 JsonDocument graph;assert(!deserializeJson(graph,buffer.str()));
 for(int base:{0,20}){
  graph["nodes"][6]["parameters"]["baseRed"]=base;
  JsonDocument compiled;std::string error;assert(ElmaLogic::validateEditable(graph,graph["devices"],compiled,error));
  unsigned red=999,calls=0;ElmaLogic::Runtime runtime;
  assert(runtime.begin(std::move(compiled),[&](JsonObjectConst,JsonVariantConst args,std::string&){assert(args["action"]=="write");red=args["red"].as<unsigned>();++calls;return true;},error));
  JsonDocument status;auto network=status["network"].to<JsonObject>();
  auto tick=[&](unsigned now,bool ap,bool sta,unsigned expected){network["apMode"]=ap;network["wifiConnected"]=sta;runtime.tick(now,status.as<JsonVariantConst>());assert(runtime.error().empty());assert(red==expected);};
  tick(0,true,true,base?255:250);const auto initial=calls;
  runtime.releaseEvaluationCache(); // editing must preserve previous signatures and action state
  tick(20,true,true,base?255:250);assert(calls==initial); // no repeated action for unchanged values
  tick(40,false,true,150+base);
  tick(60,false,false,base);
  tick(80,true,false,100+base);
  tick(240,true,true,150+base); // AP's blink is dark; STA's solid color remains.
  tick(400,true,true,base?255:250);
  JsonDocument live;runtime.telemetry(live.to<JsonObject>());assert(live["values"]["brightness"]["value"]==100);
 }
}
