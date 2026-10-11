#pragma once
#include <stdint.h>
#include <stddef.h>
namespace StorageIntegrity {
inline uint32_t update(uint32_t crc, const uint8_t* data, size_t size) {
    static const uint32_t table[16]={0x00000000U,0x1db71064U,0x3b6e20c8U,0x26d930acU,0x76dc4190U,0x6b6b51f4U,0x4db26158U,0x5005713cU,0xedb88320U,0xf00f9344U,0xd6d6a3e8U,0xcb61b38cU,0x9b64c2b0U,0x86d3d2d4U,0xa00ae278U,0xbdbdf21cU};
    for (size_t i=0;i<size;i++) {
        crc ^= data[i];
        crc=(crc>>4)^table[crc&15];crc=(crc>>4)^table[crc&15];
    }
    return crc;
}
}
#ifndef APP_DISABLE_WEB_UI
#include <ESPAsyncWebServer.h>
#include <functional>
void registerStorageIntegrityRoutes(AsyncWebServer&,std::function<bool(AsyncWebServerRequest*)>);
#endif
