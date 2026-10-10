#include "logic_runtime.h"
#include "logic_text.h"
#include "logic_sleep_contract.h"
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <cctype>

namespace ElmaLogic {
void Runtime::setPeripheralBinding(const char* id,JsonObjectConst binding){
 for(const char* collection:{"devices","nodes"})for(JsonObject item:graph_[collection].as<JsonArray>())if(item["peripheral"]["id"]==id)item["binding"].set(binding);
}
void Runtime::setLedBinding(JsonObjectConst binding) {
    // Metadata only: keep timers, running groups and unsaved execution state.
    for(const char* collection:{"devices","nodes"})for(JsonObject item:graph_[collection].as<JsonArray>()) {
        if(item["type"]!="hardware.led")continue;
        item["binding"].set(binding);
        for(JsonObject port:item["ports"].as<JsonArray>()) {
            const std::string id=port["id"]|"";
            const bool color=id=="red"||id=="green"||id=="blue"||id=="brightness";
            port["enabled"]=(binding["pin"]|-1)>=0 && (!color || binding["ledType"]!="regular");
        }
    }
}
namespace {
void setBuzzerPreset(JsonObject source,const char* requested) {
    const std::string preset=requested?requested:"beep";
    auto melody=source["melody"].to<JsonObject>();melody["instrument"]="organ";melody["octave"]=4;
    auto notes=melody["notes"].to<JsonArray>();
    auto add=[&](int note,float start,float duration){auto item=notes.add<JsonObject>();item["note"]=note;item["start"]=start;item["duration"]=duration;item["velocity"]=.75f;};
    if(preset=="double-beep"){add(69,0,.18f);add(69,.30f,.18f);}
    else if(preset=="alert"){add(76,0,.16f);add(69,.22f,.16f);add(76,.44f,.24f);}
    else if(preset=="doorbell"){add(76,0,.35f);add(72,.42f,.55f);}
    else if(preset=="success"){add(60,0,.16f);add(64,.20f,.16f);add(67,.40f,.30f);}
    else add(69,0,.25f);
}
}
bool Runtime::begin(const char* payload, Action action, std::string& error) {
    if(std::strlen(payload)>32768){error="Invalid/oversized compiled Logics payload";return false;}
    suspend();graph_.clear();previous_.clear();cache_.clear();
    JsonDocument document;
    if(deserializeJson(document,payload)){error="Invalid compiled Logics JSON";return false;}
    return begin(std::move(document),std::move(action),error);
}
bool Runtime::begin(JsonDocument&& payload, Action action, std::string& error) {
    if(payload.overflowed() || payload["schemaVersion"]!=1 || !payload["nodes"].is<JsonArray>() || !payload["connections"].is<JsonArray>() || payload["nodes"].size()>64 || payload["connections"].size()>128 || measureJson(payload)>32768){error="Invalid/oversized compiled Logics payload";return false;}
    suspend();states_.clear();states_.shrink_to_fit();pulses_.clear();pulses_.shrink_to_fit();activity_.clear();activity_.shrink_to_fit();
    graph_=std::move(payload);previous_.clear();cache_.clear();started_=false;paused_=false;error_.clear();
    states_.assign(graph_["nodes"].size(), State{}); action_ = std::move(action);
    pulses_.reserve(std::min(size_t(128),graph_["connections"].size()+size_t(4))); activity_.assign(states_.size(), Activity{});
    for (JsonObjectConst edge : graph_["connections"].as<JsonArrayConst>()) {
        if (index(edge["source"]["node"] | "") < 0 || index(edge["target"]["node"] | "") < 0) {
            error = "Compiled Logics contains a missing node"; graph_.clear(); return false;
        }
    }
    return true;
}

int Runtime::index(const char* id) const {
    for (size_t i = 0; i < states_.size(); ++i) if (node(i)["id"] == id) return int(i);
    return -1;
}

bool Runtime::linked(size_t n, const char* port) const {
    for (JsonObjectConst edge : graph_["connections"].as<JsonArrayConst>())
        if (edge["target"]["node"] == node(n)["id"] && edge["target"]["port"] == port) return true;
    return false;
}

JsonVariantConst Runtime::input(size_t n, const char* port) {
    for (JsonObjectConst edge : graph_["connections"].as<JsonArrayConst>()) {
        if (edge["target"]["node"] == node(n)["id"] && edge["target"]["port"] == port) {
            int upstream = index(edge["source"]["node"] | "");
            return upstream < 0 ? JsonVariantConst{} : value(size_t(upstream), edge["source"]["port"] | "");
        }
    }
    return node(n)["parameters"][port];
}

JsonVariantConst Runtime::path(const char* name) const {
    JsonVariantConst result = status_; std::string text(name); size_t offset = 0;
    while (!result.isNull()) {
        size_t end = text.find('.', offset); std::string key = text.substr(offset, end-offset);
        result = result[key]; if (end == std::string::npos) break; offset = end+1;
    }
    return result;
}

JsonVariantConst Runtime::value(size_t n, const char* port) {
    std::string id = node(n)["id"].as<std::string>();
    if (!cache_[id][port].isNull()) return cache_[id][port];
    if (++depth_ > 32 || ++budget_ > 2048) { --depth_; error_ = "Logics evaluation budget exceeded"; return {}; }
    std::string type = node(n)["type"].as<std::string>();
    JsonDocument result;
    if(type=="value.text") {
        auto appended=input(n,"append");std::string text;
        if((!linked(n,"append")||!appended.isNull()) && appendText(input(n,"value").as<std::string>(),appended,input(n,"separator").as<std::string>(),text))result.set(text);
        else error_="Text value unavailable or exceeds 1024 bytes";
    }
    else if(type=="hardware.wake_gpio") {
        const std::string pin=std::to_string(node(n)["parameters"]["pin"]|-1);
        result["type"]=type;result["parameters"].set(node(n)["parameters"]);
        result["binding"]["allowedPins"][pin].set(node(n)["binding"]["allowedPins"][pin]);
    }
    else if((type=="hardware.sleep"||type=="event.wake")&&std::string(port)=="cause")result.set(status_["power"]["cause"]);
    else if(type=="clock.alarm") {
        const std::string source=node(n)["parameters"]["clockSource"]|"utc";
        auto epoch=status_["clock"][source];
        const bool valid=!epoch.isNull() && epoch.as<int64_t>()>=946684800LL && epoch.as<int64_t>()<4102444800LL;
        if(std::string(port)=="valid")result.set(valid);
        else if(std::string(port)=="epoch" && valid)result.set(epoch);
        else if(std::string(port)=="clockTime" && valid)result.set(alarmClockText(epoch.as<int64_t>()));
    }
    else if(type=="timing.countdown") {
        const auto& state=states_[n];
        if(std::string(port)=="running")result.set(state.pending && !paused_ && !state.frozen);
        else if(std::string(port)=="remaining") {
            const uint32_t at=state.frozen?state.frozenAt:paused_?pausedAt_:now_;
            if(state.pending)result.set(std::max(int32_t(0),int32_t(state.due-at))/1000.0);
            else if(state.initialized)result.set(0.0);
            else result.set(input(n,"seconds"));
        }
    }
    else if(type=="convert.boolean_number"||type=="convert.boolean_integer") {auto v=input(n,"input");if(!v.isNull())result.set(v.as<bool>()?1:0);}
    else if(type=="convert.number_integer"||type=="convert.measurement_number") {
        auto v=input(n,"input");if(!v.isNull()){double x=v.is<bool>()?(v.as<bool>()?1.:0.):v.as<double>();
        if(std::isfinite(x)){if(type=="convert.measurement_number")result.set(x);else if(x>=-2147483648. && x<=2147483647.)result.set(int32_t(x));}}
    }
    else if(type=="convert.text_number") {
        auto v=input(n,"input");if(v.is<const char*>()){const char* text=v.as<const char*>();char* end=nullptr;double x=std::strtod(text,&end);
        if(end!=text){while(*end && std::isspace(static_cast<unsigned char>(*end)))++end;if(!*end && std::isfinite(x))result.set(x);}}
    }
    else if(type.compare(0,7,"bridge.")==0)result.set(input(n,"value"));
    else if(type=="recording.interval")result.set(input(n,"value"));
    else if (type.compare(0, 6, "value.") == 0) result.set(input(n, "value"));
    else if (type.compare(0, 10, "mainboard.") == 0) {
        auto binding = node(n)["binding"];
        if (binding["kind"] == "status" && (binding["availability"].isNull() || path(binding["availability"]).as<bool>()))
            result.set(path(binding["path"] | ""));
    } else if(type=="hardware.can.received")result.set(status_["can"]["received"][port]);
    else if(type=="hardware.can.status")result.set(status_["can"][port]);
    else if(type=="hardware.modbus.value"){
        int index=input(n,"index")|0;auto rs=status_["rs485"];bool available=rs["ready"]==true&&index>=0&&index<int(rs["values"].size());
        if(std::string(port)=="available")result.set(available);else if(available)result.set(rs["values"][index]);
    } else if(type=="hardware.modbus.received")result.set(status_["rs485"]["received"][port]);
    else if(type=="hardware.modbus.status")result.set(status_["rs485"][port]);
    else if(type.compare(0,13,"hardware.hmi.")==0)result.set(status_["hmi"]["data"][type.substr(13)][port]);
    else if(type=="hardware.gpio")result.set(status_["gpio"][id][port]);
    else if(type=="hardware.led")result.set(status_["builtinLed"][port]);
    else if (type == "peripheral.reference") result.set(node(n));
    else if (type=="peripheral.low" || type=="peripheral.critical") {
        JsonVariantConst voltage=status_["peripherals"][node(n)["peripheral"]["id"].as<std::string>()]["voltage"];
        auto threshold=input(n,"threshold");if(!voltage.isNull()&&!threshold.isNull())result.set(voltage.as<double>()<threshold.as<double>());
    } else if (type.compare(0, 11, "peripheral.") == 0 && type != "peripheral.file")
        result.set(status_["peripherals"][node(n)["peripheral"]["id"].as<std::string>()][port]);
    else if (type == "condition.between") {
        auto v = input(n,"value"), a = input(n,"minimum"), b = input(n,"maximum");
        if (!v.isNull() && !a.isNull() && !b.isNull()) result.set(v.as<double>() >= a.as<double>() && v.as<double>() <= b.as<double>());
    } else if (type.compare(0, 10, "condition.") == 0 && port == std::string("result")) {
        auto a = input(n,"a"), b = input(n,"b"); std::string op = node(n)["parameters"]["operator"] | ">";
        if (!a.isNull() && !b.isNull()) {
            double x = a.as<double>(), y = b.as<double>();
            if (std::isfinite(x) && std::isfinite(y)) result.set(op==">" ? x>y : op=="<" ? x<y : op==">=" ? x>=y : op=="<=" ? x<=y : op=="==" ? x==y : x!=y);
        }
    } else if (type.compare(0, 8, "boolean.") == 0) {
        auto a = input(n,type=="boolean.not" ? "value" : "a"), b = input(n,"b");
        if (!a.isNull() && (type=="boolean.not" || !b.isNull())) result.set(type=="boolean.not" ? !a.as<bool>() : type=="boolean.and" ? a.as<bool>() && b.as<bool>() : a.as<bool>() || b.as<bool>());
    } else if (type=="audio.file" || type=="peripheral.file") result.set(input(n,"path"));
    else if (type=="audio.tts") { result["text"].set(input(n,"text")); result["language"].set(input(n,"language")); for(const char* key:{"speechRate","voicePitch","intonation"})result[key].set(node(n)["parameters"][key]); }
    else if (type=="audio.piano") result["melody"].set(node(n)["parameters"]);
    else if (type.compare(0, 6, "audio.") == 0) {
        auto source = input(n,"source");
        if (source.is<const char*>()) result["path"].set(source); else result.set(source);
        if (type=="audio.equalizer") for (const char* band : {"lowDb","presenceDb","highDb"}) result["equalizer"][band].set(input(n,band));
        else { const char* key = type=="audio.volume" ? "volume" : type=="audio.speed" ? "speed" : "semitones"; result[type=="audio.pitch" ? "pitch" : key].set(input(n,key)); }
    }
    cache_[id][port].set(result.as<JsonVariantConst>()); --depth_; return cache_[id][port];
}

bool Runtime::changed(size_t n, JsonVariantConst current, const char* edge, JsonVariantConst equals) {
    auto& state = states_[n]; std::string id = node(n)["id"].as<std::string>();
    if (current.isNull()) { state.initialized = false; return false; }
    auto before = previous_[id].as<JsonVariantConst>();
    bool fired = state.initialized && before != current &&
        (std::strcmp(edge,"rising")==0 ? current.is<bool>() && current.as<bool>() && !before.as<bool>() :
         std::strcmp(edge,"falling")==0 ? current.is<bool>() && !current.as<bool>() && before.as<bool>() :
         std::strcmp(edge,"equals")==0 ? current==equals :
         std::strcmp(edge,"nonempty")==0 ? !current.as<std::string>().empty() : true);
    previous_[id].set(current); state.initialized = true; return fired;
}

uint32_t Runtime::interval(size_t n) {
    auto v = input(n,"seconds"); double seconds = v.as<double>();
    if (v.isNull() || !std::isfinite(seconds) || seconds <= 0 || seconds > 86400) { error_ = "Logics interval must be between 0 and 86400 seconds"; return 0; }
    return std::max(uint32_t(1), uint32_t(seconds*1000));
}

void Runtime::emit(size_t n, const char* port) {
    if(!allowed(n))return;
    activity_[n].at=now_;++activity_[n].sequence;activity_[n].failed=false;
    for (JsonObjectConst edge : graph_["connections"].as<JsonArrayConst>()) {
        if (edge["source"]["node"] == node(n)["id"] && edge["source"]["port"] == port) {
            int target = index(edge["target"]["node"] | "");
            if (target >= 0 && pulses_.size() < 128) pulses_.push_back({size_t(target),edge["target"]["port"].as<std::string>()});
            else error_ = "Logics pulse queue exceeded";
        }
    }
}

bool Runtime::testAction(const char* nodeId, const char* command, std::string& error) {
    const int found=index(nodeId ? nodeId : "");
    if(found<0){error="Logics test node was not found; save the canvas first";return false;}
    const size_t n=size_t(found);const std::string type=node(n)["type"]|"";
    if(type=="clock.alarm" && std::string(command?command:"")=="setClock") {
        int64_t epoch;auto p=node(n)["parameters"];
        if(!alarmDateTime(p["manualDate"]|"",p["manualTime"]|"",epoch)){error="Set a valid clock date/time (2000-2099)";return false;}
        JsonDocument args;args["action"]="setClock";args["epoch"]=epoch;args["source"].set(p["clockSource"]);
        return action_&&action_(node(n),args.as<JsonVariantConst>(),error);
    }
    const std::string profile=node(n)["peripheral"]["profile"]|"";
    if(type!="peripheral.play" || profile.find("buzzer")==std::string::npos){error="Only a configured Buzzer Play node can be tested here";return false;}
    const std::string requested=command ? command : "";
    if(requested!="play"&&requested!="stop"){error="Buzzer test action must be play or stop";return false;}
    JsonDocument args;args["action"]=requested;
    if(requested=="play"){
        cache_.clear();budget_=0;depth_=0;
        auto source=input(n,"source");
        if(source.is<const char*>()&&std::strlen(source.as<const char*>()))args["source"]["path"].set(source);
        else if(!source.isNull())args["source"].set(source);
        else setBuzzerPreset(args["source"].to<JsonObject>(),node(n)["parameters"]["preset"]|"beep");
    }
    const bool ok=action_&&action_(node(n),args.as<JsonVariantConst>(),error);
    activity_[n].at=now_;++activity_[n].sequence;activity_[n].failed=!ok;
    if(!ok&&error.empty())error="Buzzer test action failed";
    return ok;
}

void Runtime::execute(size_t n, const char* trigger) {
    if(!allowed(n))return;
    auto& state = states_[n]; std::string type = node(n)["type"].as<std::string>();
    if(type=="hardware.sleep") {
        if(state.sleeping)return;
        JsonDocument args;args["seconds"].set(input(n,"seconds"));args["wake"].set(input(n,"wake"));std::string error;
        state.wakeSequence=status_["power"]["sequence"]|0u;
        if(action_&&action_(node(n),args.as<JsonVariantConst>(),error))state.sleeping=true;
        else {error_=error.empty()?"Sleep rejected":error;emit(n,"failed");activity_[n].failed=true;}
    }
    else if(type=="clock.alarm") {
        if(std::strcmp(trigger,"rearm")==0)state.alarm=AlarmState{};
        else if(std::strcmp(trigger,"setClock")==0){std::string message;if(!testAction(node(n)["id"]|"","setClock",message))error_=message;}
    }
    else if (type=="condition.if" || type=="flow.branch") {
        auto v = input(n,"condition"); if (!v.isNull()) emit(n,v.as<bool>() ? "true" : "false");
    } else if (type=="flow.gate") { if (input(n,"enabled").as<bool>()) emit(n); }
    else if(type.compare(0,7,"bridge.")==0)emit(n);
    else if (type=="flow.sequence") { emit(n,"first"); emit(n,"second"); }
    else if(type=="timing.countdown") {
        if(std::strcmp(trigger,"stop")==0){state.pending=false;state.initialized=false;}
        else if(std::strcmp(trigger,"in")==0 || std::strcmp(trigger,"reset")==0) {
            const uint32_t ms=interval(n);if(!ms)return;
            const bool run=std::strcmp(trigger,"in")==0 || state.pending;
            state.interval=ms;state.due=now_+ms;state.pending=run;state.initialized=run;
        }
        cache_.remove(node(n)["id"]|"");
    }
    else if (type.compare(0,7,"timing.")==0) {
        uint32_t ms = interval(n); if (!ms) return;
        if (type=="timing.cooldown") { if (!state.pending || int32_t(now_-state.due)>=0) { emit(n); state.pending=true;state.due=now_+ms; } }
        else if (type=="timing.repeat") {
            if (state.pending) return;
            double requested=input(n,"count").as<double>();
            int count=std::isfinite(requested) && requested>=1 && requested<=128 ? int(requested) : 0;
            if (count<1 || count>128 || requested!=count) { error_="Repeat count must be 1–128";return; }
            emit(n); state.remaining=unsigned(count-1);state.pending=count>1;state.due=now_+ms;state.interval=ms;
        } else if (type=="timing.timer") { state.enabled=true;state.pending=true;state.interval=ms;state.due=now_+ms; }
        else { state.pending=true;state.due=now_+ms; }
    } else if (type.compare(0,11,"peripheral.")==0 || type.compare(0,7,"action.")==0 || type.compare(0,10,"mainboard.")==0 || type.compare(0,9,"hardware.")==0) {
        if(type=="mainboard.save_data"||type=="mainboard.plot") {
            if(state.recordingWritten && state.recordingElapsed<recordingInterval(n))return;
            state.recordingWritten=true;state.recordingElapsed=0;
        }
        JsonDocument args; auto target = node(n);
        if (type.compare(0,7,"action.")==0) { auto ref=input(n,"device");target=ref.as<JsonObjectConst>();args["action"]=type.substr(7); }
        else args["action"]=type.substr(type.find('.')+1);
        if(type=="hardware.can.configure"||type=="hardware.can.send"){
            args["action"]=type=="hardware.can.configure"?"configure":"send";
            for(const char* key:{"bitrate","listenOnly","identifier","extended","remote","data","length"})args[key].set(input(n,key));
        }
        if(type=="hardware.modbus.read"){
            args["action"]="read";for(const char* key:{"baud","parity","stops","unit","function","address","count"})args[key].set(input(n,key));
        }
        if (type=="peripheral.play") {
            auto source=input(n,"source");
            if(source.is<const char*>()&&strlen(source.as<const char*>()))args["source"]["path"].set(source);
            else if(!source.isNull())args["source"].set(source);
            else if(std::string(node(n)["peripheral"]["profile"]|"").find("buzzer")!=std::string::npos)setBuzzerPreset(args["source"].to<JsonObject>(),node(n)["parameters"]["preset"]|"beep");
        }
        if(type=="mainboard.plot"||type=="mainboard.save_data")for(const char* key:{"plot","series","unit","path"})args[key].set(node(n)["parameters"][key]);
        if(type=="mainboard.mqtt.publish") {
            for(const char* key:{"topic","retained","qos"})args[key].set(input(n,key));
            auto appended=input(n,"value");std::string text;
            if(linked(n,"text")&&input(n,"text").isNull()){error_="MQTT text unavailable";activity_[n].at=now_;++activity_[n].sequence;activity_[n].failed=true;return;}
            if(linked(n,"value")&&appended.isNull()){error_="MQTT appended value unavailable";activity_[n].at=now_;++activity_[n].sequence;activity_[n].failed=true;return;}
            if(!appendText(input(n,"text").as<std::string>(),appended,input(n,"separator").as<std::string>(),text)){error_="MQTT payload exceeds 1024 bytes or is not a primitive value";activity_[n].at=now_;++activity_[n].sequence;activity_[n].failed=true;return;}
            args["payload"]=text;
        }
        if(type=="peripheral.strip"){for(const char* key:{"red","green","blue","brightness","effectSpeed"})args[key].set(input(n,key));args["effect"].set(node(n)["parameters"]["effect"]);}
        if(type=="peripheral.text"){args["text"].set(input(n,"text"));args["seconds"].set(input(n,"seconds"));}
        if (type=="peripheral.volume") args["value"].set(input(n,"volume"));
        else if (linked(n,"value") || !node(n)["parameters"]["value"].isNull()) args["value"].set(input(n,"value"));
        if((type=="mainboard.plot"||type=="mainboard.save_data")&&args["value"].is<bool>())args["value"]=args["value"].as<bool>()?1:0;
        if(type.compare(0,13,"hardware.hmi.")==0){args["action"]=type.substr(13);for(const char* key:{"itemId","parent","title","kind","submenu","minimum","maximum","step","value","unit","icon","text","navigation","configuration","childId"})args[key].set(input(n,key));}
        if(type=="hardware.gpio" || type=="hardware.led") {
            args["action"]=trigger;
            for(const char* key:{"state","duty","red","green","blue","brightness"})args[key].set(input(n,key));
        }
        std::string error;
        // Never replace a connected-but-unavailable converted value with an action's default.
        if(type.compare(0,10,"mainboard.")!=0)for(auto p:node(n)["ports"].as<JsonArrayConst>()) {
            const char* key=p["id"]|"";
            if(p["direction"]=="input" && p["type"]!="execution" && linked(n,key) && input(n,key).isNull()) {
                error_="Connected input value unavailable";activity_[n].at=now_;++activity_[n].sequence;activity_[n].failed=true;return;
            }
        }
        if (action_ && action_(target,args.as<JsonVariantConst>(),error)) {
#if APP_ESP8266_COMPACT
            // Wi-Fi telemetry may be absent during boot; a later valid sample recovers.
            if(type=="mainboard.plot" && error_=="Plot value unavailable") error_.clear();
#endif
            emit(n);
        }
        else {error_=error.empty() ? "Logics action failed" : error;activity_[n].at=now_;++activity_[n].sequence;activity_[n].failed=true;}
    }
}

void Runtime::drain() {
    size_t at=0; while (at<pulses_.size() && at<128) { auto pulse=pulses_[at++];execute(pulse.node,pulse.port.c_str()); }
    if (at<pulses_.size()) error_="Logics execution budget exceeded";
    pulses_.clear();
}

void Runtime::lifecycle(const char* event) {
    for(size_t n=0;n<states_.size();++n) if(node(n)["binding"]["kind"]=="lifecycle" && node(n)["binding"]["event"]==event) emit(n);
    drain();
}

void Runtime::suspend() {
    pulses_.clear(); for(auto& state:states_) {state.pending=false;state.enabled=false;state.initialized=false;}
}

uint64_t Runtime::recordingInterval(size_t n) const {
    for(JsonObjectConst e:graph_["connections"].as<JsonArrayConst>())if(e["target"]["node"]==node(n)["id"]&&e["target"]["port"]=="value") {
        int upstream=index(e["source"]["node"]|"");
        if(upstream<0||node(upstream)["type"]!="recording.interval")break;
        auto p=node(upstream)["parameters"];std::string unit=p["timeUnit"]|"";
        double factor=unit=="milliseconds"?1:unit=="seconds"?1000:unit=="minutes"?60000:unit=="hours"?3600000:unit=="days"?86400000:0;
        double ms=p["duration"].as<double>()*factor;
        if(std::isfinite(ms)&&ms>=1&&ms<=31536000000.0)return uint64_t(std::ceil(ms));
    }
    return 5000;
}
uint32_t Runtime::recordingPollInterval(uint32_t normal) const {
    if(paused_)return normal;
    for(size_t n=0;n<states_.size();++n)if((node(n)["type"]=="mainboard.save_data"||node(n)["type"]=="mainboard.plot")&&allowed(n))normal=uint32_t(std::min<uint64_t>(normal,recordingInterval(n)));
    return std::max<uint32_t>(1,normal);
}
void Runtime::tick(uint32_t now, JsonVariantConst status) {
    if(paused_)return;
    now_=now;status_=status;cache_.clear();budget_=0;depth_=0;
    started_=true;
    for(size_t n=0;n<states_.size();++n) {
        std::string type=node(n)["type"].as<std::string>();auto& state=states_[n];
        if(type=="mainboard.save_data"||type=="mainboard.plot") {
            if(state.recordingStarted && allowed(n))state.recordingElapsed+=uint32_t(now-state.recordingAt);
            state.recordingAt=now;state.recordingStarted=true;
        }
        if(!allowed(n))continue;
        if(type=="hardware.sleep"&&state.sleeping && status_["power"]["node"]==node(n)["id"] && (status_["power"]["sequence"]|0u)!=state.wakeSequence) {
            state.sleeping=false;const std::string error=status_["power"]["error"]|"";
            if(error.empty())emit(n);else {error_=error;activity_[n].failed=true;emit(n,"failed");}
        }
        if(type=="event.wake") {
            uint32_t count=status_["power"]["wakeCount"]|0u;
            if(count&&count!=state.wakeSequence){state.wakeSequence=count;emit(n);}
        }
        if(type=="clock.alarm") {
            auto p=node(n)["parameters"];const std::string source=p["clockSource"]|"utc";
            auto epoch=status_["clock"][source];
            if(alarmDue(p,epoch.isNull()?0:epoch.as<int64_t>(),input(n,"enabled").as<bool>(),state.alarm))emit(n);
        }
        if((type=="mainboard.save_data"||type=="mainboard.plot") && !linked(n,"in") && state.recordingElapsed>=recordingInterval(n))execute(n,"in");
        if((type=="event.start" || (node(n)["binding"]["kind"]=="lifecycle" && node(n)["binding"]["event"]=="started")) && !state.startSent){state.startSent=true;emit(n);}
        if(type.compare(0,6,"event.")==0 && type!="event.start" && type!="event.wake") {
            if(changed(n,input(n,"value"),type=="event.rising" ? "rising" : type=="event.falling" ? "falling" : "change"))emit(n);
        } else if(type=="hardware.can.received") {
            uint32_t sequence=status_["can"]["rxSequence"]|0u;int64_t identifier=input(n,"identifierFilter")|int64_t(-1);
            if(sequence&&sequence!=state.wakeSequence){state.wakeSequence=sequence;if(identifier<0||identifier==(status_["can"]["received"]["identifier"]|int64_t(-2)))emit(n);}
        } else if(type=="hardware.modbus.received") {
            uint32_t sequence=status_["rs485"]["rxSequence"]|0u;int unit=input(n,"unitFilter")|0;
            if(sequence&&sequence!=state.wakeSequence){state.wakeSequence=sequence;if(!unit||unit==(status_["rs485"]["received"]["unit"]|-1))emit(n);}
        } else if(node(n)["binding"]["kind"]=="transition") {
            auto b=node(n)["binding"];if(changed(n,path(b["path"]|""),b["edge"]|"change",b["equals"]) && (type.compare(0,13,"hardware.hmi.")!=0 || (node(n)["parameters"]["itemIdFilter"]|0)==0 || status_["hmi"]["data"][type.substr(13)]["itemId"]==node(n)["parameters"]["itemIdFilter"]))emit(n);
        } else if(type=="peripheral.rising" || type=="peripheral.falling") {
            JsonVariantConst v=status_["peripherals"][node(n)["peripheral"]["id"].as<std::string>()]["state"];
            if(changed(n,v,type=="peripheral.rising" ? "rising" : "falling"))emit(n);
        } else if(type=="condition.if" && !linked(n,"in")) {
            auto v=input(n,"condition");if(changed(n,v,"change"))emit(n,v.as<bool>()?"true":"false");
        } else if(type=="timing.timer" && linked(n,"enabled")) {
            bool enabled=input(n,"enabled").as<bool>();
            if(enabled&&!state.enabled){state.interval=interval(n);state.pending=state.interval>0;state.due=now_+state.interval;}
            if(!enabled)state.pending=false;
            state.enabled=enabled;
        }
        if(state.pending && int32_t(now_-state.due)>=0 && type!="timing.cooldown") {
            if(type=="timing.countdown"){state.pending=false;cache_.remove(node(n)["id"]|"");}
            emit(n);
            if(type=="timing.repeat") {if(--state.remaining==0)state.pending=false;else state.due=now_+state.interval;}
            else if(type=="timing.timer")state.due=now_+state.interval;
            else state.pending=false;
        }
    }
    drain();
    for(size_t n=0;n<states_.size();++n)for(JsonObjectConst p:node(n)["ports"].as<JsonArrayConst>())if(p["direction"]=="output" && p["type"]!="execution" && p["type"]!="audio" && p["type"]!="peripheral")value(n,p["id"]|"");
    status_={};
}
}

