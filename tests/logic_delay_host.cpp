#include "logic_runtime.h"
#include <cassert>
#include <chrono>
#include <iostream>
int main(){
 const char* graph=R"json({"schemaVersion":1,"nodes":[
 {"id":"go","type":"mainboard.control","binding":{"kind":"lifecycle","event":"go"}},
 {"id":"other","type":"mainboard.control","binding":{"kind":"lifecycle","event":"other"}},
 {"id":"delay","type":"timing.delay","parameters":{"seconds":5}},
 {"id":"waited","type":"hardware.led"},{"id":"immediate","type":"hardware.led"}
 ],"connections":[
 {"source":{"node":"go","port":"out"},"target":{"node":"delay","port":"in"}},
 {"source":{"node":"delay","port":"out"},"target":{"node":"waited","port":"toggle"}},
 {"source":{"node":"other","port":"out"},"target":{"node":"immediate","port":"toggle"}}
 ]})json";
 ElmaLogic::Runtime runtime;std::string error;JsonDocument status;int waited=0,immediate=0;
 assert(runtime.begin(graph,[&](JsonObjectConst node,JsonVariantConst,std::string&){if(node["id"]=="waited")++waited;else ++immediate;return true;},error));
 auto tick=[&](uint32_t t){runtime.tick(t,status.as<JsonVariantConst>());};
 auto start=std::chrono::steady_clock::now();
 tick(0);runtime.lifecycle("go");tick(1000);runtime.lifecycle("other");tick(1001);
 assert(immediate==1&&waited==0);tick(4999);assert(waited==0);tick(5000);assert(waited==1);
 tick(10000);assert(waited==1);
 // Re-triggering restarts this node's deadline, without blocking other nodes.
 runtime.lifecycle("go");tick(12000);runtime.lifecycle("go");tick(15000);assert(waited==1);tick(17000);assert(waited==2);
 runtime.restart();tick(0xfffffff0u);runtime.lifecycle("go");tick(uint32_t(0xfffffff0u+4999u));assert(waited==2);tick(uint32_t(0xfffffff0u+5000u));assert(waited==3);
 assert(std::chrono::steady_clock::now()-start<std::chrono::seconds(2));assert(runtime.error().empty());
 std::cout<<"PASS: nonblocking Delay, independent execution, deadline/retrigger, single completion and clock rollover\n";
}
