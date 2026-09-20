#ifndef ELMA_LOGIC_STORAGE_HOST
#include "logic_storage.h"
#include "storage_backend.h"
#include <Preferences.h>
#endif
#include <string>
#include <cstdio>
#include <algorithm>

namespace {
constexpr size_t kNvsLogicChunkBytes=1000;
constexpr uint8_t kNvsLogicMaxChunks=40;
uint32_t latestRevision=0;
void rememberRevision(uint32_t revision){latestRevision=revision;Preferences prefs;if(prefs.begin("elma-logics",false)){prefs.putUInt("revision",revision);prefs.end();}}
std::string chunkKey(char bank,uint8_t index){char key[8];std::snprintf(key,sizeof(key),"%c%02u",bank,unsigned(index));return key;}
bool readChunkBank(Preferences& prefs,char bank,std::string& data){
    char countKey[3]={bank,'c','\0'};uint8_t count=prefs.getUChar(countKey,0);
    if(!count||count>kNvsLogicMaxChunks)return false;
    data.clear();
    for(uint8_t i=0;i<count;++i){
        std::string key=chunkKey(bank,i);size_t size=prefs.getBytesLength(key.c_str());
        if(!size||size>kNvsLogicChunkBytes){data.clear();return false;}
        size_t at=data.size();data.resize(at+size);
        if(prefs.getBytes(key.c_str(),&data[at],size)!=size){data.clear();return false;}
    }
    return !data.empty();
}
void removeChunkBank(Preferences& prefs,char bank){
    char countKey[3]={bank,'c','\0'};uint8_t count=prefs.getUChar(countKey,kNvsLogicMaxChunks);
    if(!count||count>kNvsLogicMaxChunks)count=kNvsLogicMaxChunks;
    prefs.remove(countKey);
    for(uint8_t i=0;i<count;++i){std::string key=chunkKey(bank,i);prefs.remove(key.c_str());}
}
bool writeChunkBank(Preferences& prefs,char bank,const std::string& data){
    size_t count=(data.size()+kNvsLogicChunkBytes-1)/kNvsLogicChunkBytes;
    if(!count||count>kNvsLogicMaxChunks)return false;
    removeChunkBank(prefs,bank);
    for(size_t i=0;i<count;++i){
        size_t at=i*kNvsLogicChunkBytes,size=std::min(kNvsLogicChunkBytes,data.size()-at);
        std::string key=chunkKey(bank,uint8_t(i));
        if(prefs.putBytes(key.c_str(),data.data()+at,size)!=size){removeChunkBank(prefs,bank);return false;}
    }
    char countKey[3]={bank,'c','\0'};
    return prefs.putUChar(countKey,uint8_t(count))==sizeof(uint8_t);
}
}
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
        uint8_t active=prefs.getUChar("bank",0)&1;
        for(char bank:{active?'b':'a',active?'a':'b'}){
            std::string data;JsonDocument candidate;
            if(readChunkBank(prefs,bank,data)&&!deserializeMsgPack(candidate,data))accept(candidate);
        }
        for(const char* key:{"compact","record"}) {
            size_t size=prefs.getBytesLength(key);std::string data;
            if(size && size<=40000){data.resize(size);if(prefs.getBytes(key,&data[0],size)!=size)data.clear();}
            JsonDocument candidate;
            if(!data.empty() && !(std::string(key)=="compact"?deserializeMsgPack(candidate,data):deserializeJson(candidate,data)))accept(candidate);
        }
        prefs.end();
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
    // MessagePack is substantially smaller than the editable JSON and NVS
    // already journals blob updates atomically. Keep the legacy JSON reader
    // above so graphs saved by older firmware migrate on their next save.
    String packed;serializeMsgPack(record,packed);std::string data(packed.c_str(),packed.length());
    auto writeCompact=[&](){
        Preferences prefs;bool saved=prefs.begin("elma-logics",false);
        if(saved){
            uint8_t active=prefs.getUChar("bank",0)&1,next=active^1;char bank=next?'b':'a';
            saved=writeChunkBank(prefs,bank,data);
            if(saved){saved=prefs.putUChar("bank",next)==sizeof(uint8_t);if(saved){removeChunkBank(prefs,active?'b':'a');prefs.remove("compact");prefs.remove("record");}}
            prefs.end();
        }
        return saved;
    };
    bool ok=writeCompact();
    if(!ok) {
        // Persistent Logics are more important than the inactive copy of the
        // bounded reboot log. Reclaim only that old checkpoint and retry; the
        // live serial/RAM log and current boot checkpoint remain available.
        Preferences logs;
        if(logs.begin("rebootlog",false)){uint8_t active=logs.getUChar("active",0)&1;logs.remove(active?"boot0":"boot1");logs.end();}
        Preferences stale;
        if(stale.begin("elma-logics",false)){stale.remove("compact");stale.remove("record");removeChunkBank(stale,stale.getUChar("bank",0)&1?'b':'a');stale.end();}
        ok=writeCompact();
    }
    if(ok){rememberRevision(next);error="";return true;}
    error="Cannot persist Logics: insert a configured SD card or free internal storage";
    return false;
}
bool clearLogicRecord() {
    bool ok=true;
    for(auto target:{StorageTarget::Flash,StorageTarget::Sd}) {
        auto fs=getStorageFs(target);if(!fs || !storageMounted(target))continue;
        beginStorageWrite(target);
        for(const char* path:{"/.elma-logics.json","/.elma-logics.0.json","/.elma-logics.1.json"})fs->remove(path);
        endStorageWrite(target);
    }
    Preferences prefs;
    if(prefs.begin("elma-logics",false)){ok=prefs.clear()&&ok;prefs.end();}else ok=false;
    latestRevision=0;
    return ok;
}