namespace ElmaLogic {
void Runtime::pause(uint32_t now) { if(!paused_) {paused_=true;pausedAt_=now;freezeStates(now);} }
void Runtime::resume(uint32_t now) {if(!paused_)return;paused_=false;freezeStates(now);}
void Runtime::restart() {suspend();for(auto& state:states_)state=State{};previous_.clear();cache_.clear();started_=false;paused_=false;error_.clear();}
void Runtime::telemetry(JsonObject target) const {
    for(JsonPairConst n:cache_.as<JsonObjectConst>())for(JsonPairConst p:n.value().as<JsonObjectConst>()) {
        if(p.value().is<JsonObjectConst>() || p.value().is<JsonArrayConst>())continue;
        target["values"][n.key().c_str()][p.key().c_str()].set(p.value());
    }
    for(size_t n=0;n<activity_.size();++n) {
        const std::string id=node(n)["id"].as<std::string>();
        if(node(n)["type"]=="timing.countdown" && states_[n].pending) {
            const auto& state=states_[n];
            const uint32_t at=state.frozen?state.frozenAt:paused_?pausedAt_:now_;
            target["values"][id]["remaining"]=std::max(int32_t(0),int32_t(state.due-at))/1000.0;
            target["values"][id]["running"]=!paused_ && !state.frozen;
        }
        auto item=target["activity"][id];
        item["sequence"]=activity_[n].sequence;item["at"]=activity_[n].at;item["failed"]=activity_[n].failed;
    }
    target["error"]=error_;
}
}

