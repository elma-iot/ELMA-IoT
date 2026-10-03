#include "logic_validation.h"
#include <fstream>
#include <iterator>
#include <cassert>
#include <iostream>
int main(int argc,char** argv){
 assert(argc==2);std::ifstream file(argv[1]);std::string payload((std::istreambuf_iterator<char>(file)),{}),error;JsonDocument graph,output;
 assert(!deserializeJson(graph,payload));
 auto check=[&](){return ElmaLogic::validateEditable(graph.as<JsonVariantConst>(),graph["devices"].as<JsonArrayConst>(),output,error);};
 assert(check());JsonObject sleep,wake;
 for(JsonObject n:graph["nodes"].as<JsonArray>()){if(n["type"]=="hardware.sleep")sleep=n;if(n["type"]=="hardware.wake_gpio")wake=n;}
 sleep["parameters"]["mode"]="deep";assert(check());
 wake["parameters"]["pin"]=48;wake["binding"]["allowedPins"]["48"]["deep"]=true;assert(!check()); // caller cannot forge hardware permission
 wake["parameters"]["pin"]=1;sleep["parameters"]["mode"]="hibernate";assert(!check());
 std::cout<<"PASS: live graph validation and canonical sleep capability enforcement\n";
}
