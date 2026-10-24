#include "lcd_wake_gesture.h"
#include <cassert>
#include <cstdio>
int main(){
 LcdWakeGesture g;
 assert(!g.update(true,100,100,100));assert(!g.update(false,0,0,160));
 assert(!g.update(false,0,0,220));assert(!g.update(true,105,100,250));assert(g.update(false,0,0,310));
 // Single tap, held contact, drag and distant taps must not wake.
 g.reset();g.update(true,10,10,100);g.update(false,0,0,150);g.update(true,10,10,700);assert(!g.update(false,0,0,750));
 g.reset();g.update(true,10,10,100);assert(!g.update(false,0,0,500));g.update(true,10,10,550);assert(!g.update(false,0,0,600));
 g.reset();g.update(true,10,10,100);g.update(true,100,100,130);assert(!g.update(false,0,0,150));g.update(true,10,10,200);assert(!g.update(false,0,0,250));
 g.reset();g.update(true,10,10,100);g.update(false,0,0,150);g.update(true,100,100,200);assert(!g.update(false,0,0,250));
 // Unsigned time arithmetic also works across millis() wrap.
 g.reset();g.update(true,10,10,0xffffff80u);g.update(false,0,0,0xffffffb0u);g.update(true,10,10,10);assert(g.update(false,0,0,60));
 puts("PASS LCD double-tap release, timing, movement and clock wrap");
}
