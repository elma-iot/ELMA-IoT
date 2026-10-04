#include "logic_validation.h"
#include "logic_runtime.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>

int main(int argc,char** argv) {
    assert(argc==2);
    std::ifstream file(argv[1]);
    std::string raw((std::istreambuf_iterator<char>(file)),{}),error;
    JsonDocument graph,accepted;
    assert(!deserializeJson(graph,raw));
    for(JsonObject device:graph["devices"].as<JsonArray>())device["peripheral"]["id"]="live-array-identity";
    auto validate=[&](){return ElmaLogic::validateEditable(graph.as<JsonVariantConst>(),graph["devices"].as<JsonArrayConst>(),accepted,error);};
    if(!validate()){std::cerr<<error;return 1;}
    assert(!accepted.overflowed());
    assert(accepted["nodes"].size()==16);
    assert(accepted["groups"].size()==1); // Layout-only color is absent in packed input.
    ElmaLogic::Runtime runtime;
    assert(runtime.begin(std::move(accepted),[](JsonObjectConst,JsonVariantConst,std::string&){return true;},error));
    JsonDocument binding;binding["defaults"]["brightness"]=37;binding["defaults"]["effect"]="scan";
    runtime.setPeripheralBinding("live-array-identity",binding.as<JsonObjectConst>());
    bool rebound=false;
    for(JsonObjectConst node:runtime.nodes())if(node["peripheral"]["id"]=="live-array-identity"){
        assert(node["binding"]["defaults"]["brightness"]==37);
        assert(node["binding"]["defaults"]["effect"]=="scan");rebound=true;
    }
    assert(rebound); // Compact validation must retain identity for live changes.
    JsonDocument status;
    for(unsigned at=0;at<40000;at+=25)runtime.tick(at,status.as<JsonVariantConst>());
    graph["connections"][0]["target"]["port"]="unknown";
    assert(!validate());assert(error.find("Incompatible")!=std::string::npos);
    std::cout<<"PASS: packed groups, ownership transfer and connector validation\n";
}
