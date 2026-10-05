#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <string>
namespace PortablePeripherals {
static bool outputKnown[64]{},outputOn[64]{};
inline bool state(int pin){return pin>=0&&pin<64&&outputKnown[pin]?outputOn[pin]:digitalRead(pin)!=LOW;}
inline int pin(JsonObjectConst n){for(JsonPairConst p:n["binding"]["pins"].as<JsonObjectConst>())if(p.value().is<int>()&&p.value().as<int>()>=0)return p.value().as<int>();return -1;}
inline bool output(JsonObjectConst n){const char* p=n["peripheral"]["profile"]|"";for(const char* allowed:{"relay-module","mosfet-switch","solenoid-valve-driver","pump-driver","vibration-motor","led-pwm-dimmer","pwm-fan"})if(!strcmp(p,allowed))return true;return false;}
inline bool action(JsonObjectConst n,JsonVariantConst args,std::string& error){int p=pin(n);if(p<0||p>=64){error="Peripheral GPIO is unassigned";return false;}String command=args["action"]|"";double v=command=="on"?100:command=="off"?0:command=="toggle"?(state(p)?0:100):args["value"]|-1.;bool relay=n["peripheral"]["profile"]=="relay-module";if(command=="set"&&relay)v*=100;if(!isfinite(v)||v<0||v>100){error="Peripheral output value is out of range";return false;}if(command!="on"&&command!="off"&&command!="toggle"&&command!="set"){error="Unsupported peripheral command";return false;}pinMode(p,OUTPUT);if(relay){if(v!=0&&v!=100){error="Relay accepts 0 or 1";return false;}digitalWrite(p,v?HIGH:LOW);}else {
#ifdef ESP8266
 analogWriteRange(1023);
#elif ESP_ARDUINO_VERSION_MAJOR >= 3
 analogWriteResolution(p,10);
#else
 analogWriteResolution(10);
#endif
 analogWrite(p,int(v*1023/100));}outputKnown[p]=true;outputOn[p]=v>0;return true;}
inline void sample(JsonObjectConst n,JsonObject root){if(n["binding"]["kind"]!="peripheral")return;const char* id=n["peripheral"]["id"]|"";auto target=root["peripherals"][id];String profile=n["peripheral"]["profile"]|"";int p=pin(n);if(p<0)return;
 if(n["binding"]["group"]=="input"){
  if(profile.indexOf("analog")>=0){auto pins=n["binding"]["pins"];int x=pins["VRX"]|pins["OUT"]|p,y=pins["VRY"]|-1,sw=pins["SW"]|-1;target["value"]=analogRead(x);target["x"].set(target["value"]);if(y>=0)target["y"]=analogRead(y);if(sw>=0){pinMode(sw,INPUT_PULLUP);target["pressed"]=digitalRead(sw)==LOW;}}
  else {bool passive=profile=="physical-button"||profile=="toggle-switch"||profile=="reed-switch"||profile=="limit-switch"||profile=="wake-button";
#ifdef ESP8266
   pinMode(p,passive&&p!=16?INPUT_PULLUP:INPUT);
#else
   pinMode(p,passive?INPUT_PULLUP:INPUT);
#endif
   target["state"]=passive?digitalRead(p)==LOW:digitalRead(p)==HIGH;}
 }else if(n["binding"]["group"]=="sensor" && profile=="ldr"){target["value"]=analogRead(p);}
 else if(output(n))target["state"]=state(p);
}
}
