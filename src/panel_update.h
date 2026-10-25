#pragma once
#if APP_HAS_ONBOARD_PANEL
#include <Adafruit_GFX.h>
#include <esp_heap_caps.h>
#include "panel_geometry.h"
#include "update_screen.h"
#if APP_ROTARY_HMI
#include <esp_lcd_panel_rgb.h>
#endif

// Two tiny monochrome pages, expanded by the RGB ISR without touching flash or
// PSRAM. Pages are switched only at the start of a frame; a writer never changes
// the page being scanned. Text is white, the surrounding progress pixels blue.
class PanelUpdate {
public:
    static constexpr int width=kPanelWidth/2,height=kPanelHeight/2;
    static constexpr size_t pageBytes=((width+7)/8)*height;
    class Canvas:public GFXcanvas1 {
    public:
        Canvas():GFXcanvas1(PanelUpdate::width,PanelUpdate::height,false){}
        void page(uint8_t* pixels){buffer=pixels;buffer_owned=false;}
    };
    struct Scanout {
        uint8_t* pages[2]{};
        volatile uint32_t pending=0,scanning=0;
    };
    ~PanelUpdate();
    bool allocate();
    bool render(unsigned percent,uint32_t now,bool round);
    const uint8_t* pixels()const{return scan_->pages[scan_->pending];}
#if APP_ROTARY_HMI
    bool beginRgb(esp_lcd_rgb_panel_config_t configuration);
#endif
private:
    Canvas canvas_;
    Scanout* scan_=nullptr;
    uint32_t lastDraw_=0;unsigned lastPercent_=101,lastDots_=4;
#if APP_ROTARY_HMI
    esp_lcd_panel_handle_t rgb_=nullptr;
    static bool IRAM_ATTR fill(esp_lcd_panel_handle_t,void*,int,int,void*);
#endif
};
#endif
