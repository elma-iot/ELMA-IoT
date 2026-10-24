#include "logic_runtime.h"
#include "logic_validation.h"
#include <fstream>
#include <sstream>
#include <cassert>
#include <iostream>
int main(){
    std::ifstream file("Firmware/tests/led_boolean_mix.graph.json");std::stringstream raw;raw<<file.rdbuf();
    JsonDocument graph,accepted,status;assert(!deserializeJson(graph,raw.str()));std::string error;
    auto group=graph["groups"].to<JsonArray>().add<JsonObject>();group["id"]="test-group";group["name"]="Test";group["color"]="#335577";group["mode"]="playing";
    auto members=group["nodes"].to<JsonArray>();for(JsonObjectConst node:graph["nodes"].as<JsonArrayConst>())members.add(node["id"]);
    assert(ElmaLogic::validateEditable(graph,graph["devices"],accepted,error));
    auto groups=accepted["groups"].as<JsonArray>();assert(groups.size());std::string id=groups[0]["id"];
    unsigned calls=0;ElmaLogic::Runtime runtime;
    assert(runtime.begin(std::move(accepted),[&](JsonObjectConst,JsonVariantConst,std::string&){++calls;return true;},error));
    status["network"]["wifiConnected"]=true;status["network"]["apMode"]=true;
    runtime.tick(0,status);assert(calls>0);const auto before=calls;assert(runtime.hasRunningNodes());
    assert(runtime.controlGroup(id.c_str(),"stopped",20));assert(!runtime.hasRunningNodes());
    for(unsigned now=40;now<2000;now+=20)runtime.tick(now,status);
    assert(calls==before);JsonDocument live;runtime.telemetry(live.to<JsonObject>());
    assert(live["values"].isNull()); // no color/effect/source re-evaluation while stopped
    runtime.observe(status);live.clear();runtime.telemetry(live.to<JsonObject>());assert(live["values"].isNull());
    assert(runtime.controlGroup(id.c_str(),"playing",2000));runtime.tick(2020,status);assert(calls>before);
    auto outside=graph["nodes"].as<JsonArray>().add<JsonObject>();outside["id"]="outside";outside["type"]="value.number";outside["parameters"]["value"]=42;
    auto other=graph["groups"].as<JsonArray>().add<JsonObject>();other["id"]="other";other["name"]="Other";other["color"]="#335577";other["mode"]="playing";other["nodes"].to<JsonArray>().add("outside");
    JsonDocument compiledOther;assert(ElmaLogic::validateEditable(graph,graph["devices"],compiledOther,error));
    ElmaLogic::Runtime mixed;assert(mixed.begin(std::move(compiledOther),[](JsonObjectConst,JsonVariantConst,std::string&){return true;},error));
    mixed.tick(0,status);assert(mixed.controlGroup(id.c_str(),"stopped",20));assert(mixed.hasRunningNodes());mixed.tick(40,status);
    live.clear();mixed.telemetry(live.to<JsonObject>());assert(live["values"]["outside"]["value"]==42);assert(live["values"]["mixer"].isNull());
    assert(mixed.controlGroup("other","stopped",60));assert(!mixed.hasRunningNodes());
    std::cout<<"PASS stopped group releases evaluation data, performs no actions/effects, and restarts cleanly\n";
}
