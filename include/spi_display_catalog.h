#pragma once
// Generated from web/modules/spi-display-catalog.json. Do not hand-edit.
#include <stdint.h>
#include <cstring>
namespace SpiDisplays {
struct Controller {const char* id;const char* driver;uint16_t width,height;uint8_t variant;bool round,monochrome;};
inline constexpr Controller controllers[]={
    {"gc9a01","gc9a01",240,240,0,true,false},
    {"st7789-240","st7789",240,240,0,false,false},
    {"st7789-280","st7789",240,280,0,false,false},
    {"st7789-320","st7789",240,320,0,false,false},
    {"st7789-135","st7789",135,240,0,false,false},
    {"st7735-black","st7735",128,160,2,false,false},
    {"st7735-red","st7735",128,160,3,false,false},
    {"st7735-green","st7735",128,160,0,false,false},
    {"st7735-square","st7735",128,128,1,false,false},
    {"ili9341","ili9341",240,320,0,false,false},
    {"st7796s","st7796s",320,480,0,false,false},
    {"ssd1331","ssd1331",96,64,0,false,false},
    {"ssd1351","ssd1351",128,128,0,false,false},
    {"ssd1351-96","ssd1351",128,96,0,false,false},
    {"ssd1306-spi","ssd1306",128,64,0,false,true},
    {"ssd1306-spi-32","ssd1306",128,32,0,false,true},
    {"sh1106-spi","sh1106",128,64,0,false,true},
};
inline const Controller* controller(const char* id){for(const auto& item:controllers)if(!strcmp(id,item.id))return &item;return nullptr;}
}
