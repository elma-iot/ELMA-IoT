#include "logic_request_guard.h"
#include "segmented_buffer.h"
#include <cassert>
#include <iostream>
#include <string>
static size_t outstanding=0,allocations=0,failAfter=999999;
static void* limited(size_t n){assert(n<600);if(allocations++>=failAfter)return nullptr;auto p=std::malloc(n);if(p)++outstanding;return p;}
static void release(void* p){if(p)--outstanding;std::free(p);}
int main(){
    LogicRequestGuard guard;JsonDocument command;
    assert(guard.check(command)==LogicRequestGuard::Legacy);
    command["request"]["client"]="browser-1";command["request"]["sequence"]=1;command["mode"]="playing";
    assert(guard.check(command)==LogicRequestGuard::Fresh);guard.remember(command);assert(guard.check(command)==LogicRequestGuard::Replayed);
    command["mode"]="stopped";assert(guard.check(command)==LogicRequestGuard::Conflict);
    command["request"]["sequence"]=2;assert(guard.check(command)==LogicRequestGuard::Fresh);guard.remember(command);
    command["request"]["sequence"]=1;assert(guard.check(command)==LogicRequestGuard::Replayed); // stale save cannot overwrite a newer edit
    command["request"]["client"]="browser-2";assert(guard.check(command)==LogicRequestGuard::Fresh);
    command["request"]["sequence"]=0;assert(guard.check(command)==LogicRequestGuard::Invalid);
    command["request"]["client"]=std::string(41,'x');command["request"]["sequence"]=1;assert(guard.check(command)==LogicRequestGuard::Invalid);
    const std::string source(30000,'z');
    {SegmentedBuffer bytes(limited,release);assert(bytes.write(reinterpret_cast<const uint8_t*>(source.data()),source.size())==source.size());std::string out;
     while(bytes.available()){auto n=std::min(size_t(137),bytes.available());out.append(bytes.data(),n);bytes.consume(n);}assert(out==source);assert(outstanding==0);}
    allocations=0;failAfter=2;
    {SegmentedBuffer bytes(limited,release);assert(bytes.write(reinterpret_cast<const uint8_t*>(source.data()),source.size())<source.size());}
    assert(outstanding==0);
    std::cout<<"PASS retry/stale-request guard and 30 KiB response in sub-600-byte allocations, including failure cleanup\n";
}
