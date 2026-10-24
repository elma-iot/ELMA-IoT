#include "logic_runtime.h"
#include "logic_validation.h"
#include <cassert>
bool validateLogicHardware(JsonObject,std::string&);
int main(){
 for(const char* effect:{"solid","blink","breathe","colorloop"}){
  JsonDocument graph;
  assert(!deserializeJson(graph,R"json({"schemaVersion":1,"nodes":[
   {"id":"start","type":"event.start","parameters":{},"ports":[{"id":"out","direction":"output","type":"execution"}]},
   {"id":"brightness","type":"value.led_brightness","parameters":{"value":37},"ports":[{"id":"value","direction":"output","type":"number"}]},
   {"id":"effect","type":"value.led_effect","parameters":{"effect":"solid","speed":82},"ports":[{"id":"effect","direction":"output","type":"string"},{"id":"speed","direction":"output","type":"number"}]},
   {"id":"led","type":"hardware.led","binding":{"kind":"led","pin":4,"ledType":"rgb"},"parameters":{"red":0,"green":255,"blue":0,"brightness":20,"effect":"solid","effectSpeed":50},"ports":[{"id":"on","direction":"input","type":"execution"},{"id":"brightness","direction":"input","type":"number"},{"id":"effect","direction":"input","type":"string"},{"id":"effectSpeed","direction":"input","type":"number"}]}],"connections":[
   {"source":{"node":"start","port":"out"},"target":{"node":"led","port":"on"}},
   {"source":{"node":"brightness","port":"value"},"target":{"node":"led","port":"brightness"}},
   {"source":{"node":"effect","port":"effect"},"target":{"node":"led","port":"effect"}},
   {"source":{"node":"effect","port":"speed"},"target":{"node":"led","port":"effectSpeed"}}]})json"));
  graph["nodes"][2]["parameters"]["effect"]=effect;
  std::string error;
  // Exercise the same hardware-port gate used by the device's Save & apply.
  assert(validateLogicHardware(graph["nodes"][3].as<JsonObject>(),error));
  for(JsonObjectConst port:graph["nodes"][3]["ports"].as<JsonArrayConst>())assert(port["enabled"]==true);
  ElmaLogic::Runtime runtime;unsigned actions=0;
  assert(runtime.begin(std::move(graph),[&](JsonObjectConst,JsonVariantConst args,std::string&){
   assert(args["action"]=="on"&&args["brightness"]==37&&args["effect"]==effect&&args["effectSpeed"]==82);++actions;return true;
  },error));
  JsonDocument status;runtime.tick(0,status.as<JsonVariantConst>());runtime.tick(100,status.as<JsonVariantConst>());
  assert(actions==1&&runtime.error().empty());
 }
}
