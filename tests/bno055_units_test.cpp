#include "../src/bno055_units.h"
#include <cassert>
#include <cmath>
int main(){
 uint8_t bytes[24]{};
 auto put=[&](int offset,int16_t value){bytes[offset]=uint8_t(value);bytes[offset+1]=uint16_t(value)>>8;};
 put(0,981);put(2,-981);put(4,0);put(6,160);put(8,-32);put(12,-1440);
 put(18,5759);put(20,-720);put(22,1440);
 auto s=Bno055Units::decode(bytes);
 assert(std::fabs(s.accel[0]-9.81f)<0.0001f&&std::fabs(s.accel[1]+9.81f)<0.0001f);
 assert(s.mag[0]==10&&s.mag[1]==-2&&s.gyro[0]==-90);
 assert(s.heading==359.9375f&&s.roll==-45&&s.pitch==90);
 put(0,-32768);put(2,32767);s=Bno055Units::decode(bytes);
 assert(std::fabs(s.accel[0]+327.68f)<0.0001f&&std::fabs(s.accel[1]-327.67f)<0.0001f);
}
