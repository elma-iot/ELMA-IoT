#pragma once
#include <cstddef>
#include <cstdint>
namespace ModbusRtu {
inline uint16_t crc(const uint8_t* data,size_t size){uint16_t c=0xffff;for(size_t i=0;i<size;i++){c^=data[i];for(int b=0;b<8;b++)c=(c&1)?(c>>1)^0xa001:c>>1;}return c;}
inline void readRequest(uint8_t* out,uint8_t unit,uint8_t function,uint16_t address,uint16_t count){out[0]=unit;out[1]=function;out[2]=address>>8;out[3]=address;out[4]=count>>8;out[5]=count;auto c=crc(out,6);out[6]=c;out[7]=c>>8;}
inline bool validReply(const uint8_t* data,size_t size,uint8_t unit,uint8_t function,uint16_t count){
 if(size<5||data[0]!=unit||data[1]!=function)return false;
 unsigned bytes=function<=2?(count+7)/8:count*2;
 return data[2]==bytes&&size==bytes+5&&crc(data,size-2)==uint16_t(data[size-2]|(data[size-1]<<8));
}
}
