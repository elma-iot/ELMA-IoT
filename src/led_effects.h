#pragma once
#include <stdint.h>
#include <math.h>
#include <string.h>
// Established patterns: Adafruit NeoPixel strandtest (wipe, theater chase,
// rainbow) and WLED effect descriptions (blink, breathe, scan, colorloop).
// Cooperative deterministic frames; no delays or random global state.
namespace LedEffects {
static const char* const names[]={"solid","blink","breathe","chase","rainbow","color-wipe","theater-chase","theater-chase-rainbow","colorloop","scan"};
inline int effectId(const char* name){for(int i=0;i<10;i++)if(!strcmp(name,names[i]))return i;return -1;}
inline uint32_t pixel(int effect,unsigned i,unsigned count,uint32_t elapsed,unsigned speed,unsigned brightness,unsigned red,unsigned green,unsigned blue){
 const uint32_t stepMs=300-speed*275/100,step=elapsed/stepMs;
 const uint32_t cycle=12000-speed*100;
 double gain=brightness/100.;
 if(effect==1)gain*=((elapsed/(1000-speed*8))%2)==0;
 else if(effect==2)gain*=.5-.5*cos(6.283185307179586*(elapsed%cycle)/cycle);
 else if(effect==3)gain*=i==step%count;
 else if(effect==5){unsigned phase=step%(count*2);gain*=phase<count?i<=phase:i>phase-count;}
 else if(effect==6||effect==7)gain*=(i+step)%3==0;
 else if(effect==9){unsigned length=count>1?2*(count-1):1,pos=step%length;if(pos>=count)pos=length-pos;gain*=i==pos;}
 if(effect==4||effect==7||effect==8){
  unsigned hue=((elapsed%cycle)*1536/cycle+(effect==8?0:i*1536/count))%1536;
  unsigned sector=hue/256,x=hue%256;
  switch(sector){case 0:red=255;green=x;blue=0;break;case 1:red=255-x;green=255;blue=0;break;case 2:red=0;green=255;blue=x;break;case 3:red=0;green=255-x;blue=255;break;case 4:red=x;green=0;blue=255;break;default:red=255;green=0;blue=255-x;}
 }
 return (uint32_t(red*gain+.5)<<16)|(uint32_t(green*gain+.5)<<8)|uint32_t(blue*gain+.5);
}
}
