#pragma once
#include <cstdint>
#include <cstdlib>

// Two short, nearby taps. Release the second tap before rebuilding the UI so
// the wake gesture cannot press a control on the newly restored screen.
class LcdWakeGesture {
    bool down_=false,first_=false,moved_=false;
    uint32_t downAt_=0,firstAt_=0;
    int16_t x_=0,y_=0,firstX_=0,firstY_=0;
public:
    void reset(){down_=false;first_=false;moved_=false;}
    bool update(bool touched,int16_t x,int16_t y,uint32_t now){
        if(first_&&uint32_t(now-firstAt_)>450)first_=false;
        if(touched){
            if(!down_){down_=true;downAt_=now;x_=x;y_=y;moved_=false;}
            if(std::abs(int(x)-x_)>30||std::abs(int(y)-y_)>30)moved_=true;
            return false;
        }
        if(!down_)return false;
        down_=false;
        if(moved_||uint32_t(now-downAt_)>250){first_=false;return false;}
        if(first_&&std::abs(int(x_)-firstX_)<=50&&std::abs(int(y_)-firstY_)<=50){reset();return true;}
        first_=true;firstAt_=now;firstX_=x_;firstY_=y_;return false;
    }
};