namespace ElmaLogic {
void Runtime::observe(JsonVariantConst status) {
    status_=status;cache_.clear();budget_=0;depth_=0;
    for(size_t n=0;n<states_.size();++n)for(JsonObjectConst p:node(n)["ports"].as<JsonArrayConst>())if(p["direction"]=="output" && p["type"]!="execution" && p["type"]!="audio" && p["type"]!="peripheral")value(n,p["id"]|"");
    status_={};
}
}

namespace ElmaLogic {
bool Runtime::allowed(size_t n) const {
    if(paused_)return false;
    for(JsonObjectConst group:graph_["groups"].as<JsonArrayConst>())for(JsonVariantConst member:group["nodes"].as<JsonArrayConst>())if(member==node(n)["id"])return group["mode"]=="playing" || group["mode"].isNull();
    return true;
}
void Runtime::freezeStates(uint32_t now) {
    for(size_t n=0;n<states_.size();++n){auto& state=states_[n];bool blocked=!allowed(n);
        if(blocked&&!state.frozen){state.frozen=true;state.frozenAt=now;state.alarm.previous=0;}
        else if(!blocked&&state.frozen){if(state.pending)state.due+=now-state.frozenAt;state.recordingAt=now;state.frozen=false;}
    }
}
bool Runtime::controlGroup(const char* id,const char* mode,uint32_t now) {
    if(std::string(mode)!="playing"&&std::string(mode)!="paused"&&std::string(mode)!="stopped")return false;
    for(JsonObject group:graph_["groups"].as<JsonArray>())if(group["id"]==id){
        if(group["mode"]==mode)return true;
        group["mode"]=mode;
        if(std::string(mode)=="stopped")for(JsonVariantConst member:group["nodes"].as<JsonArrayConst>()){int n=index(member|"");if(n>=0){states_[n]=State{};previous_.remove(member|"");}}
        freezeStates(now);return true;
    }
    return false;
}
}
