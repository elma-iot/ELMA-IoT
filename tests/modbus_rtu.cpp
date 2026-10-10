#include "../src/modbus_rtu.h"
#include <cassert>
#include <cstring>
int main(){
 uint8_t request[8];ModbusRtu::readRequest(request,1,3,0,10);const uint8_t expected[]={1,3,0,0,0,10,0xc5,0xcd};assert(!memcmp(request,expected,8));
 uint8_t reply[]={1,3,4,0,42,0x12,0x34,0,0};auto crc=ModbusRtu::crc(reply,7);reply[7]=crc;reply[8]=crc>>8;
 assert(ModbusRtu::validReply(reply,9,1,3,2));assert(!ModbusRtu::validReply(reply,9,2,3,2));assert(!ModbusRtu::validReply(reply,9,1,4,2));assert(!ModbusRtu::validReply(reply,9,1,3,1));
 for(int n=0;n<9;n++)assert(!ModbusRtu::validReply(reply,n,1,3,2));reply[4]^=1;assert(!ModbusRtu::validReply(reply,9,1,3,2));
 uint8_t bits[]={3,1,2,0xaa,1,0,0};crc=ModbusRtu::crc(bits,5);bits[5]=crc;bits[6]=crc>>8;assert(ModbusRtu::validReply(bits,7,3,1,9));assert(!ModbusRtu::validReply(bits,7,3,1,8));
}
