#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#ifdef ESP32
#include <esp_attr.h>
#define ROTARY_ISR_INLINE __attribute__((always_inline))
#define ROTARY_ISR_DATA DRAM_ATTR
#else
#define ROTARY_ISR_INLINE
#define ROTARY_ISR_DATA
#endif

namespace RotaryHmi {
struct HoldFailsafe {
    uint32_t since = 0;
    bool held = false, fired = false;
    bool sample(bool down, uint32_t now) {
        if (!down) {
            held = fired = false;
            return false;
        }
        if (!held) {
            held = true;
            since = now;
        }
        if (!fired && uint32_t(now - since) >= 15000) {
            fired = true;
            return true;
        }
        return false;
    }
};
// Gray-code transitions reject impossible two-bit jumps; bouncing edges cancel.
struct Encoder {
    uint8_t previous=0; int partial=0;
    void reset(bool a,bool b){previous=(a?2:0)|(b?1:0);partial=0;}
    ROTARY_ISR_INLINE int sample(bool a,bool b,int steps=4){
        static const ROTARY_ISR_DATA int8_t table[16]={0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};
        uint8_t next=(a?2:0)|(b?1:0);
        if((previous^next)==3)partial=0;else partial+=table[(previous<<2)|next];
        previous=next;steps=steps==1||steps==2?steps:4;
        if(partial>=steps){partial-=steps;return 1;}
        if(partial<=-steps){partial+=steps;return -1;}
        return 0;
    }
};
struct Item {
    int id=0,parent=0,submenu=-1;std::string title,icon,kind="action",unit,variable;
    double minimum=0,maximum=100,step=1,value=0;
};
class Model {
public:
    std::vector<Item> items;std::vector<int> history;int menu=0,selected=0;
    bool editing=false,confirming=false;uint32_t revision=0;
    std::vector<int> visible()const{std::vector<int> result;for(size_t i=0;i<items.size();++i)if(items[i].parent==menu)result.push_back(int(i));return result;}
    Item* current(){auto list=visible();return list.empty()?nullptr:&items[list[std::max(0,std::min(selected,int(list.size())-1))]];}
    bool select(int id){auto list=visible();for(size_t i=0;i<list.size();++i)if(items[list[i]].id==id){selected=int(i);editing=false;confirming=false;++revision;return true;}return false;}
    bool navigate(int delta){auto list=visible();if(list.empty())return false;
        if(editing){auto* item=current();double old=item->value;item->value=std::max(item->minimum,std::min(item->maximum,item->value+delta*item->step));if(old!=item->value){++revision;return true;}return false;}
        confirming=false;selected=((selected+delta)%int(list.size())+int(list.size()))%int(list.size());++revision;return false;
    }
    bool activate(){auto* item=current();if(!item)return false;
        if(item->kind=="menu"){if(history.size()>=8)return false;history.push_back(menu);menu=item->submenu;selected=0;editing=false;confirming=false;++revision;return false;}
        if(item->kind=="value"){editing=!editing;++revision;return false;}
        if(item->kind=="confirm"&&!confirming){confirming=true;++revision;return false;}
        confirming=false;++revision;return true;
    }
    void back(){if(confirming){confirming=false;}else if(editing){editing=false;}else if(!history.empty()){menu=history.back();history.pop_back();selected=0;}++revision;}
    bool setValue(int id,double value){if(!std::isfinite(value))return false;for(auto& item:items)if(item.id==id){const double next=std::max(item.minimum,std::min(item.maximum,value));if(next!=item.value){item.value=next;++revision;}return true;}return false;}
};
}
