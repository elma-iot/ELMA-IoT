#include "logic_runtime.h"
#include <cassert>
#include <iostream>
int main(){
 const char* program=R"json({"schemaVersion":1,"nodes":[
 {"id":"alarm","type":"clock.alarm","parameters":{"enabled":true,"clockSource":"manual","schedule":"weekdays","weekdays":"1111100","alarmTime":"07:00:00","manualDate":"2026-10-05","manualTime":"06:59:59"},"ports":[{"id":"valid","direction":"output","type":"boolean"},{"id":"epoch","direction":"output","type":"number"},{"id":"clockTime","direction":"output","type":"string"}]},
 {"id":"led","type":"hardware.led","parameters":{}}],
 "connections":[{"source":{"node":"alarm","port":"out"},"target":{"node":"led","port":"toggle"}}],
 "groups":[{"id":"g","mode":"playing","nodes":["alarm","led"]}]})json";
 ElmaLogic::Runtime runtime;std::string error;int actions=0,sets=0;JsonDocument status,live;int64_t deadline;
 assert(ElmaLogic::alarmDateTime("2026-10-05","07:00:00",deadline));
 assert(runtime.begin(program,[&](JsonObjectConst node,JsonVariantConst args,std::string&){
  if(node["type"]=="clock.alarm"){assert(args["action"]=="setClock");assert(args["source"]=="manual");assert(args["epoch"].as<int64_t>()==deadline-1);++sets;}
  else {assert(args["action"]=="toggle");++actions;}return true;
 },error));
 uint32_t ms=0;auto tick=[&](int64_t epoch){status["clock"]["manual"]=epoch;runtime.tick(ms+=500,status.as<JsonVariantConst>());live.clear();runtime.telemetry(live.to<JsonObject>());};
 tick(0);assert(!live["values"]["alarm"]["valid"].as<bool>());assert(actions==0);
 assert(runtime.testAction("alarm","setClock",error));assert(sets==1);
 tick(deadline-1);assert(live["values"]["alarm"]["valid"]==true);assert(live["values"]["alarm"]["clockTime"]=="2026-10-05 06:59:59Z");
 tick(deadline);assert(actions==1);tick(deadline);assert(actions==1);
 assert(runtime.controlGroup("g","paused",ms));tick(deadline+86400-1);assert(runtime.controlGroup("g","playing",ms));tick(deadline+86400+1);assert(actions==1);
 tick(deadline+2*86400-1);tick(deadline+2*86400);assert(actions==2);
 assert(runtime.error().empty());std::cout<<"PASS: alarm runtime, clock telemetry, manual set, LED execution, grouped pause and no replay\n";
}
