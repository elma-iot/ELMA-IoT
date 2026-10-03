#include "logic_runtime.h"
#include "logic_sleep_contract.h"
#include <cassert>
#include <iostream>
int main(){
 const char* program=R"json({"schemaVersion":1,"nodes":[
 {"id":"start","type":"mainboard.control","binding":{"kind":"lifecycle","event":"go"}},
 {"id":"sleep","type":"hardware.sleep","parameters":{"mode":"light","timerEnabled":true,"seconds":5},"ports":[{"id":"cause","direction":"output","type":"integer"}]},
 {"id":"wakepin","type":"hardware.wake_gpio","parameters":{"pin":3,"level":"low"},"binding":{"allowedPins":{"3":{"light":true,"deep":true}}}},
 {"id":"wake","type":"event.wake"},{"id":"awake","type":"hardware.led"},{"id":"failed","type":"hardware.led"},{"id":"event","type":"hardware.led"}],
 "connections":[
 {"source":{"node":"start","port":"out"},"target":{"node":"sleep","port":"in"}},
 {"source":{"node":"wakepin","port":"source"},"target":{"node":"sleep","port":"wake"}},
 {"source":{"node":"sleep","port":"out"},"target":{"node":"awake","port":"toggle"}},
 {"source":{"node":"sleep","port":"failed"},"target":{"node":"failed","port":"toggle"}},
 {"source":{"node":"wake","port":"out"},"target":{"node":"event","port":"toggle"}}]})json";
 ElmaLogic::Runtime runtime;std::string error;int requests=0,awake=0,failed=0,events=0;bool accept=true;
 assert(runtime.begin(program,[&](JsonObjectConst node,JsonVariantConst args,std::string& err){
  if(node["type"]=="hardware.sleep"){++requests;assert(args["seconds"]==5);assert(args["wake"]["parameters"]["pin"]==3);if(!accept){err="busy";return false;}}
  else if(node["id"]=="awake")++awake;else if(node["id"]=="failed")++failed;else ++events;return true;
 },error));
 JsonDocument status,live;uint32_t ms=0;auto tick=[&](){runtime.tick(ms+=100,status.as<JsonVariantConst>());};
 status["power"]["wakeCount"]=0;status["power"]["sequence"]=0;tick();assert(events==0);
 runtime.lifecycle("go");runtime.lifecycle("go");tick();assert(requests==1&&awake==0); // pending is not completion
 status["power"]["node"]="sleep";status["power"]["sequence"]=1;status["power"]["wakeCount"]=1;status["power"]["cause"]=4;status["power"]["error"]="";tick();assert(awake==1&&events==1);tick();assert(awake==1&&events==1);
 runtime.telemetry(live.to<JsonObject>());assert(live["values"]["sleep"]["cause"]==4);
 runtime.lifecycle("go");status["power"]["sequence"]=2;status["power"]["error"]="wake level already active";tick();assert(failed==1&&awake==1&&events==1);
 accept=false;runtime.lifecycle("go");assert(failed==2&&requests==3);
 runtime.restart();tick();assert(events==2); // fresh runtime after deep boot can observe retained wake cause
 assert(ElmaLogic::validSleepSeconds(.001)&&ElmaLogic::validSleepSeconds(604800));assert(!ElmaLogic::validSleepSeconds(0));
 std::cout<<"PASS: sleep queues once; awake/event only on success; failures; wake metadata and timer bounds\n";
}
