#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <string>
#include <cmath>
#include "led_effects.h"
#if !defined(CONFIG_IDF_TARGET_ESP32C2)
#include <Adafruit_NeoPixel.h>
#else
#include <esp_cpu.h>
#include <soc/gpio_reg.h>
#include <soc/soc.h>
// C2 has no RMT. Cap a transfer at 32 RGB pixels (<1 ms), never a long strip.
#define NEO_GRB 0
#define NEO_KHZ800 0
class Adafruit_NeoPixel {
 uint8_t bytes_[96]{};uint16_t count_=0;int pin_=-1;
public:
 void updateType(int){} void setPin(int p){pin_=p;} void updateLength(int n){count_=n<=32?n:0;}
 uint8_t* getPixels(){return count_?bytes_:nullptr;}void begin(){pinMode(pin_,OUTPUT);}
 static uint32_t Color(uint8_t r,uint8_t g,uint8_t b){return (uint32_t(r)<<16)|(uint32_t(g)<<8)|b;}
 void setPixelColor(int i,uint32_t c){if(i<count_){bytes_[3*i]=c>>8;bytes_[3*i+1]=c>>16;bytes_[3*i+2]=c;}}
 void IRAM_ATTR show(){uint32_t mask=1u<<pin_,mhz=getCpuFrequencyMhz(),period=mhz*125/100;noInterrupts();for(int i=0;i<count_*3;i++)for(int bit=7;bit>=0;bit--){uint32_t start=esp_cpu_get_cycle_count();REG_WRITE(GPIO_OUT_W1TS_REG,mask);uint32_t high=mhz*(bytes_[i]&(1<<bit)?70:35)/100;while(uint32_t(esp_cpu_get_cycle_count()-start)<high){}REG_WRITE(GPIO_OUT_W1TC_REG,mask);while(uint32_t(esp_cpu_get_cycle_count()-start)<period){}}interrupts();}
};
#endif

