#pragma once
#include "update_screen.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace UpdateScreen {
// Small monochrome I2C OLEDs retain the existing GFX renderer. Color panels
// use native-resolution UpdateGraphics coverage and an IRAM scanout callback.
template<class Graphics>
void artwork(Graphics& display,unsigned value,uint32_t now,bool round,uint16_t textColor=1,uint16_t progressColor=1){
    const int width=display.width(),height=display.height();
    const int scale=width>=200 && height>=160?2:1;
    const int titleY=height/2-(height<48?12:16)*scale;
    display.fillScreen(0);display.setTextWrap(false);display.setTextSize(scale);display.setTextColor(textColor);
    display.setCursor((width-8*6*scale)/2,titleY);display.print("Updating");
    display.setCursor((width+8*6*scale)/2,titleY);
    for(unsigned n=0;n<dots(now);++n)display.print('.');
    char label[8];snprintf(label,sizeof(label),"%u%%",percent(value));
    display.setCursor((width-int(strlen(label))*6*scale)/2,height/2+(height<48?0:3)*scale);display.print(label);
    if(round){
        const int cx=width/2,cy=height/2,r=(width<height?width:height)/2-5;
        const int stroke=width>=200?9:4;
        for(unsigned degree=0;degree<arcDegrees(value);++degree){
            const float a=(int(degree)-90)*0.01745329252f;
            for(int s=0;s<stroke;++s)display.drawLine(cx+int(cosf(a)*(r-s)),cy+int(sinf(a)*(r-s)),cx+int(cosf(a+0.01745329252f)*(r-s)),cy+int(sinf(a+0.01745329252f)*(r-s)),progressColor);
        }
    }else{
        const int margin=width<100?4:12,barY=height<48?height-7:height*3/4+2,barHeight=height<48?5:7,barWidth=width-margin*2;
        display.drawRect(margin,barY,barWidth,barHeight,progressColor);
        display.fillRect(margin+1,barY+1,(barWidth-2)*percent(value)/100,barHeight-2,progressColor);
    }
}
}
