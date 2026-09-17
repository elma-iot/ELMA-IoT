#include "logic_runtime.h"
#include "logic_validation.h"
#include <iostream>
#include <stdexcept>
#define CHECK(x) if(!(x))throw std::runtime_error(#x)
int main(){try{
 const char* source=R"json({"schemaVersion":1,"nodes":[{"id":"a","type":"event.start","parameters":{},"ports":[{"id":"out","direction":"output","type":"execution"}]},{"id":"ar","type":"timing.repeat","parameters":{"seconds":5,"count":3},"ports":[]},{"id":"ax","type":"mainboard.mqtt.connect","parameters":{},"ports":[]},{"id":"b","type":"event.start","parameters":{},"ports":[]},{"id":"br","type":"timing.repeat","parameters":{"seconds":5,"count":3},"ports":[]},{"id":"bx","type":"mainboard.mqtt.connect","parameters":{},"ports":[]}],"connections":[{"source":{"node":"a","port":"out"},"target":{"node":"ar","port":"in"}},{"source":{"node":"ar","port":"out"},"target":{"node":"ax","port":"in"}},{"source":{"node":"b","port":"out"},"target":{"node":"br","port":"in"}},{"source":{"node":"br","port":"out"},"target":{"node":"bx","port":"in"}}],"groups":[{"id":"A","name":"A","nodes":["a","ar","ax"],"color":"#438deb","mode":"playing","rect":{"x":0,"y":0,"width":900,"height":300}},{"id":"B","name":"B","nodes":["b","br","bx"],"color":"#24bc73","mode":"playing"}]})json";
 ElmaLogic::Runtime r;std::string error;int a=0,b=0;JsonDocument status;
 CHECK(r.begin(source,[&](JsonObjectConst n,JsonVariantConst,std::string&){if(n["id"]=="ax")++a;else if(n["id"]=="bx")++b;return true;},error));
 auto tick=[&](uint32_t t){r.tick(t,status.as<JsonVariantConst>());};
 tick(0);CHECK(a==1&&b==1);CHECK(r.controlGroup("A","paused",1000));tick(5000);CHECK(a==1&&b==2);
 CHECK(r.controlGroup("A","playing",6000));tick(9999);CHECK(a==1&&b==2);tick(10000);CHECK(a==2&&b==3);
 CHECK(r.controlGroup("A","stopped",10100));tick(20000);CHECK(a==2&&b==3);CHECK(r.controlGroup("A","playing",20100));tick(20100);CHECK(a==3&&b==3);
 r.pause(21100);CHECK(r.controlGroup("A","paused",22000));r.resume(24000);tick(30000);CHECK(a==3);CHECK(r.controlGroup("A","playing",31000));tick(34999);CHECK(a==3);tick(35000);CHECK(a==4);
 CHECK(!r.controlGroup("missing","playing",35000));CHECK(!r.controlGroup("A","bad",35000));
 JsonDocument telemetry;r.telemetry(telemetry.to<JsonObject>());std::string serialized;serializeJson(telemetry,serialized);JsonDocument parsed;CHECK(!deserializeJson(parsed,serialized));
 CHECK(parsed["activity"].as<JsonObjectConst>().size()==r.nodes().size());for(JsonObjectConst node:r.nodes())CHECK(parsed["activity"].containsKey(node["id"].as<std::string>()));
 ElmaLogic::Runtime uuidRuntime;CHECK(uuidRuntime.begin(R"json({"schemaVersion":1,"nodes":[{"id":"123e4567-e89b-12d3-a456-426614174000","type":"event.start","parameters":{},"ports":[]}],"connections":[]})json",{},error));uuidRuntime.tick(0,status.as<JsonVariantConst>());
 JsonDocument uuidTelemetry;uuidRuntime.telemetry(uuidTelemetry.to<JsonObject>());serialized.clear();serializeJson(uuidTelemetry,serialized);parsed.clear();CHECK(!deserializeJson(parsed,serialized));CHECK(parsed["activity"].containsKey("123e4567-e89b-12d3-a456-426614174000"));
 JsonDocument input,accepted;CHECK(!deserializeJson(input,source));CHECK(ElmaLogic::validateEditable(input.as<JsonVariantConst>(),JsonArrayConst{},accepted,error));CHECK(accepted["groups"][0]["rect"]["width"]==900);
 input["groups"][0]["rect"]["width"]=10;CHECK(!ElmaLogic::validateEditable(input.as<JsonVariantConst>(),JsonArrayConst{},accepted,error));input["groups"][0]["rect"]["width"]=900;
 input["groups"][1]["nodes"].as<JsonArray>().add("a");CHECK(!ElmaLogic::validateEditable(input.as<JsonVariantConst>(),JsonArrayConst{},accepted,error));
 JsonDocument compact,rebuilt;CHECK(!deserializeJson(compact,source));compact.remove("devices");for(JsonObject n:compact["nodes"].as<JsonArray>()){n.remove("ports");n.remove("binding");}CHECK(ElmaLogic::validateEditable(compact.as<JsonVariantConst>(),JsonArrayConst{},rebuilt,error));CHECK(rebuilt["nodes"][0]["ports"].size()>0);CHECK(rebuilt["nodes"][2]["binding"]["body"]["action"]=="connect");
 ElmaLogic::Runtime voice;JsonDocument spoken;CHECK(voice.begin(R"json({"schemaVersion":1,"nodes":[{"id":"start","type":"event.start","parameters":{},"ports":[]},{"id":"play","type":"peripheral.play","parameters":{},"ports":[]},{"id":"voice","type":"audio.tts","parameters":{"text":"Temperature above 60 degrees","language":"en","speechRate":0.7,"voicePitch":110,"intonation":0.8},"ports":[{"id":"source","direction":"output","type":"audio"}]}],"connections":[{"source":{"node":"start","port":"out"},"target":{"node":"play","port":"in"}},{"source":{"node":"voice","port":"source"},"target":{"node":"play","port":"source"}}]})json",[&](JsonObjectConst,JsonVariantConst args,std::string&){spoken.set(args);return true;},error));voice.tick(0,status.as<JsonVariantConst>());std::string voiceJson;serializeJson(spoken,voiceJson);std::cout<<voiceJson<<"\n";CHECK(std::abs(spoken["source"]["speechRate"].as<double>()-0.7)<0.00001);CHECK(spoken["source"]["voicePitch"]==110);CHECK(std::abs(spoken["source"]["intonation"].as<double>()-0.8)<0.00001);

 std::cout<<"PASS: independent groups, pause timing, stop/restart, combined master/group pause, canonical validation and saved bounds\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