// A fixed number of independently controlled arrays bounds heap and refresh work.
namespace LedArrays {
struct Array {
 int pin=-1; uint16_t count=0; bool on=false,releasePending=false; uint8_t red=255,green=128,blue=0,brightness=20,speed=50,effect=0;
 uint32_t next=0,phase=0,lastFrame=0; bool dirty=true;
 Adafruit_NeoPixel pixels;
};
static Array arrays[4];
inline bool matches(JsonObjectConst n){return n["binding"]["pins"]["DIN"].is<int>() && n["binding"]["count"].is<int>();}
inline bool updateBinding(JsonObject binding,JsonObjectConst values,std::string& error){
 if(values.isNull())return true;
 int limit=binding["maxCount"]|128;
 const char* layout=values["LED_LAYOUT"]|binding["layout"]|"strip";
 if(strcmp(layout,"strip")&&strcmp(layout,"ring")&&strcmp(layout,"panel")){error="Invalid LED layout";return false;}
 for(const char* key:{"LED_ROWS","LED_COLUMNS","LED_COUNT"})if(!values[key].isNull()&&!values[key].is<int>()){error="LED dimensions must be whole numbers";return false;}
 int rows=values["LED_ROWS"]|binding["rows"]|4,columns=values["LED_COLUMNS"]|binding["columns"]|8;
 if(rows<1||columns<1||rows>limit||columns>limit){error="LED dimensions exceed chip limit";return false;}
 int count=!strcmp(layout,"panel")?rows*columns:values["LED_COUNT"]|binding["count"]|8;
 if(rows<1||columns<1||rows>limit||columns>limit||count<1||count>limit){error="LED dimensions exceed chip limit";return false;}
 const char* effect=values["LED_DEFAULT_EFFECT"]|binding["defaults"]["effect"]|"solid";
 if(LedEffects::effectId(effect)<0){error="Unknown LED effect";return false;}
 binding["layout"]=String(layout);binding["count"]=count;binding["rows"]=rows;binding["columns"]=columns;
 binding["serpentine"]=values["LED_SERPENTINE"]|binding["serpentine"]|true;
 binding["defaults"]["effect"]=String(effect);
 const char* keys[]={"brightness","red","green","blue","effectSpeed"};
 const char* source[]={"LED_DEFAULT_BRIGHTNESS","LED_DEFAULT_RED","LED_DEFAULT_GREEN","LED_DEFAULT_BLUE","LED_DEFAULT_EFFECTSPEED"};
 for(int i=0;i<5;i++)if(!values[source[i]].isNull()){
  if(!values[source[i]].is<int>()){error="LED values must be whole numbers";return false;}
  int v=values[source[i]].as<int>();if(v<0||v>((i==0||i==4)?100:255)){error="LED value out of range";return false;}binding["defaults"][keys[i]]=v;
 }
 if(!values["DIN"].isNull()){
  const char* text=values["DIN"].as<const char*>();int pin=values["DIN"].is<int>()?values["DIN"].as<int>():text?atoi(text):-1;
  if(text){for(const char* digit=text;*digit;digit++)if(*digit<'0'||*digit>'9'){error="Invalid LED data pin";return false;}}
  if(pin<0||pin>48){error="Invalid LED data pin";return false;}binding["pins"]["DIN"]=pin;
 }
 return true;
}
inline bool action(JsonObjectConst n,JsonVariantConst a,std::string& error){
 int pin=n["binding"]["pins"]["DIN"]|-1,count=n["binding"]["count"]|0;
#if defined(CONFIG_IDF_TARGET_ESP32C2)
 const int limit=32;
#elif defined(ESP8266)
 const int limit=128;
#else
 const int limit=256;
#endif
 if(pin<0||count<1||count>limit){error="Invalid LED array pin or count";return false;}
 Array* slot=nullptr;for(auto& s:arrays)if(s.pin==pin){slot=&s;break;}
 if(!slot)for(auto& s:arrays)if(s.pin<0){slot=&s;break;}
 if(!slot){error="Maximum four active LED arrays";return false;}
 const char* command=a["action"]|"";
 if(strcmp(command,"strip")&&strcmp(command,"on")&&strcmp(command,"off")&&strcmp(command,"toggle")){error="Unsupported LED array action";return false;}
 if(slot->pin<0||slot->releasePending){
  JsonObjectConst d=n["binding"]["defaults"].as<JsonObjectConst>();
  slot->red=constrain(d["red"]|255,0,255);slot->green=constrain(d["green"]|128,0,255);slot->blue=constrain(d["blue"]|0,0,255);
  slot->brightness=constrain(d["brightness"]|20,0,100);slot->speed=constrain(d["effectSpeed"]|50,0,100);slot->effect=0;
  slot->effect=std::max(0,LedEffects::effectId(d["effect"]|"solid"));
 }
 if(!strcmp(command,"strip")){
  for(const char* key:{"red","green","blue","brightness","effectSpeed"}){double v=a[key]|0.;double max=(!strcmp(key,"brightness")||!strcmp(key,"effectSpeed"))?100:255;if(!std::isfinite(v)||v<0||v>max){error="LED array value out of range";return false;}}
  const char* effect=a["effect"]|"solid";int e=LedEffects::effectId(effect);if(e<0){error="Unknown LED array effect";return false;}
  slot->red=a["red"]|255;slot->green=a["green"]|128;slot->blue=a["blue"]|0;slot->brightness=a["brightness"]|20;slot->speed=a["effectSpeed"]|50;slot->effect=e;
 }
 if(slot->pin!=pin||slot->count!=count){slot->pixels.updateType(NEO_GRB+NEO_KHZ800);slot->pixels.setPin(pin);slot->pixels.updateLength(count);if(!slot->pixels.getPixels()){error="Insufficient memory for LED array";return false;}slot->pixels.begin();slot->pin=pin;slot->count=count;}
 slot->dirty=true;slot->phase=0;slot->lastFrame=millis();slot->releasePending=false;slot->on=!strcmp(command,"toggle")?!slot->on:strcmp(command,"off")!=0;slot->next=0;return true;
}
// Reset is also requested by HTTP handlers. Only the main-loop tick may call
// NeoPixel::show(), which can yield on ESP8266 and must never run in async TCP.
inline void reset(){for(auto& s:arrays)if(s.pin>=0){s.on=false;s.releasePending=true;}}
inline void tick(uint32_t now,bool animate=true){
 for(auto& s:arrays)if(s.releasePending){for(unsigned i=0;i<s.count;i++)s.pixels.setPixelColor(i,0);s.pixels.show();s.pin=-1;s.count=0;s.releasePending=false;s.pixels.updateLength(0);}
 if(!animate)return;
 for(auto& s:arrays){
  if(s.pin<0||(s.next&&int32_t(now-s.next)<0))continue;
  if(!s.dirty&&(!s.on||s.effect==0))continue;
  uint32_t delta=now-s.lastFrame;s.lastFrame=now;
  // Do not jump over pixels after a network/storage stall.
  s.phase+=std::min(delta,uint32_t(25));s.next=now+25;s.dirty=false;
  for(unsigned i=0;i<s.count;i++)s.pixels.setPixelColor(i,s.on?LedEffects::pixel(s.effect,i,s.count,s.phase,s.speed,s.brightness,s.red,s.green,s.blue):0);
  s.pixels.show();
 }
}
}
