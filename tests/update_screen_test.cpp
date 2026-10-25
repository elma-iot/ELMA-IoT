#include "update_screen.h"
#include "firmware_update_gate.h"
#include <cassert>
#include <array>
#include <thread>
int main(){
    assert(UpdateScreen::percent(101)==100 && UpdateScreen::arcDegrees(100)==360 && UpdateScreen::arcDegrees(25)==90);
    assert(UpdateScreen::dots(0)==0 && UpdateScreen::dots(1200)==3 && UpdateScreen::dots(1600)==0);
    FirmwareUpdateGate gate;assert(gate.request());assert(gate.state()==FirmwareUpdateGate::Requested);
    std::thread task([&](){gate.acknowledge(true);});task.join();assert(gate.state()==FirmwareUpdateGate::Ready);
    gate.reset();assert(gate.request());gate.cancel();gate.acknowledge(true);assert(gate.state()==FirmwareUpdateGate::Cancelled);
    gate.reset();assert(gate.request());gate.acknowledge(false);assert(gate.state()==FirmwareUpdateGate::Failed && !gate.request());
    constexpr int w=240,h=240;std::array<uint8_t,30*h> bits{};
    auto set=[&](int x,int y){bits[y*30+x/8]|=0x80>>(x%8);};
    set(5,120);set(120,100);set(120,240-5);
    // Every ten-row bounce buffer, including the final one, writes exactly
    // len_bytes. Guards catch confusing pixels with bytes (a 2x overrun).
    constexpr int count=480*10/2;std::array<uint32_t,count+2> output{};
    for(int start=0;start<480*480;start+=480*10){
        output.front()=0x12345678;output.back()=0x87654321;
        UpdateScreen::expandRgbPairs<w,h>(bits.data(),output.data()+1,start,480*10*2);
        assert(output.front()==0x12345678 && output.back()==0x87654321);
        for(int i=0;i<count;++i){
            const int at=start+i*2,x=at%480/2,y=at/480/2;
            bool lit=bits[y*30+x/8]&(0x80>>(x%8));
            const uint32_t c=!lit?0:UpdateScreen::progressPixel(x,y,w,h)?UpdateScreen::blue:0xffff;
            assert(output[i+1]==(c|(c<<16)));
        }
    }
}
