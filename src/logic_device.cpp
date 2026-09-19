#include "logic_device.h"
#include "system_metrics.h"
#include "device_log.h"
#include "storage_backend.h"
#include "logic_validation.h"
#include "logic_storage.h"
#include <Preferences.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <cstring>

namespace {
constexpr uint32_t kLogicActivityMagic = 0x454c4d41u;
constexpr uint32_t kLogicActionStableMs = 30000u;
constexpr char kLogicRecoveryNamespace[] = "logic_guard";
struct LogicActivityMarker {uint32_t magic;char node[64];char group[64];};
RTC_NOINIT_ATTR LogicActivityMarker logicActivityMarker;

bool abnormalRuntimeReset() {
    const esp_reset_reason_t reason=esp_reset_reason();
    return reason==ESP_RST_PANIC||reason==ESP_RST_INT_WDT||reason==ESP_RST_TASK_WDT||reason==ESP_RST_WDT;
}
void markLogicActivity(const char* node,const std::string& group) {
    logicActivityMarker.magic=kLogicActivityMagic;
    std::strncpy(logicActivityMarker.node,node?node:"",sizeof(logicActivityMarker.node)-1);
    logicActivityMarker.node[sizeof(logicActivityMarker.node)-1]='\0';
    std::strncpy(logicActivityMarker.group,group.c_str(),sizeof(logicActivityMarker.group)-1);
    logicActivityMarker.group[sizeof(logicActivityMarker.group)-1]='\0';
}
void clearLogicActivity(){logicActivityMarker.magic=0;logicActivityMarker.node[0]='\0';logicActivityMarker.group[0]='\0';}

int pin(JsonObjectConst node) {
    for (JsonPairConst item : node["binding"]["pins"].as<JsonObjectConst>())
        if (item.value().is<int>() && item.value().as<int>() >= 0) return item.value().as<int>();
    return -1;
}
}

std::string LogicDevice::groupForNode(const char* nodeId) const {
    if(!nodeId||!*nodeId)return {};
    for(JsonObjectConst group:runtime_.graph()["groups"].as<JsonArrayConst>())
        for(JsonVariantConst member:group["nodes"].as<JsonArrayConst>())
            if(member==nodeId)return group["id"]|"";
    return {};
}

void LogicDevice::clearRecoveryState() {
    Preferences preferences;
    if(preferences.begin(kLogicRecoveryNamespace,false)){preferences.clear();preferences.end();}
    quarantinedGroup_.clear();quarantinedNode_.clear();recoveryWarning_.clear();
    recoveryConfirmationRequired_=false;recoveryAppliedThisBoot_=false;
}

void LogicDevice::loadRecoveryState(JsonDocument& graph) {
    Preferences preferences;
    if(preferences.begin(kLogicRecoveryNamespace,true)){
        if(preferences.getBool("active",false)){
            quarantinedGroup_=preferences.getString("group","").c_str();
            quarantinedNode_=preferences.getString("node","").c_str();
            recoveryWarning_=preferences.getString("warning","").c_str();
            recoveryConfirmationRequired_=true;
        }
        preferences.end();
    }
    if(abnormalRuntimeReset()&&logicActivityMarker.magic==kLogicActivityMagic){
        quarantinedNode_=logicActivityMarker.node;
        quarantinedGroup_=logicActivityMarker.group;
        recoveryWarning_="Logics was stopped after an abnormal restart while running "+
            (quarantinedNode_.empty()?std::string("the automation runtime"):quarantinedNode_)+
            ". Review it before starting it again.";
        recoveryConfirmationRequired_=true;recoveryAppliedThisBoot_=true;
        Preferences writer;
        if(writer.begin(kLogicRecoveryNamespace,false)){
            writer.putBool("active",true);writer.putString("group",quarantinedGroup_.c_str());
            writer.putString("node",quarantinedNode_.c_str());writer.putString("warning",recoveryWarning_.c_str());writer.end();
        }
        DebugLog.printf("[logic-guard] quarantined group=%s node=%s after abnormal reset\n",quarantinedGroup_.c_str(),quarantinedNode_.c_str());
    }
    clearLogicActivity();
    if(!recoveryConfirmationRequired_)return;

    bool nodeExists=quarantinedNode_.empty();
    for(JsonObjectConst node:graph["nodes"].as<JsonArrayConst>())if(node["id"]==quarantinedNode_){nodeExists=true;break;}
    if(!nodeExists){clearRecoveryState();return;}
    bool groupStopped=false;
    if(!quarantinedGroup_.empty())for(JsonObject group:graph["groups"].as<JsonArray>())if(group["id"]==quarantinedGroup_){group["mode"]="stopped";groupStopped=true;break;}
    if(!groupStopped){quarantinedGroup_.clear();mode_="stopped";graph["mode"]="stopped";}
}

