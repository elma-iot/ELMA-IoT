#if APP_HAS_ONBOARD_PANEL
#include "panel_boot.h"
#include "panel_display.h"
#include "panel_boot_logo.h"
#include <cstring>

void drawPanelBootLogo(PanelDisplay& panel) {
    // Decode one row from the flash-resident indexed artwork. No full RGB buffer.
    uint8_t row[kPanelWidth*2];
    const int left=(kPanelWidth-128)/2,top=(kPanelHeight-128)/2;
    for(int y=0;y<kPanelHeight;++y){
        for(int x=0;x<kPanelWidth;++x){
            unsigned index=0;
            if(x>=left&&x<left+128&&y>=top&&y<top+128){
                unsigned pixel=(y-top)*128+x-left;
                index=(elmaBootPixels[16+pixel/4]>>(6-2*(pixel%4)))&3;
            }
            const auto* color=elmaBootPixels+index*4;
            uint16_t rgb=((color[2]&248)<<8)|((color[1]&252)<<3)|(color[0]>>3);
#if APP_ROTARY_HMI
            row[x*2]=rgb&255;row[x*2+1]=rgb>>8;
#else
            row[x*2]=rgb>>8;row[x*2+1]=rgb&255;
#endif
        }
        if(!panel.drawColor(0,y,kPanelWidth,1,row))break;
    }
}

#endif
