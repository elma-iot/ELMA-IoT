#include "panel_update.h"
#if APP_HAS_ONBOARD_PANEL
#include <esp_lcd_panel_ops.h>
PanelUpdate::~PanelUpdate(){
#if APP_ROTARY_HMI
    if(rgb_)esp_lcd_panel_del(rgb_); // Stop the ISR before freeing its DRAM.
#endif
}
bool PanelUpdate::allocate(){
    graphics_.reset(new(std::nothrow) UpdateGraphics());
#if APP_ROTARY_HMI
    constexpr bool round=true;
#else
    constexpr bool round=false;
#endif
    return graphics_&&graphics_->begin(kPanelWidth,kPanelHeight,round);
}
bool PanelUpdate::render(unsigned value,uint32_t now){return graphics_&&graphics_->render(value,now);}
#if APP_ROTARY_HMI
bool IRAM_ATTR PanelUpdate::fill(esp_lcd_panel_handle_t,void* output,int position,int length,void* context){
    UpdateGraphics::fill(context,output,position,length);return false;
}
bool PanelUpdate::beginRgb(esp_lcd_rgb_panel_config_t config){
    config.flags.fb_in_psram=0;config.flags.no_fb=1;config.flags.double_fb=0;
    config.num_fbs=0;config.bounce_buffer_size_px=kPanelWidth*10;
    if(esp_lcd_new_rgb_panel(&config,&rgb_)!=ESP_OK)return false;
    esp_lcd_rgb_panel_event_callbacks_t events{};events.on_bounce_empty=fill;
    if(esp_lcd_rgb_panel_register_event_callbacks(rgb_,&events,graphics_->scanout())!=ESP_OK)return false;
    return esp_lcd_panel_init(rgb_)==ESP_OK;
}
#endif
#endif