bool LogicDevice::begin(const char* program, AppState& state, StatusWriter status, ElmaLogic::Runtime::Action actions) {
    mutex_=xSemaphoreCreateMutex();
    if(!mutex_)return false;
    source_=2166136261u;for(const char* p=program;*p;++p)source_=(source_^uint8_t(*p))*16777619u;
    JsonDocument compiled;deserializeJson(compiled,program);devices_.set(compiled["devices"]);
    mode_=compiled["mode"]|"playing";
    if(mode_!="playing"&&mode_!="paused"&&mode_!="stopped")mode_="stopped";
    std::string restored=program;
    JsonDocument data;
    if(loadLogicRecord(data)) {
        if(data["source"]==source_) {
#ifdef APP_LEGACY_OTA_FIT
            // Compatibility builds retain the compiled graph; only saved control modes are restored.
            for(JsonObject group:compiled["groups"].as<JsonArray>())for(JsonObjectConst savedGroup:data["graph"]["groups"].as<JsonArrayConst>())if(group["id"]==savedGroup["id"]) {
                const char* mode=savedGroup["mode"]|"playing";
                if(std::string(mode)=="playing"||std::string(mode)=="paused"||std::string(mode)=="stopped")group["mode"]=mode;
            }
            restored.clear();serializeJson(compiled,restored);mode_=data["mode"]|"playing";
            if(mode_!="playing"&&mode_!="paused"&&mode_!="stopped")mode_="stopped";
#else
            JsonDocument accepted;std::string message;
            if(ElmaLogic::validateEditable(data["graph"],devices_.as<JsonArrayConst>(),accepted,message)) {
                restored.clear();serializeJson(accepted,restored);mode_=data["mode"]|"playing";
                if(mode_!="playing"&&mode_!="paused"&&mode_!="stopped")mode_="stopped";
            }
#endif
        }
    }
    JsonDocument guarded;deserializeJson(guarded,restored);loadRecoveryState(guarded);restored.clear();serializeJson(guarded,restored);
    state_=&state;status_=std::move(status);actions_=std::move(actions);std::string error;
    bool ok=runtime_.begin(restored.c_str(),[this](JsonObjectConst n,JsonVariantConst a,std::string& e){return action(n,a,e);},error);
    if (!ok) {state_->setLastError(error.c_str());DebugLog.printf("[logics] %s\n",error.c_str());}
    if(ok && mode_=="paused")runtime_.pause(millis());
    if(ok && recoveryAppliedThisBoot_){String ignored;persist(runtime_.graph(),mode_,ignored);state_->setLastError(recoveryWarning_.c_str());}
    return ok;
}

