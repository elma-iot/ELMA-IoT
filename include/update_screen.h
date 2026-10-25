#pragma once
#include <stdint.h>

namespace UpdateScreen {
constexpr uint8_t percent(unsigned value) { return value > 100 ? 100 : value; }
constexpr unsigned arcDegrees(unsigned value) { return percent(value) * 360U / 100U; }
constexpr unsigned dots(uint32_t milliseconds) { return milliseconds / 400U % 4U; }
constexpr uint16_t blue = 0x2ddf; // RGB565, close to the clock's blue accent.
// Text is confined to the middle half. All lit pixels outside it are progress.
constexpr bool progressPixel(int x,int y,int width,int height) {
    return x < width/8 || x >= width*7/8 || y < height/3 || y >= height*2/3;
}
#if defined(__GNUC__)
#define ELMA_UPDATE_INLINE inline __attribute__((always_inline))
#else
#define ELMA_UPDATE_INLINE __forceinline
#endif
// The ISR passes a byte count, whereas its position is measured in pixels.
// Force-inline: this routine must stay in the caller's IRAM, including at -Os.
template<int Width,int Height>
ELMA_UPDATE_INLINE void expandRgbPairs(const uint8_t* page,uint32_t* destination,int position,int lengthBytes){
    const int end=position+lengthBytes/int(sizeof(uint16_t));
    for(int at=position;at<end;at+=2){
        const int y=at/(Width*2)/2,x=(at%(Width*2))/2;
        const bool lit=(page[y*((Width+7)/8)+(x>>3)]&(0x80>>(x&7)))!=0;
        const uint32_t color=!lit?0:((x<Width/8||x>=Width*7/8||y<Height/3||y>=Height*2/3)?blue:0xffff);
        *destination++=color|(color<<16);
    }
}
#undef ELMA_UPDATE_INLINE
}
