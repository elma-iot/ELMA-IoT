#pragma once
#include <cstdint>
namespace Bno055Units {
struct Sample {float accel[3],mag[3],gyro[3],heading,roll,pitch;};
inline int16_t word(const uint8_t* p){return int16_t(uint16_t(p[0])|(uint16_t(p[1])<<8));}
inline Sample decode(const uint8_t* p){Sample s{};for(int i=0;i<3;i++){s.accel[i]=word(p+2*i)/100.f;s.mag[i]=word(p+6+2*i)/16.f;s.gyro[i]=word(p+12+2*i)/16.f;}s.heading=word(p+18)/16.f;s.roll=word(p+20)/16.f;s.pitch=word(p+22)/16.f;return s;}
}
