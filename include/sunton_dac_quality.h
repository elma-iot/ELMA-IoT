#pragma once
#include <stdint.h>

// The classic ESP32 DAC consumes only the upper eight bits. Interpolate at
// twice the source rate and feed quantization error forward, rather than
// truncating quiet PCM into a few repeated DAC codes. No heap allocations.
struct SuntonDacQuality {
    int32_t previous[2]={0,0},error[2]={0,0};
    uint32_t random=0x7451a9c3U;
    void reset(){previous[0]=previous[1]=error[0]=error[1]=0;}
    uint8_t quantize(int32_t sample,unsigned channel){
        random^=random<<13;random^=random>>17;random^=random<<5;
        const int32_t dither=int32_t(random&255)-int32_t((random>>8)&255);
        const int32_t wanted=sample+error[channel];
        int32_t code=(wanted+dither+128)>>8;
        if(code<-128)code=-128;if(code>127)code=127;
        error[channel]=wanted-code*256;
        if(error[channel]<-512)error[channel]=-512;if(error[channel]>512)error[channel]=512;
        return uint8_t(code+128);
    }
    void expand(uint32_t pcm,uint32_t (&output)[2]){
        const int32_t current[2]={int16_t(pcm>>16),int16_t(pcm)};
        if(!current[0]&&!current[1]&&!previous[0]&&!previous[1]){
            reset();output[0]=output[1]=0x80008000U;return;
        }
        for(unsigned step=1;step<=2;++step){
            uint32_t word=0;
            for(unsigned channel=0;channel<2;++channel){
                const int32_t value=previous[channel]+(current[channel]-previous[channel])*int32_t(step)/2;
                word|=uint32_t(quantize(value,channel))<<(channel?8:24);
            }
            output[step-1]=word;
        }
        previous[0]=current[0];previous[1]=current[1];
    }
};
