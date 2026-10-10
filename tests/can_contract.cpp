#include "can_contract.h"
#include <cassert>
#include <iostream>
int main(){
 JsonDocument p;deserializeJson(p,R"({"identifier":2047,"extended":false,"remote":false,"data":"00 FF 1a","length":0})");
 p["pin"]="15";assert(CanContract::pin(p["pin"])==15);p["pin"]=16;assert(CanContract::pin(p["pin"])==16);
 for(const char* text:{"","GPIO15","15x","99999999999999999999999"}){p["pin"]=text;assert(CanContract::pin(p["pin"])==-1);}
 assert(CanContract::valid("hardware.can.send",p));uint8_t bytes[8],length=0;assert(CanContract::bytes(p["data"],bytes,length)&&length==3&&bytes[1]==255&&bytes[2]==26);
 p["identifier"]=2048;assert(!CanContract::valid("hardware.can.send",p));p["extended"]=true;assert(CanContract::valid("hardware.can.send",p));
 p["identifier"]=536870912;assert(!CanContract::valid("hardware.can.send",p));p["identifier"]=1;
 for(const char* text:{"0","GG","00FF","00 01 02 03 04 05 06 07 08"}){p["data"]=text;assert(!CanContract::valid("hardware.can.send",p));}
 p["data"]="";p["remote"]=true;p["length"]=8;assert(CanContract::valid("hardware.can.send",p));p["length"]=9;assert(!CanContract::valid("hardware.can.send",p));
 p["bitrate"]=500000;p["listenOnly"]=true;assert(CanContract::valid("hardware.can.configure",p));p["bitrate"]=123456;assert(!CanContract::valid("hardware.can.configure",p));
 std::cout<<"CAN identifiers, data bytes, remote frames and bitrate validation passed\n";
}
