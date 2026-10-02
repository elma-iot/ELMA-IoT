#include "logic_validation.h"
#include "logic_catalog.h"
#include <set>
#include <algorithm>
#include <cmath>
#include <vector>
#include <functional>
#include <cctype>

bool validateLogicHardware(JsonObject n,std::string& error) {
    const std::string type=n["type"]|"";
    if(type!="hardware.gpio"&&type!="hardware.led")return true;
    std::set<std::string> enabled;
    if(type=="hardware.gpio") {
        if(!n["parameters"]["pin"].is<int>()){error="GPIO must be an integer";return false;}
        int pin=n["parameters"]["pin"];
        auto caps=n["binding"]["allowedPins"][std::to_string(pin)];
        std::string mode=n["parameters"]["mode"]|"";
        bool valid=(mode=="input"&&caps["input"]==true)||((mode=="input_pullup"||mode=="input_pulldown")&&caps["pull"]==true)||((mode=="output"||mode=="pwm")&&caps["output"]==true)||(mode=="analog"&&caps["analog"]==true);
        if(mode=="pwm" && n["binding"]["pwmAllowed"]==false)valid=false;
        if(!valid){error="GPIO or mode is unavailable; recompile for changed hardware";return false;}
        double duty=n["parameters"]["duty"]|-1.0,frequency=n["parameters"]["frequency"]|0.0;
        if(!std::isfinite(duty)||duty<0||duty>100||!std::isfinite(frequency)||frequency<100||frequency>20000){error="Invalid GPIO PWM parameters";return false;}
        enabled={"digital"};
        if(mode=="output"||mode=="pwm")enabled.insert("out");
        if(mode=="output")enabled.insert({"toggle","write","state"});
        if(mode=="pwm")enabled.insert({"pwm","duty"});
        if(mode=="analog")enabled.insert({"analog","millivolts"});
    } else {
        if((n["binding"]["pin"]|-1)<0){error="Built-in LED is unavailable";return false;}
        enabled={"on","off","toggle","write","release","out","digital"};
        if(n["binding"]["ledType"]!="regular")enabled.insert({"red","green","blue","brightness"});
        for(const char* key:{"red","green","blue","brightness"}) {
            double value=n["parameters"][key]|-1.0;
            if(!std::isfinite(value)||value<0||value>(std::string(key)=="brightness"?100:255)){error="Invalid LED color/brightness";return false;}
        }
    }
    for(JsonObject p:n["ports"].as<JsonArray>())p["enabled"]=enabled.count(p["id"]|"")!=0;
    return true;
}


