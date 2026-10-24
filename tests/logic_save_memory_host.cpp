#include "logic_validation.h"
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

class Budget final : public ArduinoJson::Allocator {
public:
    size_t used=0,peak=0,limit=32768;
    std::map<void*,size_t> blocks;
    void* allocate(size_t n) override {
        if(used+n>limit)return nullptr;
        void* p=std::malloc(n);if(p){blocks[p]=n;used+=n;peak=std::max(peak,used);}return p;
    }
    void deallocate(void* p) override {if(p){used-=blocks.at(p);blocks.erase(p);std::free(p);}}
    void* reallocate(void* p,size_t n) override {
        if(!p)return allocate(n);
        size_t old=blocks.at(p);if(used-old+n>limit)return nullptr;
        void* q=std::realloc(p,n);if(q){blocks.erase(p);blocks[q]=n;used=used-old+n;peak=std::max(peak,used);}return q;
    }
};
int main(){
    std::ifstream file("Firmware/tests/led_boolean_mix.graph.json");std::stringstream raw;raw<<file.rdbuf();
    JsonDocument input;assert(!deserializeJson(input,raw.str()));
    // Large editor-only metadata must not multiply during a save. Contracts
    // are rebuilt from trusted definitions, not supplied browser port data.
    JsonDocument devices;devices.set(input["devices"]);input.remove("devices");
    for(JsonObject node:input["nodes"].as<JsonArray>()){node.remove("ports");node.remove("binding");}
    for(JsonObject d:devices.as<JsonArray>()){
        d["help"]=std::string(40000,'x');
        for(JsonObject p:d["ports"].as<JsonArray>())p["help"]=std::string(40000,'y');
    }
    Budget budget;JsonDocument accepted(&budget);std::string error;
    if(!ElmaLogic::validateEditable(input,devices.as<JsonArrayConst>(),accepted,error)){std::cerr<<error<<" peak="<<budget.peak<<"\n";return 1;}
    assert(accepted["devices"].isNull());
    for(JsonObjectConst node:accepted["nodes"].as<JsonArrayConst>())for(JsonObjectConst p:node["ports"].as<JsonArrayConst>())assert(p["help"].isNull());
    assert(accepted["connections"].size()==input["connections"].size());
    std::cout<<"PASS: mixed LED graph validates under 32 KiB budget despite bulky editor metadata; peak="<<budget.peak<<"\n";
    accepted.clear();budget.limit=128;
    assert(!ElmaLogic::validateEditable(input,devices.as<JsonArrayConst>(),accepted,error));
    assert(!error.empty());
}
