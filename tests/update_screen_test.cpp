#include "update_screen.h"
#include "firmware_update_gate.h"
#include "display_idle_policy.h"
#include <cassert>
#include <array>
#include <thread>
int main(){
    assert(!displayTimeoutDue(500000,0,true,0));
    assert(!displayTimeoutDue(500000,0,false,1));
    assert(!displayTimeoutDue(999,0,true,1) && displayTimeoutDue(1000,0,true,1));
    assert(displayTimeoutDue(2000,0xfffffff0U,true,1)); // millis rollover
    assert(UpdateScreen::percent(101)==100 && UpdateScreen::arcDegrees(100)==360 && UpdateScreen::arcDegrees(25)==90);
    assert(UpdateScreen::dots(0)==0 && UpdateScreen::dots(1200)==3 && UpdateScreen::dots(1600)==0);
    FirmwareUpdateGate gate;assert(gate.request());assert(gate.state()==FirmwareUpdateGate::Requested);
    std::thread task([&](){gate.acknowledge(true);});task.join();assert(gate.state()==FirmwareUpdateGate::Ready);
    gate.reset();assert(gate.request());gate.cancel();gate.acknowledge(true);assert(gate.state()==FirmwareUpdateGate::Cancelled);
    gate.reset();assert(gate.request());gate.acknowledge(false);assert(gate.state()==FirmwareUpdateGate::Failed && !gate.request());
}
