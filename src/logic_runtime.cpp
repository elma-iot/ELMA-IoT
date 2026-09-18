#include "logic_runtime.h"
#include "logic_text.h"
#include <algorithm>
#include <cstring>

namespace ElmaLogic {
bool Runtime::begin(const char* payload, Action action, std::string& error) {
    suspend(); graph_.clear(); previous_.clear(); cache_.clear(); started_ = false; paused_ = false; error_.clear();
    if (std::strlen(payload) > 32768 || deserializeJson(graph_, payload) ||
        graph_["schemaVersion"] != 1 || !graph_["nodes"].is<JsonArray>() ||
        !graph_["connections"].is<JsonArray>() || graph_["nodes"].size() > 64 || graph_["connections"].size() > 128) {
        error = "Invalid/oversized compiled Logics payload"; graph_.clear(); return false;
    }
    states_.assign(graph_["nodes"].size(), State{}); action_ = std::move(action);
    pulses_.reserve(128); activity_.assign(states_.size(), Activity{});
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
    else if (type.compare(0, 6, "value.") == 0) result.set(input(n, "value"));
    else if (type.compare(0, 10, "mainboard.") == 0) {
        auto binding = node(n)["binding"];
        if (binding["kind"] == "status" && (binding["availability"].isNull() || path(binding["availability"]).as<bool>()))
            result.set(path(binding["path"] | ""));
    } else if (type == "peripheral.reference") result.set(node(n));
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

void Runtime::execute(size_t n, const char*) {
    if(!allowed(n))return;
    auto& state = states_[n]; std::string type = node(n)["type"].as<std::string>();
    if (type=="condition.if" || type=="flow.branch") {
        auto v = input(n,"condition"); if (!v.isNull()) emit(n,v.as<bool>() ? "true" : "false");
    } else if (type=="flow.gate") { if (input(n,"enabled").as<bool>()) emit(n); }
    else if (type=="flow.sequence") { emit(n,"first"); emit(n,"second"); }
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
    } else if (type.compare(0,11,"peripheral.")==0 || type.compare(0,7,"action.")==0 || type.compare(0,10,"mainboard.")==0) {
        JsonDocument args; auto target = node(n);
        if (type.compare(0,7,"action.")==0) { auto ref=input(n,"device");target=ref.as<JsonObjectConst>();args["action"]=type.substr(7); }
        else args["action"]=type.substr(type.find('.')+1);
        if (type=="peripheral.play") {
            auto source=input(n,"source");if(source.is<const char*>())args["source"]["path"].set(source);else args["source"].set(source);
        }
        if(type=="mainboard.mqtt.publish") {
            for(const char* key:{"topic","retained","qos"})args[key].set(input(n,key));
            auto appended=input(n,"value");std::string text;
            if(linked(n,"text")&&input(n,"text").isNull()){error_="MQTT text unavailable";activity_[n].at=now_;++activity_[n].sequence;activity_[n].failed=true;return;}
            if(linked(n,"value")&&appended.isNull()){error_="MQTT appended value unavailable";activity_[n].at=now_;++activity_[n].sequence;activity_[n].failed=true;return;}
            if(!appendText(input(n,"text").as<std::string>(),appended,input(n,"separator").as<std::string>(),text)){error_="MQTT payload exceeds 1024 bytes or is not a primitive value";activity_[n].at=now_;++activity_[n].sequence;activity_[n].failed=true;return;}
            args["payload"]=text;
        }
        if(type=="peripheral.text"){args["text"].set(input(n,"text"));args["seconds"].set(input(n,"seconds"));}
        if (type=="peripheral.volume") args["value"].set(input(n,"volume"));
        else if (linked(n,"value") || !node(n)["parameters"]["value"].isNull()) args["value"].set(input(n,"value"));
        std::string error;
        if (action_ && action_(target,args.as<JsonVariantConst>(),error)) emit(n);
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

void Runtime::tick(uint32_t now, JsonVariantConst status) {
    if(paused_)return;
    now_=now;status_=status;cache_.clear();budget_=0;depth_=0;
    started_=true;
    for(size_t n=0;n<states_.size();++n) {
        std::string type=node(n)["type"].as<std::string>();auto& state=states_[n];
        if(!allowed(n))continue;
        if((type=="event.start" || (node(n)["binding"]["kind"]=="lifecycle" && node(n)["binding"]["event"]=="started")) && !state.startSent){state.startSent=true;emit(n);}
        if(type.compare(0,6,"event.")==0 && type!="event.start") {
            if(changed(n,input(n,"value"),type=="event.rising" ? "rising" : type=="event.falling" ? "falling" : "change"))emit(n);
        } else if(node(n)["binding"]["kind"]=="transition") {
            auto b=node(n)["binding"];if(changed(n,path(b["path"]|""),b["edge"]|"change",b["equals"]))emit(n);
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
        if(blocked&&!state.frozen){state.frozen=true;state.frozenAt=now;}
        else if(!blocked&&state.frozen){if(state.pending)state.due+=now-state.frozenAt;state.frozen=false;}
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
