#pragma once
// Bounded cancellation tokens: queue producers never touch audio drivers.
#include <Arduino.h>
#include <cstring>
namespace LogicAudioDispatch {
struct Entry {char id[65]{};uint32_t token=0;};
static Entry entries[64];static unsigned next=0;static uint32_t serial=0;
static portMUX_TYPE guard=portMUX_INITIALIZER_UNLOCKED;
static uint32_t outputEpochs[2]{};
inline uint32_t outputToken(bool buzzer,bool invalidate=false){portENTER_CRITICAL(&guard);auto& epoch=outputEpochs[buzzer?1:0];if(invalidate)++epoch;auto value=epoch;portEXIT_CRITICAL(&guard);return value;}
inline uint32_t token(const char* id,bool invalidate=false){
 if(!id||!*id)return 0;
 portENTER_CRITICAL(&guard);Entry* found=nullptr;
 for(auto& e:entries)if(strcmp(e.id,id)==0){found=&e;break;}
 if(!found){found=&entries[next++%64];strlcpy(found->id,id,sizeof(found->id));found->token=++serial;}
 if(invalidate)found->token=++serial;
 auto value=found->token;portEXIT_CRITICAL(&guard);return value;
}
inline bool valid(const char* id,uint32_t value){
 if(!id||!*id)return true;bool match=false;portENTER_CRITICAL(&guard);
 for(auto& e:entries)if(strcmp(e.id,id)==0){match=e.token==value;break;}
 portEXIT_CRITICAL(&guard);return match;
}
}
