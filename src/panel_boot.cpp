#if APP_HAS_ONBOARD_PANEL
#include "panel_boot.h"
#include "panel_display.h"
#include "panel_boot_logo.h"
#include <cstring>

void drawPanelBootLogo(PanelDisplay& panel) {
    // Decode one row from the flash-resident indexed artwork. No full RGB buffer.
    constexpr int rows=APP_WAVESHARE_AMOLED?2:1;
    uint8_t row[kPanelWidth*2*rows];
    const int scale=kPanelBootLogoScale,size=kPanelBootLogoSize;
    const int left=(kPanelWidth-size)/2,top=(kPanelHeight-size)/2;
    for(int y=0;y<kPanelHeight;y+=rows){
      for(int n=0;n<rows;++n){
        const int pixelY=y+n;
        for(int x=0;x<kPanelWidth;++x){
            unsigned index=0;
            if(x>=left&&x<left+size&&pixelY>=top&&pixelY<top+size){
                unsigned pixel=((pixelY-top)/scale)*128+(x-left)/scale;
                index=(elmaBootPixels[16+pixel/4]>>(6-2*(pixel%4)))&3;
            }
            const auto* color=elmaBootPixels+index*4;
            uint16_t rgb=((color[2]&248)<<8)|((color[1]&252)<<3)|(color[0]>>3);
#if APP_ROTARY_HMI
            row[(n*kPanelWidth+x)*2]=rgb&255;row[(n*kPanelWidth+x)*2+1]=rgb>>8;
#else
            row[(n*kPanelWidth+x)*2]=rgb>>8;row[(n*kPanelWidth+x)*2+1]=rgb&255;
#endif
        }
      }
        if(!panel.drawColor(0,y,kPanelWidth,rows,row))break;
    }
}

#endif
