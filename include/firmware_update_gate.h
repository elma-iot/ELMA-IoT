#pragma once
#include <atomic>

// Upload/network tasks request an exclusive presentation; loopTask owns all
// subsystem teardown and display operations. A timeout can never authorize a write.
class FirmwareUpdateGate {
public:
    enum State {Idle,Requested,Ready,Failed,Cancelled};
    bool request(){int expected=Idle;return state_.compare_exchange_strong(expected,Requested)||expected==Ready;}
    State state()const{return static_cast<State>(state_.load(std::memory_order_acquire));}
    void acknowledge(bool ready){int expected=Requested;state_.compare_exchange_strong(expected,ready?Ready:Failed);}
    void cancel(){int expected=Requested;state_.compare_exchange_strong(expected,Cancelled);}
    void reset(){state_.store(Idle,std::memory_order_release);}
private:
    std::atomic<int> state_{Idle};
};
