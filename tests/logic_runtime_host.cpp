#include "logic_runtime.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>

int main(int argc,char** argv) {
    assert(argc==2);std::ifstream file(argv[1]);std::string program((std::istreambuf_iterator<char>(file)),{});
    ElmaLogic::Runtime runtime;std::string error;std::vector<uint32_t> calls;uint32_t now=0;
    assert(runtime.begin(program.c_str(),[&](JsonObjectConst node,JsonVariantConst args,std::string&){
        assert(node["type"]=="peripheral.play");assert(args["source"]["text"]=="ESP temperature above 50 degrees");
        assert(args["source"]["language"]=="en");calls.push_back(now);return true;
    },error));
    auto tick=[&](uint32_t t,double temp,bool available=true){JsonDocument status;status["system"]["chipTemperatureAvailable"]=available;status["system"]["chipTemperatureC"]=temp;now=t;runtime.tick(t,status.as<JsonVariantConst>());};
    tick(0,40);tick(100,51);assert(calls==std::vector<uint32_t>{100});
    tick(5099,55);assert(calls.size()==1);tick(5100,55);tick(10100,55);tick(20100,55);
    assert((calls==std::vector<uint32_t>{100,5100,10100}));
    tick(20200,40);tick(20300,55);tick(25300,55);tick(30300,55);assert(calls.size()==6);
    tick(30400,0,false);tick(30500,55);assert(calls.size()==6); // Missing readings cannot make a false threshold edge.
    tick(30600,40);tick(30700,55);assert(calls.size()==7);runtime.suspend();tick(40000,55);assert(calls.size()==7);
    // Same engine, wraparound clock: timers still fire once at each due time.
    calls.clear();assert(runtime.begin(program.c_str(),[&](JsonObjectConst,JsonVariantConst,std::string&){calls.push_back(now);return true;},error));
    tick(0xfffff000u,40);tick(0xfffff100u,55);tick(uint32_t(0xfffff100u+4999u),55);assert(calls.size()==1);
    tick(uint32_t(0xfffff100u+5000u),55);tick(uint32_t(0xfffff100u+10000u),55);assert(calls.size()==3);
    assert(runtime.error().empty());
    std::cout<<"PASS: real C++ graph execution, TTS descriptor, three 5-second pulses, rearm, unavailable readings, OTA cancellation, millis wrap\n";
}
