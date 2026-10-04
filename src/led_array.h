#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <string>
#include <cmath>
#include "led_effects.h"
#if !defined(ESP8266) && !defined(APP_DISABLE_AUDIO)
#include "led_audio.h"
#endif
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

#if defined(ESP32) && !defined(CONFIG_IDF_TARGET_ESP32C2) && !defined(APP_DISABLE_AUDIO)
#include "led_output.h"
using LedPixelOutput=LedOutput::Pixels;
#else
using LedPixelOutput=Adafruit_NeoPixel;
#endif
// A fixed number of independently controlled arrays bounds heap and refresh work.
namespace LedArrays {
struct Segment {
 uint16_t offset=0,count=0;uint8_t red=255,green=128,blue=0,brightness=20,speed=50,effect=0;uint32_t phase=0;
};
struct Array {
 Segment segments[8];uint8_t segmentCount=1;
 int pin=-1; uint16_t count=0; bool on=false,releasePending=false; uint8_t red=255,green=128,blue=0,brightness=20,speed=50,effect=0;
 uint32_t next=0,phase=0,lastFrame=0; bool dirty=true,initialClearPending=false;
 LedPixelOutput pixels;
};
static Array arrays[4];
static bool shuttingDown=false;
struct BootIndicator {bool done=false;uint8_t mode=0;uint32_t since=0;};
static BootIndicator boot;
inline void bootWifi(bool ap,bool connected,uint32_t now){
 if(boot.done)return;
 uint8_t next=connected?2:ap?1:0;
 if(next!=boot.mode){boot.mode=next;boot.since=now;}
 if(boot.mode&&uint32_t(now-boot.since)>=(boot.mode==2?1500u:5000u)){boot.done=true;for(auto& a:arrays)a.dirty=true;}
}
inline bool bootActive(){return !boot.done&&boot.mode;}
inline uint32_t bootPixel(uint32_t now){
 // Fixed 20% physical brightness, independent of optional effect modules.
 float gain=boot.mode==2?1.f:.5f-.5f*cosf(6.28318530718f*((now-boot.since)%2000)/2000);
 uint32_t value=uint32_t(51*gain+.5f);return boot.mode==2?value<<8:value;
}
inline bool matches(JsonObjectConst n){return n["binding"]["pins"]["DIN"].is<int>() && n["binding"]["count"].is<int>();}
inline bool updateBinding(JsonObject binding,JsonObjectConst values,std::string& error){
 if(values.isNull())return true;
 int limit=binding["maxCount"]|128;
 const char* layout=values["LED_LAYOUT"]|binding["layout"]|"strip";
 if(strcmp(layout,"strip")&&strcmp(layout,"ring")&&strcmp(layout,"panel")){error="Invalid LED layout";return false;}
 for(const char* key:{"LED_ROWS","LED_COLUMNS","LED_COUNT","LED_ARRAYS"})if(!values[key].isNull()&&!values[key].is<int>()){error="LED dimensions must be whole numbers";return false;}
 int rows=values["LED_ROWS"]|binding["rows"]|4,columns=values["LED_COLUMNS"]|binding["columns"]|8;
 if(rows<1||columns<1||rows>limit||columns>limit){error="LED dimensions exceed chip limit";return false;}
 int count=!strcmp(layout,"panel")?rows*columns:values["LED_COUNT"]|binding["perArrayCount"]|binding["count"]|8;
 if(rows<1||columns<1||rows>limit||columns>limit||count<1||count>limit){error="LED dimensions exceed chip limit";return false;}
 const char* effect=values["LED_DEFAULT_EFFECT"]|binding["defaults"]["effect"]|"solid";
 if(LedEffects::effectId(effect)<0){error="Unknown LED effect";return false;}
 binding["layout"]=String(layout);binding["count"]=count;binding["rows"]=rows;binding["columns"]=columns;
 binding["serpentine"]=values["LED_SERPENTINE"].is<bool>()?values["LED_SERPENTINE"].as<bool>():binding["serpentine"]|true;
 binding["defaults"]["effect"]=String(effect);
 const char* keys[]={"brightness","red","green","blue","effectSpeed"};
 const char* source[]={"LED_DEFAULT_BRIGHTNESS","LED_DEFAULT_RED","LED_DEFAULT_GREEN","LED_DEFAULT_BLUE","LED_DEFAULT_EFFECTSPEED"};
 for(int i=0;i<5;i++)if(!values[source[i]].isNull()){
  if(!values[source[i]].is<int>()){error="LED values must be whole numbers";return false;}
  int v=values[source[i]].as<int>();if(v<0||v>((i==0||i==4)?100:255)){error="LED value out of range";return false;}binding["defaults"][keys[i]]=v;
 }
 int arrayCount=values["LED_ARRAYS"]|binding["arrayCount"]|1;
 if(arrayCount<1||arrayCount>8){error="Maximum 8 chained arrays per data output";return false;}
 if(!values["LED_SYNC"].isNull()&&!values["LED_SYNC"].is<bool>()){error="LED sync must be a checkbox value";return false;}
 bool sync=values["LED_SYNC"].is<bool>()?values["LED_SYNC"].as<bool>():binding["sync"]|false;
 if(!values["LED_ARRAY_ITEMS"].isNull()&&!values["LED_ARRAY_ITEMS"].is<JsonArrayConst>()){error="Invalid chained array definitions";return false;}
 JsonDocument chain;JsonArray parts=chain.to<JsonArray>();int total=0;
 for(int index=0;index<arrayCount;index++){
  JsonVariantConst raw=index?values["LED_ARRAY_ITEMS"][index-1]:JsonVariantConst();
  if(!raw.isNull()&&!raw.is<JsonObjectConst>()){error="Invalid chained array definition";return false;}
  JsonObjectConst extra=raw.as<JsonObjectConst>();
  for(const char* key:{"LED_ROWS","LED_COLUMNS","LED_COUNT"})if(!extra[key].isNull()&&!extra[key].is<int>()){error="LED dimensions must be whole numbers";return false;}
  const char* shape=extra["LED_LAYOUT"]|layout;int r=extra["LED_ROWS"]|rows,c=extra["LED_COLUMNS"]|columns;
  if(strcmp(shape,"strip")&&strcmp(shape,"ring")&&strcmp(shape,"panel")){error="Invalid chained LED layout";return false;}
  if(r<1||c<1||r>limit||c>limit){error="LED dimensions exceed chip limit";return false;}
  int pixels=!strcmp(shape,"panel")?r*c:extra["LED_COUNT"]|count;
  if(pixels<1||pixels>limit||total+pixels>limit){error="Combined LED chain exceeds chip limit";return false;}
  JsonObject segment=parts.add<JsonObject>();segment["offset"]=total;segment["count"]=pixels;segment["layout"]=shape;segment["rows"]=r;segment["columns"]=c;segment["serpentine"]=extra["LED_SERPENTINE"].is<bool>()?extra["LED_SERPENTINE"].as<bool>():binding["serpentine"]|true;
  segment["defaults"].set(binding["defaults"]);
  if(!sync&&index){
   if(!extra["LED_DEFAULT_EFFECT"].isNull()){const char* e=extra["LED_DEFAULT_EFFECT"]|"";if(LedEffects::effectId(e)<0){error="Unknown chained LED effect";return false;}segment["defaults"]["effect"]=e;}
   for(int j=0;j<5;j++)if(!extra[source[j]].isNull()){
    if(!extra[source[j]].is<int>()){error="LED values must be whole numbers";return false;}
    int v=extra[source[j]].as<int>();if(v<0||v>((j==0||j==4)?100:255)){error="LED value out of range";return false;}segment["defaults"][keys[j]]=v;
   }
  }
  total+=pixels;
 }
 binding["perArrayCount"]=count;binding["count"]=total;binding["arrayCount"]=arrayCount;binding["sync"]=sync;binding["segments"].set(parts);
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
 bool configured=a["configured"]|false;
 if(!strcmp(command,"strip")&&!configured){
  for(const char* key:{"red","green","blue","brightness","effectSpeed"}){double v=a[key]|0.;double maximum=(!strcmp(key,"brightness")||!strcmp(key,"effectSpeed"))?100:255;if(!std::isfinite(v)||v<0||v>maximum){error="LED array value out of range";return false;}}
  if(LedEffects::effectId(a["effect"]|"solid")<0){error="Unknown LED array effect";return false;}
 }
 if(slot->pin<0||slot->releasePending||configured||slot->count!=count){
  JsonArrayConst definitions=n["binding"]["segments"].as<JsonArrayConst>();int number=definitions.isNull()?1:definitions.size();
  if(number<1||number>8){error="Invalid chained LED arrays";return false;}
  Segment next[8];unsigned offset=0;
  for(int i=0;i<number;i++){
   JsonObjectConst definition=definitions[i],d=definition["defaults"].is<JsonObjectConst>()?definition["defaults"].as<JsonObjectConst>():n["binding"]["defaults"].as<JsonObjectConst>();
   int pixels=definition["count"]|count;if(pixels<1||offset+pixels>unsigned(count)){error="Invalid chained LED count";return false;}
   auto& v=next[i];v.offset=offset;v.count=pixels;offset+=pixels;
   v.red=constrain(d["red"]|255,0,255);v.green=constrain(d["green"]|128,0,255);v.blue=constrain(d["blue"]|0,0,255);v.brightness=constrain(d["brightness"]|20,0,100);v.speed=constrain(d["effectSpeed"]|50,0,100);v.effect=std::max(0,LedEffects::effectId(d["effect"]|"solid"));
   const auto& old=slot->segments[i];
   if(configured&&slot->pin==pin&&i<slot->segmentCount&&old.offset==v.offset&&old.count==v.count&&old.red==v.red&&old.green==v.green&&old.blue==v.blue&&old.brightness==v.brightness&&old.speed==v.speed&&old.effect==v.effect)v.phase=old.phase;
  }
  if(offset!=unsigned(count)){error="Chained LED counts do not match output";return false;}
  if(n["binding"]["sync"]|false)for(int i=1;i<number;i++)next[i].phase=next[0].phase;
  slot->segmentCount=number;for(int i=0;i<number;i++)slot->segments[i]=next[i];
 }
 if(!strcmp(command,"strip")&&!configured)for(int i=0;i<slot->segmentCount;i++){
  auto& v=slot->segments[i];v.red=a["red"]|255;v.green=a["green"]|128;v.blue=a["blue"]|0;v.brightness=a["brightness"]|20;v.speed=a["effectSpeed"]|50;v.effect=LedEffects::effectId(a["effect"]|"solid");v.phase=0;
 }
 if(slot->pin!=pin||slot->count!=count){slot->pixels.updateType(NEO_GRB+NEO_KHZ800);slot->pixels.setPin(pin);slot->pixels.updateLength(count);if(!slot->pixels.getPixels()){error="Insufficient memory for LED array";return false;}slot->pixels.begin();slot->pin=pin;slot->count=count;slot->initialClearPending=true;}
 slot->dirty=true;slot->phase=0;slot->lastFrame=millis();slot->releasePending=false;slot->on=!strcmp(command,"toggle")?!slot->on:strcmp(command,"off")!=0;slot->next=0;return true;
}
// Reset is also requested by HTTP handlers. Only the main-loop tick may call
// NeoPixel::show(), which can yield on ESP8266 and must never run in async TCP.
inline void reset(){for(auto& s:arrays)if(s.pin>=0){s.on=false;s.releasePending=true;}}
// Call from the owning loop before reset. A queue submission alone is not
// sufficient on audio targets: the final black frame must reach the wire.
inline bool shutdown(){
 shuttingDown=true;boot.done=true;bool sent=true;
 for(auto& s:arrays)if(s.pin>=0){
  s.on=false;s.dirty=false;for(unsigned i=0;i<s.count;i++)s.pixels.setPixelColor(i,0);
#if defined(ESP32) && !defined(CONFIG_IDF_TARGET_ESP32C2) && !defined(APP_DISABLE_AUDIO)
  sent=s.pixels.showAndWait()&&sent;
#else
  s.pixels.show();delayMicroseconds(300);
#endif
 }
 return sent;
}
inline void tick(uint32_t now,bool animate=true){
 if(shuttingDown)return;
#if !defined(ESP8266) && !defined(APP_DISABLE_AUDIO)
 bool stream=false,mic=false;for(const auto& a:arrays)if(a.on&&a.pin>=0)for(unsigned j=0;j<a.segmentCount;j++){stream|=a.segments[j].effect>=10&&a.segments[j].effect<13;mic|=a.segments[j].effect>=13;}
 LedAudio::playback().enabled.store(stream&&animate,std::memory_order_relaxed);LedAudio::microphone().enabled.store(mic&&animate,std::memory_order_relaxed);
 LedAudio::service(now);
#endif
 for(auto& s:arrays)if(s.releasePending){for(unsigned i=0;i<s.count;i++)s.pixels.setPixelColor(i,0);s.pixels.show();s.pin=-1;s.count=0;s.releasePending=false;s.pixels.updateLength(0);}
 if(!animate&&!bootActive()){for(auto& s:arrays)s.lastFrame=now;return;}
 for(auto& s:arrays){
  if(s.pin<0||(s.next&&int32_t(now-s.next)<0))continue;
  // Clears latched colors after ungraceful resets, before the boot animation.
  if(s.initialClearPending){s.initialClearPending=false;for(unsigned i=0;i<s.count;i++)s.pixels.setPixelColor(i,0);s.pixels.show();s.next=now+25;continue;}
  bool animated=false;for(int j=0;j<s.segmentCount;j++)animated|=s.segments[j].effect!=0;
  if(!bootActive()&&!s.dirty&&(!s.on||!animated))continue;
  uint32_t delta=int32_t(now-s.lastFrame)>0?now-s.lastFrame:0;s.lastFrame=now;
  // Wall-clock phase keeps the effect speed independent of loop/render rate.
  s.phase+=delta;s.next=now+25;s.dirty=false;
  for(int j=0;j<s.segmentCount;j++){auto& v=s.segments[j];v.phase+=delta;
   for(unsigned i=0;i<v.count;i++){
    unsigned brightness=v.brightness;int effect=v.effect;
#if !defined(ESP8266) && !defined(APP_DISABLE_AUDIO)
    if(effect>=10){const auto& spectrum=effect>=13?LedAudio::micSpectrum():LedAudio::streamSpectrum();unsigned style=(effect-10)%3;float gain=spectrum.level;
     if(style==0)gain=spectrum.bands[std::min(7u,i*8/v.count)];
     else if(style==1)gain=(float(i)/v.count)<spectrum.level?1.f:0.f;
     brightness=unsigned(brightness*gain+.5f);effect=style==0?4:0;
    }
#endif
    s.pixels.setPixelColor(v.offset+i,bootActive()?bootPixel(now):s.on?LedEffects::pixel(effect,i,v.count,v.phase,v.speed,brightness,v.red,v.green,v.blue):0);
   }
  }
  const auto& first=s.segments[0];s.effect=first.effect;s.red=first.red;s.green=first.green;s.blue=first.blue;s.speed=first.speed;s.brightness=first.brightness;s.phase=first.phase;
  s.pixels.show();
 }
}
// Report active runtime values, including Logics overrides, without sending pixels.
inline void snapshot(JsonArray out,bool running){
 for(const auto& s:arrays)if(s.pin>=0&&!s.releasePending){
  JsonObject item=out.add<JsonObject>();item["pin"]=s.pin;item["count"]=s.count;item["on"]=s.on||bootActive();
  item["effect"]=LedEffects::names[s.effect];item["brightness"]=s.brightness;item["effectSpeed"]=s.speed;
  item["red"]=s.red;item["green"]=s.green;item["blue"]=s.blue;
  if(s.segmentCount>1){JsonArray segments=item["segments"].to<JsonArray>();for(int j=0;j<s.segmentCount;j++){const auto& v=s.segments[j];JsonObject entry=segments.add<JsonObject>();entry["offset"]=v.offset;entry["count"]=v.count;entry["effect"]=LedEffects::names[v.effect];entry["brightness"]=v.brightness;entry["effectSpeed"]=v.speed;entry["red"]=v.red;entry["green"]=v.green;entry["blue"]=v.blue;entry["phase"]=v.phase;
#if !defined(ESP8266) && !defined(APP_DISABLE_AUDIO)
   if(v.effect>=10){const auto& audio=v.effect>=13?LedAudio::micSpectrum():LedAudio::streamSpectrum();entry["audioLevel"]=audio.level;for(float b:audio.bands)entry["audioBands"].add(b);}
#endif
  }}
  #if !defined(ESP8266) && !defined(APP_DISABLE_AUDIO)
  if(s.effect>=10){const auto& audio=s.effect>=13?LedAudio::micSpectrum():LedAudio::streamSpectrum();item["audioLevel"]=audio.level;for(float b:audio.bands)item["audioBands"].add(b);}
#endif
  item["bootIndicator"]=bootActive()?boot.mode:0;item["bootPhase"]=uint32_t(millis()-boot.since);
  item["phase"]=s.phase;item["age"]=uint32_t(millis()-s.lastFrame);item["running"]=s.on||bootActive();
 }
}

}
