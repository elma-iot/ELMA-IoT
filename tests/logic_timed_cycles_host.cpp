#include "logic_runtime.h"
#include "logic_validation.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
int main(int argc,char**argv){
 assert(argc==2);std::ifstream file(argv[1]);std::string raw((std::istreambuf_iterator<char>(file)),{}),error;JsonDocument graph,out;
 assert(!deserializeJson(graph,raw));auto validate=[&](){return ElmaLogic::validateEditable(graph.as<JsonVariantConst>(),graph["devices"].as<JsonArrayConst>(),out,error);};
 assert(validate());for(JsonObject n:graph["nodes"].as<JsonArray>())if(n["type"]=="timing.delay")n["type"]="timing.cooldown";
 assert(!validate());assert(error.find("Immediate circular")!=std::string::npos);
 for(JsonObject n:graph["nodes"].as<JsonArray>())if(n["type"]=="timing.cooldown")n["type"]="timing.delay";
 graph["connections"].as<JsonArray>().remove(0);assert(!validate());assert(error.find("event/start")!=std::string::npos);
 const char* loop=R"({"schemaVersion":1,"nodes":[
 {"id":"start","type":"event.start"},
 {"id":"a","type":"mainboard.plot","parameters":{"value":1}},
 {"id":"b","type":"mainboard.plot","parameters":{"value":2}},
 {"id":"c","type":"mainboard.plot","parameters":{"value":3}},
 {"id":"d1","type":"timing.delay","parameters":{"seconds":5}},
 {"id":"d2","type":"timing.delay","parameters":{"seconds":5}},
 {"id":"d3","type":"timing.delay","parameters":{"seconds":5}}],"connections":[
 {"source":{"node":"start","port":"out"},"target":{"node":"a","port":"in"}},
 {"source":{"node":"a","port":"out"},"target":{"node":"d1","port":"in"}},
 {"source":{"node":"d1","port":"out"},"target":{"node":"b","port":"in"}},
 {"source":{"node":"b","port":"out"},"target":{"node":"d2","port":"in"}},
 {"source":{"node":"d2","port":"out"},"target":{"node":"c","port":"in"}},
 {"source":{"node":"c","port":"out"},"target":{"node":"d3","port":"in"}},
 {"source":{"node":"d3","port":"out"},"target":{"node":"a","port":"in"}}]})";
 ElmaLogic::Runtime runtime;JsonDocument status;std::vector<std::pair<std::string,uint32_t>> samples;uint32_t now=0;
 assert(runtime.begin(loop,[&](JsonObjectConst n,JsonVariantConst,std::string&){samples.emplace_back(n["id"]|"",now);return true;},error));
 for(now=0;now<=60000;now+=100)runtime.tick(now,status.as<JsonVariantConst>());
 assert(samples.size()==13);for(size_t i=0;i<samples.size();++i){assert(samples[i].first==std::string(1,char('a'+i%3)));assert(samples[i].second==i*5000);}
 runtime.suspend();for(now=60100;now<80000;now+=100)runtime.tick(now,status.as<JsonVariantConst>());assert(samples.size()==13);
 std::cout<<"PASS: firmware validates only delayed loops; three samples repeat every 5 seconds for 60 seconds and stop cleanly\n";
}
