#if APP_ROTARY_HMI
#include "legacy_ledc_compat.h"
#endif
#include "logic_gpio.h"
#include <cmath>
#include <set>

LogicGpio::PinState* LogicGpio::prepare(JsonObjectConst node,std::string& error) {
    auto& state=pins_[node["id"]|""];
    const int pin=node["parameters"]["pin"]|-1;const std::string mode=node["parameters"]["mode"]|"";
    if(state.pin==pin&&state.mode==mode)return &state;
    if(state.pin>=0){if(state.channel>=0)ledcDetachPin(state.pin);pinMode(state.pin,INPUT);}
    if(node["binding"]["allowedPins"][std::to_string(pin)].isNull()){error="GPIO is not authorized";return nullptr;}
    state=PinState{};state.pin=pin;state.mode=mode;
    if(mode=="pwm") {
        // Leave timer 0 to the LCD backlight and timer 3 to the buzzer.
        for(int channel:{2,4}) {
            bool used=false;for(auto& entry:pins_)if(entry.second.channel==channel)used=true;
            if(!used){state.channel=channel;break;}
        }
        if(state.channel<0){error="No free GPIO PWM timer";state.pin=-1;return nullptr;}
        if(!ledcSetup(state.channel,node["parameters"]["frequency"]|1000,8)){error="PWM setup failed";state.pin=-1;state.channel=-1;return nullptr;}
        ledcAttachPin(pin,state.channel);ledcWrite(state.channel,0);
    } else if(mode=="output"){digitalWrite(pin,LOW);pinMode(pin,OUTPUT);}
    else pinMode(pin,mode=="input_pullup"?INPUT_PULLUP:mode=="input_pulldown"?INPUT_PULLDOWN:INPUT);
    return &state;
}
bool LogicGpio::action(JsonObjectConst node,JsonVariantConst args,std::string& error) {
    auto* state=prepare(node,error);if(!state)return false;
    const std::string command=args["action"]|"";
    if(command=="pwm"&&state->mode=="pwm") {
        if(!args["duty"].is<double>()){error="PWM duty is unavailable";return false;}
        double duty=args["duty"];if(!std::isfinite(duty)||duty<0||duty>100){error="PWM duty must be 0–100%";return false;}
        ledcWrite(state->channel,lround(duty*255/100));state->high=duty>0;return true;
    }
    if(state->mode!="output"){error="GPIO is not in output mode";return false;}
    if(command=="on")state->high=true;
    else if(command=="off")state->high=false;
    else if(command=="toggle")state->high=!state->high;
    else if(command=="write"&&args["state"].is<bool>())state->high=args["state"];
    else {error="Unsupported GPIO action or missing state";return false;}
    digitalWrite(state->pin,state->high?HIGH:LOW);return true;
}
void LogicGpio::sample(JsonObjectConst node,JsonObject target) {
    std::string ignored;auto* state=prepare(node,ignored);if(!state)return;
    target["digital"]=state->mode=="output"||state->mode=="pwm"?state->high:digitalRead(state->pin)==HIGH;
    if(state->mode=="analog"){analogReadResolution(12);target["analog"]=analogRead(state->pin);target["millivolts"]=analogReadMilliVolts(state->pin);}
}
void LogicGpio::reset() {
    for(auto& entry:pins_) {
        auto& s=entry.second;if(s.pin<0)continue;
        if(s.channel>=0){ledcWrite(s.channel,0);ledcDetachPin(s.pin);}
        if(s.mode=="output")digitalWrite(s.pin,LOW);
        pinMode(s.pin,INPUT);
    }
    pins_.clear();
}
