#pragma once
#include <ArduinoJson.h>
#include <cstring>

// Per-browser sequence numbers make retrying saves safe without retaining a
// second graph or replaying On started. Older requests cannot undo newer edits.
class LogicRequestGuard {
    struct Entry{char client[41]{};uint32_t sequence=0,hash=0;};
    Entry entries_[4]{};unsigned next_=0;
public:
    enum Result{Legacy,Fresh,Replayed,Conflict,Invalid};
    static uint32_t hash(JsonVariantConst command){
        struct Writer{uint32_t value=2166136261u;size_t write(uint8_t byte){value=(value^byte)*16777619u;return 1;}size_t write(const uint8_t* data,size_t size){for(size_t i=0;i<size;i++)write(data[i]);return size;}} writer;
        serializeJson(command,writer);return writer.value;
    }
    Result check(JsonVariantConst command)const{
        if(command["request"].isNull())return Legacy;
        const char* client=command["request"]["client"]|"";
        if(!*client||std::strlen(client)>40||!command["request"]["sequence"].is<uint32_t>()||command["request"]["sequence"]==0)return Invalid;
        uint32_t sequence=command["request"]["sequence"];
        for(const auto& entry:entries_)if(!std::strcmp(entry.client,client)){
            if(sequence<entry.sequence)return Replayed;
            if(sequence==entry.sequence)return hash(command)==entry.hash?Replayed:Conflict;
        }
        return Fresh;
    }
    void remember(JsonVariantConst command){
        if(command["request"].isNull())return;
        const char* client=command["request"]["client"]|"";Entry* slot=nullptr;
        for(auto& entry:entries_)if(!std::strcmp(entry.client,client)){slot=&entry;break;}
        if(!slot)slot=&entries_[next_++%4];
        std::strncpy(slot->client,client,40);slot->client[40]='\0';slot->sequence=command["request"]["sequence"];slot->hash=hash(command);
    }
};
