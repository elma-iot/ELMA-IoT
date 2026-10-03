#include "logic_runtime.h"
#include <cassert>
#include <iostream>

int main(){
 const char* program=R"json({"schemaVersion":1,"nodes":[
 {"id":"start","type":"mainboard.control","binding":{"kind":"lifecycle","event":"go"}},
 {"id":"reset","type":"mainboard.control","binding":{"kind":"lifecycle","event":"reset"}},
 {"id":"stop","type":"mainboard.control","binding":{"kind":"lifecycle","event":"stop"}},
 {"id":"timer","type":"timing.countdown","parameters":{"seconds":5},"ports":[{"id":"remaining","direction":"output","type":"number"},{"id":"running","direction":"output","type":"boolean"}]},
 {"id":"led","type":"hardware.led","parameters":{"red":255,"green":255,"blue":255,"brightness":100}}
 ],"connections":[
 {"source":{"node":"start","port":"out"},"target":{"node":"timer","port":"in"}},
 {"source":{"node":"reset","port":"out"},"target":{"node":"timer","port":"reset"}},
 {"source":{"node":"stop","port":"out"},"target":{"node":"timer","port":"stop"}},
 {"source":{"node":"timer","port":"out"},"target":{"node":"led","port":"toggle"}}
 ],"groups":[{"id":"g","mode":"playing","nodes":["timer","led"]}]})json";
 ElmaLogic::Runtime runtime;std::string error;int actions=0;JsonDocument status,live;
 assert(runtime.begin(program,[&](JsonObjectConst,JsonVariantConst args,std::string&){assert(args["action"]=="toggle");++actions;return true;},error));
 auto tick=[&](uint32_t now){runtime.tick(now,status.as<JsonVariantConst>());live.clear();runtime.telemetry(live.to<JsonObject>());};
 auto remaining=[&](){return live["values"]["timer"]["remaining"].as<double>();};
 tick(0);assert(remaining()==5);runtime.lifecycle("go");tick(1000);assert(remaining()==4);assert(live["values"]["timer"]["running"]==true);
 runtime.lifecycle("reset");tick(2000);assert(remaining()==4);tick(5999);assert(actions==0);tick(6000);assert(actions==1&&remaining()==0);assert(live["values"]["timer"]["running"]==false);
 tick(20000);assert(actions==1);runtime.lifecycle("reset");tick(20001);assert(remaining()==5);tick(30000);assert(actions==1); // reset idle must not start
 runtime.lifecycle("go");tick(31000);runtime.lifecycle("stop");tick(40000);assert(actions==1&&remaining()==5);
 runtime.lifecycle("go");tick(41000);runtime.lifecycle("go");tick(45999);assert(actions==1);tick(46000);assert(actions==2); // restart active
 runtime.lifecycle("go");tick(47000);runtime.pause(47000);tick(55000);assert(remaining()==4&&live["values"]["timer"]["running"]==false);runtime.resume(57000);tick(60999);assert(actions==2);tick(61000);assert(actions==3);
 runtime.lifecycle("go");tick(62000);assert(runtime.controlGroup("g","paused",62000));tick(70000);assert(actions==3);assert(runtime.controlGroup("g","playing",72000));tick(75999);assert(actions==3);tick(76000);assert(actions==4);
 runtime.restart();tick(0xfffffff0u);runtime.lifecycle("go");tick(uint32_t(0xfffffff0u+4999u));assert(actions==4);tick(uint32_t(0xfffffff0u+5000u));assert(actions==5&&remaining()==0);
 assert(runtime.error().empty());std::cout<<"PASS: countdown, reset, stop, restart, single completion, global/group pause and clock rollover\n";
}
