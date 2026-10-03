#include "logic_runtime.h"
#include <cassert>
#include <iostream>

void conversion(const char* type,const char* json,bool valid,double expected=0) {
 JsonDocument graph,value,status;deserializeJson(value,json);graph["schemaVersion"]=1;
 auto nodes=graph["nodes"].to<JsonArray>();
 auto start=nodes.add<JsonObject>();start["id"]="start";start["type"]="event.start";
 auto source=nodes.add<JsonObject>();source["id"]="source";source["type"]="value.constant";source["parameters"]["value"].set(value);
 auto convert=nodes.add<JsonObject>();convert["id"]="convert";convert["type"]=type;
 auto led=nodes.add<JsonObject>();led["id"]="led";led["type"]="hardware.led";
 auto p=led["ports"].to<JsonArray>().add<JsonObject>();p["id"]="brightness";p["direction"]="input";p["type"]="number";
 auto edges=graph["connections"].to<JsonArray>();
 auto wire=[&](const char* a,const char* ap,const char* b,const char* bp){auto e=edges.add<JsonObject>();e["source"]["node"]=a;e["source"]["port"]=ap;e["target"]["node"]=b;e["target"]["port"]=bp;};
 if(std::string(type).find("bridge.")==0){convert["parameters"]["value"].set(value);wire("start","out","convert","in");wire("convert","out","led","on");}
 else{wire("source","value","convert","input");wire("start","out","led","on");}
 wire("convert","value","led","brightness");
 std::string payload,error;serializeJson(graph,payload);int calls=0;
 ElmaLogic::Runtime runtime;
 assert(runtime.begin(payload.c_str(),[&](JsonObjectConst,JsonVariantConst args,std::string&){++calls;assert(args["brightness"].as<double>()==expected);return true;},error));
 runtime.tick(0,status.as<JsonVariantConst>());runtime.tick(1,status.as<JsonVariantConst>());
 assert(calls==(valid?1:0));assert(valid?runtime.error().empty():!runtime.error().empty());
}
int main(){
 conversion("bridge.number","20",true,20);
 conversion("convert.boolean_number","true",true,1);
 conversion("convert.boolean_integer","false",true,0);
 conversion("convert.number_integer","-12.8",true,-12);
 conversion("convert.number_integer","2147483648",false);
 conversion("convert.text_number","\" 25.5 \"",true,25.5);
 conversion("convert.text_number","\"25oops\"",false);
 conversion("convert.text_number","\"\"",false);
 conversion("convert.text_number","\"nan\"",false);
 conversion("convert.measurement_number","true",true,1);
 std::cout<<"PASS: event forwarding, conversion values and invalid-input action suppression\n";
}
