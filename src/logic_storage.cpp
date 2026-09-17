#ifndef ELMA_LOGIC_STORAGE_HOST
#include "logic_storage.h"
#include "storage_backend.h"
#include <Preferences.h>
#endif
#include <string>

namespace {uint32_t latestRevision=0;
void rememberRevision(uint32_t revision){latestRevision=revision;Preferences prefs;if(prefs.begin("elma-logics",false)){prefs.putUInt("revision",revision);prefs.end();}}}
bool loadLogicRecord(JsonDocument& record) {
    bool found=false;uint32_t revision=0;
    auto accept=[&](JsonDocument& candidate){
        uint32_t next=candidate["revision"]|0u;
        if(next>latestRevision)latestRevision=next;
        if(!found || next>revision){record.set(candidate);revision=next;found=true;}
    };
    for(auto target:{StorageTarget::Flash,StorageTarget::Sd}) {
        if(!storageMounted(target))continue;
        beginStorageRead(target);
        for(const char* path:{"/.elma-logics.json","/.elma-logics.0.json","/.elma-logics.1.json"}) {
            File file=storageOpen(target,path);JsonDocument candidate;
            if(file && !deserializeJson(candidate,file))accept(candidate);
            if(file)file.close();
        }
        endStorageRead(target);
    }
    Preferences prefs;
    if(prefs.begin("elma-logics",true)){
        uint32_t remembered=prefs.getUInt("revision",0);if(remembered>latestRevision)latestRevision=remembered;
        size_t size=prefs.getBytesLength("record");std::string data;
        if(size && size<=40000){data.resize(size);if(prefs.getBytes("record",&data[0],size)!=size)data.clear();}
        prefs.end();JsonDocument candidate;
        if(!data.empty() && !deserializeJson(candidate,data))accept(candidate);
    }
    return found;
}
bool saveLogicRecord(JsonVariantConst record,String& error) {
    JsonDocument previous,updated;loadLogicRecord(previous);updated.set(record);uint32_t next=latestRevision+1;updated["revision"]=next;
    // Contracts and bindings are rebuilt from trusted firmware definitions on restore.
    updated["graph"].remove("devices");
    for(JsonObject node:updated["graph"]["nodes"].as<JsonArray>()) {
        node.remove("ports");node.remove("binding");
    }
    record=updated.as<JsonVariantConst>();
    for(auto target:{StorageTarget::Flash,StorageTarget::Sd}) {
        auto fs=getStorageFs(target);
        if(!fs || !storageMounted(target))continue;
        beginStorageWrite(target);
        // FAT cannot rename over an existing destination. Write the inactive slot;
        // the previous valid revision remains intact throughout a failed write.
        const char* path=next%2?"/.elma-logics.1.json":"/.elma-logics.0.json";
        File file=storageOpen(target,path,"w");
        bool ok=false;
        if(file) {
            size_t expected=measureJson(record),written=serializeJson(record,file);
            file.flush();file.close();
            if(written==expected){JsonDocument check;File verify=storageOpen(target,path);ok=verify&&!deserializeJson(check,verify)&&check["revision"]==next;if(verify)verify.close();}
        }
        if(!ok)fs->remove(path);
        endStorageWrite(target);
        if(ok){rememberRevision(next);error="";return true;}
    }
    // NVS updates are atomic; never erase existing settings to make room.
    String data;serializeJson(record,data);
    Preferences prefs;
    bool ok=prefs.begin("elma-logics",false);
    if(ok){ok=prefs.putBytes("record",data.c_str(),data.length())==data.length();prefs.end();}
    if(ok){rememberRevision(next);error="";return true;}
    error="Cannot persist Logics: insert a configured SD card or free internal storage";
    return false;
}