bool LogicDevice::action(JsonObjectConst node, JsonVariantConst args, std::string& error) {
    markLogicActivity(node["id"]|"",groupForNode(node["id"]|""));
    actionMarkedThisTick_=true;
    activityStableAt_=millis()+kLogicActionStableMs;
    if (node["binding"]["group"]=="control" && std::string(node["peripheral"]["profile"]|"").find("relay")!=std::string::npos) {
        int gpio=pin(node);std::string command=args["action"]|"";
        if(gpio<0 || (command!="on" && command!="off" && command!="toggle" && command!="set")) {error="Unsupported Logics relay action";return false;}
        bool on=command=="on" || (command=="toggle" && digitalRead(gpio)==HIGH);
        if(command=="toggle")on=!on;
        if(command=="set"){if(!args["value"].is<double>() || (args["value"]!=0 && args["value"]!=1)){error="Relay Set Value accepts 0 or 1";return false;}on=args["value"].as<double>()!=0;}
        pinMode(gpio,OUTPUT);digitalWrite(gpio,on?HIGH:LOW);return true;
    }
    bool ok=actions_ && actions_(node,args,error);
    if(ok && (node["binding"]["group"]=="audio" || std::string(node["peripheral"]["profile"]|"").find("buzzer")!=std::string::npos))audioOwned_=args["action"]=="play" || (audioOwned_ && args["action"]!="stop");
    if(ok && args["action"]=="play")audioOwner_=node["id"]|"";
    return ok;
}

void LogicDevice::loop(uint32_t now, bool updating) {
    if(!mutex_ || xSemaphoreTake(mutex_,0)!=pdTRUE)return;
    struct Unlock {SemaphoreHandle_t m;~Unlock(){xSemaphoreGive(m);}} unlock{mutex_};
    updating_=updating;
    if (!runtime_.active() || !state_)return;
    if (updating) {runtime_.suspend();polled_=false;return;}
    if (polled_ && uint32_t(now-lastPoll_)<100)return;
    polled_=true;lastPoll_=now;
    JsonDocument snapshot;JsonObject root=snapshot.to<JsonObject>();state_->toJson(root);appendSystemMetricsJson(root);
    root["system"]["lastError"]=state_->snapshot().system.lastError;
    if(status_)status_(root);
    for(JsonObjectConst n:runtime_.nodes()) {
        if(n["binding"]["kind"]!="peripheral")continue;
        std::string id=n["peripheral"]["id"].as<std::string>();auto target=root["peripherals"][id];
        if(n["binding"]["group"]=="sensor" && root["battery"]["available"].as<bool>()) {
            target["voltage"].set(root["battery"]["voltage"]);target["percentage"].set(root["battery"]["percentage"]);
        } else if(n["binding"]["group"]=="input" && (std::string(n["peripheral"]["profile"]|"").find("joystick")!=std::string::npos || std::string(n["peripheral"]["profile"]|"").find("potentiometer")!=std::string::npos)) {
            auto pins=n["binding"]["pins"];int x=pins["VRX"]|pins["OUT"]|-1,y=pins["VRY"]|-1,sw=pins["SW"]|-1;
            if(x>=0){target["value"]=analogRead(x);target["x"].set(target["value"]);}
            if(y>=0)target["y"]=analogRead(y);
            if(sw>=0){pinMode(sw,INPUT_PULLUP);target["pressed"]=digitalRead(sw)==LOW;}
        } else if(n["binding"]["group"]=="control" || n["binding"]["group"]=="input") {
            int gpio=pin(n);if(gpio<0)continue;bool active=digitalRead(gpio)==HIGH;
            if(n["binding"]["group"]=="input") {
                int i=n["binding"]["index"]|0;auto button=root["input"][i==0?"button1":"button2"];
                if(i<2 && button["configuredIndex"]==i)active=button["active"].as<bool>();
            }
            target["state"]=active;
        }
    }
    actionMarkedThisTick_=false;
    const bool waitingForActionStability=logicActivityMarker.magic==kLogicActivityMagic &&
        logicActivityMarker.node[0]!='\0' && static_cast<int32_t>(now-activityStableAt_)<0;
    if(!waitingForActionStability)markLogicActivity("",{});
    if(mode_=="playing")runtime_.tick(now,snapshot.as<JsonVariantConst>());
    else runtime_.observe(snapshot.as<JsonVariantConst>());
    if(!actionMarkedThisTick_&&!waitingForActionStability)clearLogicActivity();
    if(runtime_.error()!=reportedError_) {reportedError_=runtime_.error();if(!reportedError_.empty()){state_->setLastError(reportedError_.c_str());DebugLog.printf("[logics] %s\n",reportedError_.c_str());}}
}

