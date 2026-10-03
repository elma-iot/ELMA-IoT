#include "logic_runtime.h"
#include <cassert>
#include <iostream>

int main() {
    const char* program=R"json({"schemaVersion":1,"nodes":[
      {"id":"value","type":"value.number","parameters":{"value":42},"ports":[]},
      {"id":"interval","type":"recording.interval","parameters":{"duration":5,"timeUnit":"seconds"},"ports":[]},
      {"id":"plot","type":"mainboard.plot","binding":{"kind":"action"},"parameters":{"plot":"Signal","series":"Value","unit":"dBm"},"ports":[]},
      {"id":"led","type":"hardware.led","binding":{"kind":"led","pin":8,"ledType":"regular"},"parameters":{"red":255,"green":255,"blue":255,"brightness":100},"ports":[{"id":"toggle"},{"id":"red"},{"id":"out"}]}
    ],"devices":[{"type":"hardware.led","binding":{"pin":8,"ledType":"regular"},"ports":[{"id":"toggle"},{"id":"red"}]}],"connections":[
      {"source":{"node":"value","port":"value"},"target":{"node":"interval","port":"value"}},
      {"source":{"node":"interval","port":"out"},"target":{"node":"plot","port":"value"}},
      {"source":{"node":"plot","port":"out"},"target":{"node":"led","port":"toggle"}}
    ],"groups":[{"id":"automation","name":"Automation","color":"#438deb","mode":"playing","nodes":["value","interval","plot","led"]}]})json";
    ElmaLogic::Runtime runtime;std::string error;int samples=0,toggles=0,lastPin=-1;
    assert(runtime.begin(program,[&](JsonObjectConst node,JsonVariantConst args,std::string&){
        if(node["type"]=="mainboard.plot") {assert(args["value"]==42);++samples;}
        else {assert(node["type"]=="hardware.led");assert(args["action"]=="toggle");lastPin=node["binding"]["pin"];++toggles;}
        return true;
    },error));
    JsonDocument status,binding;binding["kind"]="led";binding["pin"]=48;binding["ledType"]="neopixel";
    auto tick=[&](uint32_t now){runtime.tick(now,status.as<JsonVariantConst>());};
    tick(0);tick(2000);runtime.setLedBinding(binding.as<JsonObjectConst>());
    assert(runtime.graph()["devices"][0]["binding"]["pin"]==48);
    assert(runtime.graph()["nodes"][3]["ports"][1]["enabled"]==true);
    tick(4999);assert(toggles==0);tick(5000);assert(samples==1&&toggles==1&&lastPin==48);
    binding["pin"]=21;binding["ledType"]="regular";runtime.setLedBinding(binding.as<JsonObjectConst>());
    assert(runtime.graph()["nodes"][3]["ports"][1]["enabled"]==false);
    tick(9999);assert(toggles==1);tick(10000);assert(samples==2&&toggles==2&&lastPin==21);
    binding["pin"]=-1;runtime.setLedBinding(binding.as<JsonObjectConst>());
    for(JsonObjectConst port:runtime.graph()["nodes"][3]["ports"].as<JsonArrayConst>())assert(port["enabled"]==false);
    assert(runtime.error().empty());
    std::cout<<"PASS: grouped autostart, plot sampling toggles, live LED rebinding and timer continuity\n";
}
