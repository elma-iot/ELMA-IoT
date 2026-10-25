#include "panel_update.h"
#if APP_HAS_ONBOARD_PANEL
#include <cmath>
#include <esp_lcd_panel_ops.h>

PanelUpdate::~PanelUpdate(){
#if APP_ROTARY_HMI
    if(rgb_)esp_lcd_panel_del(rgb_); // Stop the ISR before freeing its DRAM.
#endif
    if(scan_){for(auto* page:scan_->pages)heap_caps_free(page);heap_caps_free(scan_);}
}
bool PanelUpdate::allocate(){
    scan_=static_cast<Scanout*>(heap_caps_calloc(1,sizeof(Scanout),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    if(!scan_)return false;
    for(auto& page:scan_->pages){page=static_cast<uint8_t*>(heap_caps_calloc(1,pageBytes,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));if(!page)return false;}
    return true;
}
bool PanelUpdate::render(unsigned value,uint32_t now,bool round){
    value=UpdateScreen::percent(value);const unsigned dots=UpdateScreen::dots(now);
    if(value==lastPercent_ && dots==lastDots_)return false;
    if(lastPercent_!=101 && now-lastDraw_<100)return false;
    const uint32_t page=scan_->pending^1U;
#if APP_ROTARY_HMI
    // The RGB ISR acknowledges the page at the next frame boundary.
    if(rgb_ && page==scan_->scanning)return false;
#endif
    canvas_.page(scan_->pages[page]);canvas_.fillScreen(0);canvas_.setTextWrap(false);
    const uint8_t scale=width>=200?2:1;
    auto text=[&](const char* label,int y){canvas_.setTextColor(1);canvas_.setTextSize(scale);canvas_.setCursor((width-int(strlen(label))*6*scale)/2,y);canvas_.print(label);};
    // Keep the word centered as the dots animate in the space after it.
    const int titleY=height/2-16*scale;
    text("Updating",titleY);canvas_.setCursor((width+8*6*scale)/2,titleY);
    for(unsigned n=0;n<dots;++n)canvas_.print('.');
    char label[8];snprintf(label,sizeof(label),"%u%%",value);text(label,height/2+3*scale);
    if(round){
        const int cx=width/2,cy=height/2,r=width/2-5;
        for(unsigned degree=0;degree<UpdateScreen::arcDegrees(value);++degree){
            const float a=(int(degree)-90)*0.01745329252f;
            for(int stroke=0;stroke<5;++stroke)canvas_.drawLine(cx+int(cosf(a)*(r-stroke)),cy+int(sinf(a)*(r-stroke)),cx+int(cosf(a+0.01745329252f)*(r-stroke)),cy+int(sinf(a+0.01745329252f)*(r-stroke)),1);
        }
    }else{
        const int barY=height*3/4+2,barWidth=width-24;
        canvas_.drawRect(12,barY,barWidth,7,1);
        canvas_.fillRect(13,barY+1,(barWidth-2)*value/100,5,1);
    }
    __atomic_store_n(&scan_->pending,page,__ATOMIC_RELEASE);
    lastDraw_=now;lastPercent_=value;lastDots_=dots;return true;
}
#if APP_ROTARY_HMI
bool IRAM_ATTR PanelUpdate::fill(esp_lcd_panel_handle_t,void* output,int position,int length,void* context){
    auto* state=static_cast<Scanout*>(context);
    if(position==0)state->scanning=__atomic_load_n(&state->pending,__ATOMIC_ACQUIRE);
    UpdateScreen::expandRgbPairs<width,height>(state->pages[state->scanning],static_cast<uint32_t*>(output),position,length);
    return false;
}
bool PanelUpdate::beginRgb(esp_lcd_rgb_panel_config_t config){
    config.flags.fb_in_psram=0;config.flags.no_fb=1;config.flags.double_fb=0;
    config.num_fbs=0;config.bounce_buffer_size_px=kPanelWidth*10;
    if(esp_lcd_new_rgb_panel(&config,&rgb_)!=ESP_OK)return false;
    esp_lcd_rgb_panel_event_callbacks_t events{};events.on_bounce_empty=fill;
    if(esp_lcd_rgb_panel_register_event_callbacks(rgb_,&events,scan_)!=ESP_OK)return false;
    return esp_lcd_panel_init(rgb_)==ESP_OK;
}
#endif
#endif
