#pragma once
#include <cstdint>
#include <cstring>
namespace BuiltinLedEffect {
inline bool frameDue(uint32_t now,uint32_t previous){return int32_t(now-previous)>=20;}
inline bool valid(const char* mode){return mode&&(!std::strcmp(mode,"solid")||!std::strcmp(mode,"blink")||!std::strcmp(mode,"breathe")||!std::strcmp(mode,"colorloop"));}
inline uint32_t color(const char* mode,uint32_t elapsed,unsigned speed,unsigned brightness,unsigned red,unsigned green,unsigned blue){
 speed=speed>100?100:speed;brightness=brightness>100?100:brightness;
 const uint32_t period=12000-speed*100;unsigned gain=255;
 if(!std::strcmp(mode,"blink"))gain=(elapsed/(1000-speed*8))%2?0:255;
 else if(!std::strcmp(mode,"breathe")){unsigned p=(elapsed%period)*510/period,triangle=p<=255?p:510-p;gain=triangle*triangle/255;}
 else if(!std::strcmp(mode,"colorloop")){
  unsigned hue=(elapsed%period)*1536/period,sector=hue/256,x=hue%256;
  switch(sector){case 0:red=255;green=x;blue=0;break;case 1:red=255-x;green=255;blue=0;break;case 2:red=0;green=255;blue=x;break;case 3:red=0;green=255-x;blue=255;break;case 4:red=x;green=0;blue=255;break;default:red=255;green=0;blue=255-x;}
 }
 const unsigned scale=gain*brightness;
 return (uint32_t(red*scale/25500)<<16)|(uint32_t(green*scale/25500)<<8)|uint32_t(blue*scale/25500);
}
}
