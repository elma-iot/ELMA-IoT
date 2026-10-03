#include "logic_validation.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <iostream>
int main(int argc,char**argv){assert(argc==2);std::ifstream file(argv[1]);std::string raw((std::istreambuf_iterator<char>(file)),{}),error;JsonDocument graph,out,reloaded;assert(!deserializeJson(graph,raw));
auto check=[&](){return ElmaLogic::validateEditable(graph.as<JsonVariantConst>(),graph["devices"].as<JsonArrayConst>(),out,error);};
assert(check());assert(out["connections"][0]["routingPoints"]==graph["connections"][0]["routingPoints"]);
std::string stored;serializeJson(out,stored);assert(!deserializeJson(reloaded,stored));assert(reloaded["connections"][0]["routingPoints"]==graph["connections"][0]["routingPoints"]);
auto p=graph["connections"][0]["routingPoints"][0];p["controlIn"]["x"]="bad";assert(!check());p["controlIn"]["x"]=-70;p.remove("controlOut");assert(!check());p["controlOut"]["x"]=80;p["controlOut"]["y"]=-10;assert(check());
auto points=graph["connections"][0]["routingPoints"].as<JsonArray>();for(int i=1;i<65;++i){auto next=points.add<JsonObject>();next["x"]=i;next["y"]=i;}assert(!check());std::cout<<"PASS: device validates and retains routing metadata through persistence serialization\n";}
