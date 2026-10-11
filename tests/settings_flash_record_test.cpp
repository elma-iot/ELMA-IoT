#include <string>
#include <cassert>
#include <iostream>
#include "settings_flash_record.h"
int main(){
 using namespace SettingsFlash;
 const std::string payload="{\"settings\":{\"test\":123}}";
 CheckedWriter expected;expected.write(reinterpret_cast<const uint8_t*>("123456789"),9);assert(expected.crc()==0xcbf43926);
 fs::FS fs;auto bytes=std::make_shared<std::vector<uint8_t>>();fs.files[paths[0]]=bytes;File file(bytes);
 Header header{magic,1,static_cast<uint32_t>(payload.size()),0};file.write(reinterpret_cast<uint8_t*>(&header),sizeof(header));
 CheckedWriter writer(file);writer.write(reinterpret_cast<const uint8_t*>(payload.data()),payload.size());header.crc=writer.crc();file.seek(0);file.write(reinterpret_cast<uint8_t*>(&header),sizeof(header));
 Header read;assert(inspect(fs,paths[0],read));assert(read.crc==header.crc);
 auto good=*bytes;
 bytes->back()^=1;assert(!inspect(fs,paths[0],read));*bytes=good;
 bytes->resize(bytes->size()-1);assert(!inspect(fs,paths[0],read));*bytes=good;
 bytes->push_back(0);assert(!inspect(fs,paths[0],read));*bytes=good;
 (*bytes)[0]=0;assert(!inspect(fs,paths[0],read));*bytes=good;
 fs.files[paths[1]]=std::make_shared<std::vector<uint8_t>>(good.begin(),good.begin()+sizeof(Header)+3);
 assert(!inspect(fs,paths[1],read));assert(inspect(fs,paths[0],read));
 std::cout<<"PASS CRC32 known vector, exact readback, corrupt/truncated/extra bytes, invalid header and interrupted inactive record preserves active record\n";
}
