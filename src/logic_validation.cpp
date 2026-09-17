#include "logic_validation.h"
#include "logic_catalog.h"
#include <cmath>
#include <vector>
#include <functional>
#include <cctype>

namespace ElmaLogic {
namespace {
bool compatible(const char* a,const char* b) {return std::string(a)==b || std::string(b)=="number" && (std::string(a)=="analog" || std::string(a)=="integer") || std::string(a)=="path" && std::string(b)=="audio";}
JsonObjectConst port(JsonObjectConst node,const char* name,const char* direction) {for(JsonObjectConst p:node["ports"].as<JsonArrayConst>())if(p["id"]==name&&p["direction"]==direction)return p;return {};}
}
bool validateEditable(JsonVariantConst input,JsonArrayConst devices,JsonDocument& output,std::string& error) {
    auto fail=[&](const char* message){error=message;return false;};
    if(input["schemaVersion"]!=1 || !input["nodes"].is<JsonArrayConst>() || !input["connections"].is<JsonArrayConst>() || input["nodes"].size()>64 || input["connections"].size()>128 || measureJson(input)>32768)return fail("Invalid graph or graph exceeds 64 nodes / 128 links / 32 KiB");
    output.clear();output["schemaVersion"]=1;output["devices"].set(devices);output["view"].set(input["view"]);
    auto nodes=output["nodes"].to<JsonArray>();auto links=output["connections"].to<JsonArray>();
    for(JsonObjectConst n:input["nodes"].as<JsonArrayConst>()) {
        const char* id=n["id"]|"";const char* type=n["type"]|"";
        if(!*id || strlen(id)>64 || !n["parameters"].is<JsonObjectConst>())return fail("Missing node identity/parameters");
        for(JsonObjectConst old:nodes)if(old["id"]==id)return fail("Duplicate node ID");
        JsonDocument definition,filter;filter[type]=true;
        deserializeJson(definition,ELMA_LOGIC_CATALOG,DeserializationOption::Filter(filter));
        JsonObjectConst spec=definition[type].as<JsonObjectConst>();
        if(std::string(type).find("peripheral.")==0) {
            for(JsonObjectConst d:devices)if(d["type"]==type && d["peripheral"]["id"]==n["peripheral"]["id"]) {spec=d;break;}
        }
        if(spec.isNull())return fail("Unsupported node or peripheral reference; recompile for changed hardware");
#ifdef APP_DISABLE_AUDIO
        if(std::string(type).find("audio.")==0 && std::string(type)!="audio.piano")return fail("DAC audio is unavailable on this build; piano melodies remain available for buzzers");
#endif
        auto target=nodes.add<JsonObject>();target["id"]=id;target["type"]=type;target["name"]=n["name"]|spec["title"]|type;
        target["ports"].set(spec["ports"]);target["binding"].set(spec["binding"]);target["peripheral"].set(spec["peripheral"]);
        auto position=target["position"].to<JsonObject>();
        for(const char* axis:{"x","y"}) {double v=n["position"][axis]|0.0;if(!std::isfinite(v)||std::abs(v)>50000)return fail("Invalid node position");position[axis]=v;}
        target["parameters"].set(spec["parameters"]);
        for(JsonPairConst p:n["parameters"].as<JsonObjectConst>()) {
            auto expected=spec["parameters"][p.key().c_str()];auto value=p.value();
            if(expected.isNull())return fail("Unknown node parameter");
            if(expected.is<bool>()?!value.is<bool>():expected.is<double>()?!value.is<double>():expected.is<const char*>()?!value.is<const char*>():expected.is<JsonArrayConst>()?!value.is<JsonArrayConst>():false)return fail("Incorrect parameter type");
            if(value.is<double>()&&!std::isfinite(value.as<double>()))return fail("Nonfinite parameter");
            target["parameters"][p.key()].set(value);
        }
    }
    std::vector<std::vector<size_t>> adjacency(nodes.size());std::vector<bool> root(nodes.size(),false),reached(nodes.size(),false);
    auto index=[&](const char* id){for(size_t i=0;i<nodes.size();++i)if(nodes[i]["id"]==id)return int(i);return -1;};
    for(JsonObjectConst e:input["connections"].as<JsonArrayConst>()) {
        int a=index(e["source"]["node"]|""),b=index(e["target"]["node"]|"");
        if(a<0||b<0||a==b)return fail("Invalid connection endpoints");
        auto ap=port(nodes[a],e["source"]["port"]|"","output"),bp=port(nodes[b],e["target"]["port"]|"","input");
        if(ap.isNull()||bp.isNull()||!compatible(ap["type"]|"",bp["type"]|""))return fail("Incompatible connector types");
        for(JsonObjectConst old:links)if(old["target"]["node"]==e["target"]["node"]&&old["target"]["port"]==e["target"]["port"])return fail("Input has multiple connections");
        links.add(e);adjacency[a].push_back(size_t(b));
    }
    auto linked=[&](const char* id,const char* p){for(JsonObjectConst e:links)if(e["target"]["node"]==id&&e["target"]["port"]==p)return true;return false;};
    std::vector<int> mark(nodes.size());std::function<bool(size_t)> visit=[&](size_t n){if(mark[n]==1)return false;if(mark[n]==2)return true;mark[n]=1;for(auto next:adjacency[n])if(!visit(next))return false;mark[n]=2;return true;};
    for(size_t i=0;i<nodes.size();++i) {
        if(!visit(i))return fail("Circular dependency; use Repeat instead");
        auto n=nodes[i];const char* id=n["id"]|"";std::string type=n["type"]|"";
        for(JsonObjectConst p:n["ports"].as<JsonArrayConst>())if(p["direction"]=="input" && p["required"].as<bool>()&&!linked(id,p["id"]|""))return fail("Required input is disconnected");
        if(type.find("timing.")==0 && !linked(id,"seconds")) {double seconds=n["parameters"]["seconds"]|0.0;if(seconds<=0||seconds>86400)return fail("Interval must be >0 and <=86400 seconds");}
        if(type=="timing.repeat"&&!linked(id,"count")){double count=n["parameters"]["count"]|0.0;if(count<1||count>128||count!=std::floor(count))return fail("Repeat count must be a whole number 1-128");}
        if(type=="condition.between"&&!linked(id,"minimum")&&!linked(id,"maximum")&&n["parameters"]["minimum"].as<double>()>n["parameters"]["maximum"].as<double>())return fail("Minimum exceeds maximum");
        if(type=="audio.tts") {
            if(n["parameters"]["language"]!="en")return fail("Offline speech supports basic English");
            for(const char* key:{"speechRate","voicePitch","intonation"})if(!n["parameters"][key].isNull()) {
                double v=n["parameters"][key].as<double>();
                double lo=std::string(key)=="speechRate"?.5:std::string(key)=="voicePitch"?80:0;
                double hi=std::string(key)=="speechRate"?1.5:std::string(key)=="voicePitch"?220:1;
                if(!n["parameters"][key].is<double>()||!std::isfinite(v)||v<lo||v>hi)return fail("Invalid offline voice setting");
            }
            if(!linked(id,"text")){const char* text=n["parameters"]["text"]|"";if(strlen(text)>256)return fail("Speech text exceeds 256 characters");for(const unsigned char* c=(const unsigned char*)text;*c;++c)if(*c>126||*c<32)return fail("Offline speech requires printable English text");}
        }
        if(type=="audio.volume"&&!linked(id,"volume")){double v=n["parameters"]["volume"]|0.0;if(v<0||v>100)return fail("Volume must be 0-100");}
        if(type=="audio.speed"&&!linked(id,"speed")){double v=n["parameters"]["speed"]|1.0;if(v<0.25||v>4)return fail("Speed must be 0.25-4");}
        if(type=="audio.pitch"&&!linked(id,"semitones")){double v=n["parameters"]["semitones"]|0.0;if(v< -24||v>24)return fail("Pitch must be -24 to 24 semitones");}
        if(type=="audio.equalizer")for(const char* band:{"lowDb","presenceDb","highDb"})if(!linked(id,band)){double v=n["parameters"][band]|0.0;if(v< -6||v>6)return fail("Equalizer gain must be -6 to 6 dB");}
        if(type=="audio.piano") {
            auto notes=n["parameters"]["notes"].as<JsonArrayConst>();if(notes.size()>128)return fail("Piano supports at most 128 notes");
            std::string instrument=n["parameters"]["instrument"]|"piano";if(instrument!="piano"&&instrument!="bell"&&instrument!="guitar"&&instrument!="organ")return fail("Unsupported piano instrument");
            for(JsonObjectConst note:notes){double pitch=note["note"]| -1.0,start=note["start"]| -1.0,duration=note["duration"]| -1.0,velocity=note["velocity"]|0.75;if(!std::isfinite(pitch)||!std::isfinite(start)||!std::isfinite(duration)||!std::isfinite(velocity)||pitch<12||pitch>108||pitch!=std::floor(pitch)||start<0||duration<=0||start+duration>60||velocity<=0||velocity>1)return fail("Invalid piano note; melodies are limited to 60 seconds");}
        }
        if(type.find("action.")==0) {
            JsonObjectConst device;
            for(JsonObjectConst e:links)if(e["target"]["node"]==id&&e["target"]["port"]=="device")device=nodes[index(e["source"]["node"]|"")].as<JsonObjectConst>();
            bool supported=false;std::string capability="peripheral."+type.substr(7);
            for(JsonObjectConst d:devices)if(d["type"]==capability&&d["peripheral"]["id"]==device["peripheral"]["id"])supported=true;
            if(device["type"]!="peripheral.reference"||!supported)return fail("Action is unsupported for this peripheral reference");
        }
        root[i]=type.find("event.")==0 || n["binding"]["kind"]=="transition" || n["binding"]["kind"]=="lifecycle" || type=="peripheral.rising" || type=="peripheral.falling" || type=="condition.if"&&!linked(id,"in") || type=="timing.timer"&&linked(id,"enabled");
    }
    std::function<void(size_t)> reach=[&](size_t i){if(reached[i])return;reached[i]=true;for(auto next:adjacency[i])reach(next);};
    for(size_t i=0;i<nodes.size();++i)if(root[i])reach(i);
    for(size_t i=0;i<nodes.size();++i)for(JsonObjectConst p:nodes[i]["ports"].as<JsonArrayConst>())if(p["direction"]=="input"&&p["type"]=="execution"&&!reached[i])return fail("Execution path has no event/start source");
    auto groups=output["groups"].to<JsonArray>();std::vector<int> owners(nodes.size(),-1);
    if(input["groups"].size()>32)return fail("At most 32 groups are supported");
    for(JsonObjectConst g:input["groups"].as<JsonArrayConst>()) {
        const char* id=g["id"]|"";const char* mode=g["mode"]|"playing";const char* color=g["color"]|"";
        if(!*id||strlen(id)>64||strlen(color)!=7||color[0]!='#'||!g["nodes"].is<JsonArrayConst>() || (std::string(mode)!="playing"&&std::string(mode)!="paused"&&std::string(mode)!="stopped"))return fail("Invalid automation group");
        for(const char* c=color+1;*c;++c)if(!isxdigit(*c))return fail("Invalid group color");
        for(JsonObjectConst old:groups)if(old["id"]==id)return fail("Duplicate group ID");
        for(JsonVariantConst member:g["nodes"].as<JsonArrayConst>()){int n=index(member|"");if(n<0||owners[n]>=0)return fail("Group members must exist and belong to one group");owners[n]=int(groups.size());}
        auto target=groups.add<JsonObject>();target["id"]=id;target["name"]=g["name"]|"Automation";target["color"]=color;target["nodes"].set(g["nodes"]);target["mode"]=mode;
        if(!g["rect"].isNull()) {
            for(const char* key:{"x","y","width","height"}){auto value=g["rect"][key];if(!value.is<double>()||!std::isfinite(value.as<double>())||std::abs(value.as<double>())>50000)return fail("Invalid group bounds");}
            if(g["rect"]["width"].as<double>()<240||g["rect"]["height"].as<double>()<100)return fail("Group bounds too small");
            target["rect"].set(g["rect"]);
        }
    }
    if(measureJson(output)>32768 || output.overflowed())return fail("Validated graph exceeds runtime storage limit");
    return true;
}
}
