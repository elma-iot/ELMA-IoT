#pragma once
#include <stdint.h>
inline bool displayTimeoutDue(uint32_t now,uint32_t activity,bool enabled,uint16_t seconds){
    return enabled && seconds!=0 && uint32_t(now-activity)>=uint32_t(seconds)*1000U;
}
