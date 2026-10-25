#pragma once
#if APP_HAS_ONBOARD_PANEL
#include <Arduino.h>
#include <memory>
#include "panel_geometry.h"
#include "update_graphics.h"
#if APP_ROTARY_HMI
#include <esp_lcd_panel_rgb.h>
#endif
class PanelUpdate {
public:
    ~PanelUpdate();
    bool allocate();
    bool render(unsigned percent,uint32_t now);
    void row(uint16_t* output,int y){graphics_->row(output,y);}
#if APP_ROTARY_HMI
    bool beginRgb(esp_lcd_rgb_panel_config_t configuration);
#endif
private:
    std::unique_ptr<UpdateGraphics> graphics_;
#if APP_ROTARY_HMI
    esp_lcd_panel_handle_t rgb_=nullptr;
    static bool IRAM_ATTR fill(esp_lcd_panel_handle_t,void*,int,int,void*);
#endif
};
#endif
