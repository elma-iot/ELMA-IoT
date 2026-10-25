#pragma once
#include <stdint.h>
#include <stddef.h>
#if defined(ESP_PLATFORM)
#include <esp_attr.h>
#else
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif
#endif

// Native-resolution AA artwork. Only glyph coverage and perimeter spans are
// retained in DRAM, rather than a full color framebuffer or an LVGL widget tree.
class UpdateGraphics {
public:
    UpdateGraphics()=default;
    ~UpdateGraphics();
    UpdateGraphics(const UpdateGraphics&)=delete;
    UpdateGraphics& operator=(const UpdateGraphics&)=delete;
    bool begin(int width,int height,bool round);
    bool render(unsigned percent,uint32_t now);
    void row(uint16_t* output,int y);
    void* scanout()const{return state_;}
    static void IRAM_ATTR fill(void* state,void* output,int positionPixels,int lengthBytes);
    size_t allocatedBytes()const{return bytes_;}
private:
    struct State;
    State* state_=nullptr;
    size_t bytes_=0;
    uint32_t lastDraw_=0,transitionAt_=0;
    unsigned percent_=101,dots_=4;
    int from_=0,target_=0,displayed_=0;
};