void LogicDevice::enterRecoverySafeMode(const char* warning) {
    if(!mutex_ || xSemaphoreTake(mutex_,pdMS_TO_TICKS(500))!=pdTRUE)return;
    struct Unlock {SemaphoreHandle_t m;~Unlock(){xSemaphoreGive(m);}} unlock{mutex_};
    quarantinedGroup_.clear();quarantinedNode_.clear();
    recoveryWarning_=warning&&*warning?warning:"Recovery safe mode stopped Logics after repeated abnormal restarts.";
    recoveryConfirmationRequired_=true;recoveryAppliedThisBoot_=true;
    Preferences preferences;
    if(preferences.begin(kLogicRecoveryNamespace,false)){
        preferences.putBool("active",true);preferences.putString("group","");preferences.putString("node","");
        preferences.putString("warning",recoveryWarning_.c_str());preferences.end();
    }
    runtime_.restart();mode_="stopped";String ignored;persist(runtime_.graph(),mode_,ignored);
}

void LogicDevice::shuttingDown() {
    if(!mutex_ || xSemaphoreTake(mutex_,pdMS_TO_TICKS(500))!=pdTRUE)return;
    struct Unlock {SemaphoreHandle_t m;~Unlock(){xSemaphoreGive(m);}} unlock{mutex_};
    if(shutdown_ || mode_!="playing")return;
    shutdown_=true;runtime_.lifecycle("shutting_down");runtime_.suspend();
}

