#include "builtin_led_effect.h"
#include <cassert>
#include <initializer_list>
int main(){
 using namespace BuiltinLedEffect;
 assert(!frameDue(100,101)&&!frameDue(120,101)&&frameDue(121,101)&&frameDue(10,0xfffffff0u));
 assert(valid("solid")&&valid("blink")&&valid("breathe")&&valid("colorloop")&&!valid("bad")&&!valid(nullptr));
 assert(color("solid",0,50,100,255,128,0)==0xff8000);
 assert(color("solid",0,50,0,255,128,0)==0);
 assert(color("blink",0,50,100,255,0,0)==0xff0000);
 assert(color("blink",600,50,100,255,0,0)==0);
 assert(color("blink",1200,50,100,255,0,0)==0xff0000);
 assert(color("breathe",0,50,100,255,0,0)==0);
 assert(color("breathe",3500,50,100,255,0,0)==0xff0000);
 assert(color("colorloop",0,50,100,0,0,0)==0xff0000);
 assert(color("colorloop",3500,50,100,0,0,0)==0x00ffff);
 assert(interval(1000,12000,2000)*10==interval(100,12000,2000));
 assert(interval(1000,1000,200)*10==interval(100,1000,200));
 for(const char* mode:{"blink","breathe","colorloop"})for(uint32_t t:{0u,20u,50u,99u,150u})
  assert(color(mode,t,1000,100,255,128,64)==color(mode,t*10,100,100,255,128,64));
 for(unsigned speed=0;speed<=1000;speed+=25)for(unsigned brightness=0;brightness<=100;brightness+=25)
  for(const char* mode:{"solid","blink","breathe","colorloop"})for(uint32_t t:{0u,1u,10000u,0xfffffff0u}){
   auto c=color(mode,t,speed,brightness,255,255,255);
   assert(((c>>16)&255)<=255*brightness/100&&((c>>8)&255)<=255*brightness/100&&(c&255)<=255*brightness/100);
  }
}