namespace ElmaLogic {
namespace {
bool compatible(const char* a,const char* b) {return std::string(b)=="measurement"&&(std::string(a)=="number"||std::string(a)=="integer"||std::string(a)=="analog"||std::string(a)=="boolean") || std::string(b)=="scalar"&&(std::string(a)=="number"||std::string(a)=="integer"||std::string(a)=="analog"||std::string(a)=="boolean"||std::string(a)=="string") || std::string(a)==b || std::string(b)=="number" && (std::string(a)=="analog" || std::string(a)=="integer") || std::string(a)=="path" && std::string(b)=="audio";}
JsonObjectConst port(JsonObjectConst node,const char* name,const char* direction) {for(JsonObjectConst p:node["ports"].as<JsonArrayConst>())if(p["id"]==name&&p["direction"]==direction)return p;return {};}
}
bool validateEditable(JsonVariantConst input,JsonArrayConst devices,JsonDocument& output,std::string& error) {
    auto fail=[&](const char* message){error=message;return false;};
    if(input["schemaVersion"]!=1 || !input["nodes"].is<JsonArrayConst>() || !input["connections"].is<JsonArrayConst>() || input["nodes"].size()>64 || input["connections"].size()>128 || measureJson(input)>32768)return fail("Invalid graph or graph exceeds 64 nodes / 128 links / 32 KiB");
    output.clear();output["schemaVersion"]=1;output["devices"].set(devices);output["view"].set(input["view"]);
    if(input["view"]["labels"].size()>64)return fail("At most 64 canvas labels are supported");
    std::vector<std::string> labelIds;
    for(JsonObjectConst label:input["view"]["labels"].as<JsonArrayConst>()){
        const char* id=label["id"]|"";const char* text=label["text"]|"";
        if(!*id||strlen(id)>64||strlen(text)>256||std::find(labelIds.begin(),labelIds.end(),id)!=labelIds.end())return fail("Invalid canvas label");
        for(const char* axis:{"x","y"}){auto value=label[axis];if(!(value.is<double>()||value.is<int>())||!std::isfinite(value.as<double>())||std::abs(value.as<double>())>50000)return fail("Invalid canvas label position");}
        labelIds.emplace_back(id);
    }
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
        if(std::string(type).find("hardware.")==0) {
            spec=JsonObjectConst();
            for(JsonObjectConst d:devices)if(d["type"]==type){spec=d;break;}
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
    std::set<int> gpioPins;int pwmCount=0;
    for(JsonObject n:nodes) {
        if(!validateLogicHardware(n,error))return false;
        if(n["type"]=="hardware.gpio") {
            if(!gpioPins.insert(n["parameters"]["pin"].as<int>()).second)return fail("GPIO already owned by another Logics element");
            if(n["parameters"]["mode"]=="pwm" && ++pwmCount>2)return fail("At most two GPIO PWM outputs");
        }
    }
    std::vector<std::vector<size_t>> adjacency(nodes.size());std::vector<bool> root(nodes.size(),false),reached(nodes.size(),false);
    auto index=[&](const char* id){for(size_t i=0;i<nodes.size();++i)if(nodes[i]["id"]==id)return int(i);return -1;};
    for(JsonObjectConst e:input["connections"].as<JsonArrayConst>()) {
        int a=index(e["source"]["node"]|""),b=index(e["target"]["node"]|"");
        if(a<0||b<0||a==b)return fail("Invalid connection endpoints");
        auto ap=port(nodes[a],e["source"]["port"]|"","output"),bp=port(nodes[b],e["target"]["port"]|"","input");
        if(ap.isNull()||bp.isNull()||ap["enabled"]==false||bp["enabled"]==false||!compatible(ap["type"]|"",bp["type"]|""))return fail("Incompatible connector types");
        if(nodes[a]["type"]=="recording.interval" && !((nodes[b]["type"]=="mainboard.save_data"||nodes[b]["type"]=="mainboard.plot") && e["target"]["port"]=="value"))return fail("Sampling Interval output connects only to Save Data.Value or Transfer to Plotter.Value");
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
        if(type=="recording.interval") {
            std::string unit=n["parameters"]["timeUnit"]|"";
            double factor=unit=="milliseconds"?1:unit=="seconds"?1000:unit=="minutes"?60000:unit=="hours"?3600000:unit=="days"?86400000:0;
            double ms=n["parameters"]["duration"].as<double>()*factor;
            if(!std::isfinite(ms)||ms<1||ms>31536000000.0)return fail("Recording interval must be 1 ms to 365 days");
            bool output=false;for(JsonObjectConst e:links)if(e["source"]["node"]==id)output=true;
            if(!output)return fail("Connect Sampling Interval to Save Data.Value or Transfer to Plotter.Value");
        }
        if(type=="timing.repeat"&&!linked(id,"count")){double count=n["parameters"]["count"]|0.0;if(count<1||count>128||count!=std::floor(count))return fail("Repeat count must be a whole number 1-128");}
        if(type=="condition.between"&&!linked(id,"minimum")&&!linked(id,"maximum")&&n["parameters"]["minimum"].as<double>()>n["parameters"]["maximum"].as<double>())return fail("Minimum exceeds maximum");
        if(type=="mainboard.plot"||type=="mainboard.save_data") {
            for(const char* key:{"plot","series","unit"}) {
                if(!n["parameters"][key].is<const char*>())return fail("Plot labels must be text");
                size_t length=strlen(n["parameters"][key].as<const char*>());
                bool unit=std::string(key)=="unit";
                if(length>(unit?16u:32u)||(!unit&&!length))return fail("Invalid Plot label length");
            }
        }
        if(type=="mainboard.save_data") {
            const char* path=n["parameters"]["path"]|"/";
            if(*path!='/'||strlen(path)>128||strstr(path,"..")||strchr(path,'\\'))return fail("Invalid external recording folder");
            for(const unsigned char* c=(const unsigned char*)path;*c;++c)if(*c<32)return fail("Invalid recording folder character");
        }
        if(type=="mainboard.mqtt.publish") {
            if(!linked(id,"topic")){std::string topic=n["parameters"]["topic"]|"";if(topic.empty()||topic.size()>192||topic.find_first_of("+#")!=std::string::npos)return fail("Enter an exact MQTT topic without wildcards");for(unsigned char c:topic)if(c<32)return fail("MQTT topic contains a control character");}
            if(!linked(id,"qos")&&n["parameters"]["qos"]!=1)return fail("MQTT QoS must be 1");
        }
        if(type=="peripheral.text"&&!linked(id,"seconds")){double v=n["parameters"]["seconds"]|0.0;if(v<=0||v>86400)return fail("Display duration must be >0 and <=86400 seconds");}
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
        root[i]=(type=="mainboard.save_data"||type=="mainboard.plot")&&!linked(id,"in") || type.find("event.")==0 || n["binding"]["kind"]=="transition" || n["binding"]["kind"]=="lifecycle" || type=="peripheral.rising" || type=="peripheral.falling" || type=="condition.if"&&!linked(id,"in") || type=="timing.timer"&&linked(id,"enabled");
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
        if(!g["label"].isNull()){const char* label=g["label"]|"";if(strlen(label)>256)return fail("Invalid group label");target["label"]=label;}
        if(!g["rect"].isNull()) {
            for(const char* key:{"x","y","width","height"}){auto value=g["rect"][key];if(!value.is<double>()||!std::isfinite(value.as<double>())||std::abs(value.as<double>())>50000)return fail("Invalid group bounds");}
            if(g["rect"]["width"].as<double>()<240||g["rect"]["height"].as<double>()<100)return fail("Group bounds too small");
            target["rect"].set(g["rect"]);
        }
    }
    for(JsonObjectConst label:input["view"]["labels"].as<JsonArrayConst>())if(!label["groupId"].isNull()){
        const char* groupId=label["groupId"]|"";bool found=false;
        for(JsonObjectConst group:groups)if(group["id"]==groupId){found=true;break;}
        if(!found)return fail("Canvas label refers to a missing group");
    }
    if(measureJson(output)>32768 || output.overflowed())return fail("Validated graph exceeds runtime storage limit");
    return true;
}
}