bool LogicDevice::persist(JsonVariantConst graph,const std::string& mode,String& error) {
    JsonDocument record;record["source"]=source_;record["mode"]=mode;record["graph"].set(graph);
    return saveLogicRecord(record.as<JsonVariantConst>(),error);
}
void LogicDevice::applyMode(const std::string& mode) {
    if(mode==mode_)return;
    if(mode=="paused")runtime_.pause(millis());
    else if(mode=="stopped") {
        runtime_.restart();
        if(audioOwned_)for(JsonObjectConst n:runtime_.nodes())if(n["id"]==audioOwner_) {
            JsonDocument args;args["action"]="stop";args["all"]=true;std::string error;actions_(n,args.as<JsonVariantConst>(),error);break;
        }
        audioOwned_=false;
    } else if(mode_=="paused")runtime_.resume(millis());
    else runtime_.restart();
    mode_=mode;
}
void LogicDevice::snapshot(JsonDocument& response,bool graph) {
    if(!mutex_ || xSemaphoreTake(mutex_,pdMS_TO_TICKS(500))!=pdTRUE){response["error"]="Logics busy";return;}
    response["mode"]=mode_;response["updating"]=updating_;runtime_.telemetry(response["live"].to<JsonObject>());
    response["groups"].set(runtime_.graph()["groups"]);
#ifdef APP_DISABLE_AUDIO
    response["audioEnabled"]=false;
#else
    response["audioEnabled"]=true;
#endif
    if(graph)response["graph"].set(runtime_.graph());
    if(recoveryConfirmationRequired_){response["recovery"]["confirmationRequired"]=true;response["recovery"]["groupId"]=quarantinedGroup_;response["recovery"]["nodeId"]=quarantinedNode_;response["recovery"]["warning"]=recoveryWarning_;}
    xSemaphoreGive(mutex_);
}
bool LogicDevice::request(JsonVariantConst command,JsonDocument& response,String& error) {
    if(!mutex_ || xSemaphoreTake(mutex_,pdMS_TO_TICKS(500))!=pdTRUE){error="Logics busy";return false;}
    struct Unlock {SemaphoreHandle_t m;~Unlock(){xSemaphoreGive(m);}} unlock{mutex_};
    if(updating_){error="Wait for firmware transfer to finish";return false;}
    bool saved=true;
    std::string mode=command["mode"]|mode_.c_str();
    if(mode!="playing"&&mode!="paused"&&mode!="stopped"){error="Invalid Logics control state";return false;}
    if(!command["group"].isNull()) {
        const char* id=command["group"]["id"]|"";const char* groupMode=command["group"]["mode"]|"";
        if(std::string(groupMode)!="playing"&&std::string(groupMode)!="paused"&&std::string(groupMode)!="stopped"){error="Invalid group control state";return false;}
        if(recoveryConfirmationRequired_&&std::string(groupMode)=="playing"&&quarantinedGroup_==id&&!(command["confirmUnsafeRestart"]|false)){error=(std::string("RECOVERY_CONFIRMATION_REQUIRED: ")+recoveryWarning_+" Confirm to retry this automation.").c_str();return false;}
        if(recoveryConfirmationRequired_&&std::string(groupMode)=="playing"&&quarantinedGroup_==id)clearRecoveryState();
        JsonDocument updated;updated.set(runtime_.graph());bool found=false;
        for(JsonObject group:updated["groups"].as<JsonArray>())if(group["id"]==id){group["mode"]=groupMode;found=true;}
        if(!found){error="Group not found; save the canvas first";return false;}
        saved=persist(updated.as<JsonVariantConst>(),mode_,error);
        runtime_.controlGroup(id,groupMode,millis());
        if(std::string(groupMode)=="stopped" && audioOwned_)for(JsonObjectConst group:runtime_.graph()["groups"].as<JsonArrayConst>())if(group["id"]==id)for(JsonVariantConst member:group["nodes"].as<JsonArrayConst>())for(JsonObjectConst n:runtime_.nodes())if(n["id"]==member&&n["id"]==audioOwner_ ) {
            JsonDocument args;args["action"]="stop";std::string message;actions_(n,args.as<JsonVariantConst>(),message);audioOwned_=false;
        }
    } else if(!command["graph"].isNull()) {
#ifdef APP_LEGACY_OTA_FIT
        error="Legacy OTA compatibility runs the compiled graph. Recompile to edit, or migrate to the larger OTA layout for live graph editing.";return false;
#else
        JsonDocument accepted;std::string message;
        if(!ElmaLogic::validateEditable(command["graph"],devices_.as<JsonArrayConst>(),accepted,message)){error=message.c_str();return false;}
        if(!persist(accepted.as<JsonVariantConst>(),mode,error))return false;
        if(audioOwned_)for(JsonObjectConst n:runtime_.nodes())if(n["id"]==audioOwner_){JsonDocument stop;stop["action"]="stop";stop["all"]=true;std::string ignored;actions_(n,stop.as<JsonVariantConst>(),ignored);break;}
        audioOwned_=false;
        std::string payload;serializeJson(accepted,payload);
        if(!runtime_.begin(payload.c_str(),[this](JsonObjectConst n,JsonVariantConst a,std::string& e){return action(n,a,e);},message)){error=message.c_str();return false;}
        mode_="stopped";applyMode(mode);polled_=false;
#endif
    } else if(mode!=mode_) {
        if(recoveryConfirmationRequired_&&mode=="playing"&&quarantinedGroup_.empty()&&!(command["confirmUnsafeRestart"]|false)){error=(std::string("RECOVERY_CONFIRMATION_REQUIRED: ")+recoveryWarning_+" Confirm to retry Logics.").c_str();return false;}
        if(recoveryConfirmationRequired_&&mode=="playing"&&quarantinedGroup_.empty())clearRecoveryState();
        saved=persist(runtime_.graph(),mode,error);
        applyMode(mode);
    }
    response["mode"]=mode_;response["groups"].set(runtime_.graph()["groups"]);response["saved"]=saved;
    if(!saved){response["warning"]=String("Control applied for this session; reboot persistence failed. ")+error;error="";}
    return true;
}
