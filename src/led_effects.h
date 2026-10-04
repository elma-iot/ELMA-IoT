#pragma once
#include <stdint.h>
#include <math.h>
#include <string.h>
#if __has_include("elma_led_modules.h")
#include "elma_led_modules.h"
#endif
// Established patterns: Adafruit NeoPixel strandtest (wipe, theater chase,
// rainbow) and WLED effect descriptions (blink, breathe, scan, colorloop).
// Cooperative deterministic frames; no delays or random global state.
namespace LedEffects {
#ifndef ELMA_LED_EFFECT_MASK
#define ELMA_LED_EFFECT_MASK 1023
#endif
inline bool enabled(int effect){return effect>=0&&effect<16&&(ELMA_LED_EFFECT_MASK&(1u<<effect));}
static const char* const names[]={"solid","blink","breathe","chase","rainbow","color-wipe","theater-chase","theater-chase-rainbow","colorloop","scan","stream-spectrum","stream-vu","stream-pulse","mic-spectrum","mic-vu","mic-pulse"};
inline int effectId(const char* name){for(int i=0;i<16;i++)if(enabled(i)&&!strcmp(name,names[i]))return i;return -1;}
inline uint32_t pixel(int effect,unsigned i,unsigned count,uint32_t elapsed,unsigned speed,unsigned brightness,unsigned red,unsigned green,unsigned blue){
 const uint32_t stepMs=300-speed*275/100,step=elapsed/stepMs;
 const uint32_t cycle=12000-speed*100;
 double gain=brightness/100.;
 if(!enabled(effect)||!count||effect>=10)return 0;
 if(enabled(1)&&effect==1)gain*=((elapsed/(1000-speed*8))%2)==0;
 else if(enabled(2)&&effect==2)gain*=.5-.5*cos(6.283185307179586*(elapsed%cycle)/cycle);
 else if(enabled(3)&&effect==3)gain*=i==step%count;
 else if(enabled(5)&&effect==5){unsigned phase=step%(count*2);gain*=phase<count?i<=phase:i>phase-count;}
 else if((enabled(6)&&effect==6)||(enabled(7)&&effect==7))gain*=(i+step)%3==0;
 else if(enabled(9)&&effect==9){unsigned length=count>1?2*(count-1):1,pos=step%length;if(pos>=count)pos=length-pos;gain*=i==pos;}
 if((enabled(4)&&effect==4)||(enabled(7)&&effect==7)||(enabled(8)&&effect==8)){
  unsigned hue=((elapsed%cycle)*1536/cycle+(effect==8?0:i*1536/count))%1536;
  unsigned sector=hue/256,x=hue%256;
  switch(sector){case 0:red=255;green=x;blue=0;break;case 1:red=255-x;green=255;blue=0;break;case 2:red=0;green=255;blue=x;break;case 3:red=0;green=255-x;blue=255;break;case 4:red=x;green=0;blue=255;break;default:red=255;green=0;blue=255-x;}
 }
 return (uint32_t(red*gain+.5)<<16)|(uint32_t(green*gain+.5)<<8)|uint32_t(blue*gain+.5);
}
}
