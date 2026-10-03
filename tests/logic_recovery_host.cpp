#include "logic_recovery_policy.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace ElmaLogic;
    for (uint8_t attempt=0;attempt<MaxAutoRetries;++attempt)
        assert(recoveryAction(true,true,false,attempt)==RecoveryAction::Retry);
    assert(recoveryAction(true,true,false,3)==RecoveryAction::Stop);
    assert(recoveryAction(true,true,false,255)==RecoveryAction::Stop);
    assert(recoveryAction(false,true,false,3)==RecoveryAction::None);
    assert(recoveryAction(true,false,false,0)==RecoveryAction::None);
    assert(recoveryAction(true,true,true,0)==RecoveryAction::None);
    assert(legacyPrematureStop("Saved Logics were restored stopped after the storage safety upgrade. Review the graph, then explicitly start it.",0));
    assert(legacyPrematureStop("Logics was stopped after an abnormal restart while running node-1. Review it before starting it again.",0));
    assert(!legacyPrematureStop("Logics stopped after three automatic restart attempts failed. Review the graph and device logs, then explicitly start it.",3));
    assert(!legacyPrematureStop("Logics was stopped after an abnormal restart while running node-1",1));
    assert(!legacyPrematureStop("",0));
    std::cout<<"PASS: three automatic retries, stop/pause guards and legacy recovery migration\n";
}
