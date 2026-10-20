#pragma once
#include <stdint.h>
#include <cmath>

// Diagnostics are audio-owner data. Read/reset with SuntonAudioLock held.
struct SuntonDacCounters {
    uint32_t filterClips=0, nonFinite=0, driverErrors=0, shortWrites=0, framesWritten=0;
    uint32_t emptyInputObservations=0, minInputBytes=UINT32_MAX;
    uint32_t maxServiceUs=0, maxServiceGapUs=0, preGainPeak=0;
};
inline SuntonDacCounters& elmaDacCounters(){static SuntonDacCounters value;return value;}
inline int16_t elmaSaturateAudioPcm(float value,bool internal){
#if APP_SUNTON_PANEL == 3
    if(internal){
        if(!std::isfinite(value)){++elmaDacCounters().nonFinite;return 0;}
        if(value>32767){++elmaDacCounters().filterClips;return 32767;}
        if(value<-32768){++elmaDacCounters().filterClips;return -32768;}
    }
#else
    (void)internal;
#endif
    return int16_t(value);
}
enum class SuntonDacMode:uint8_t {Raw,Round,Tpdf,Feedback};
inline SuntonDacMode& elmaDacMode(){static auto mode=SuntonDacMode::Raw;return mode;}
inline bool& elmaAudioTimingLog(){static bool enabled=false;return enabled;}

// Experimental conversion modes stay OFF by default. Native clock and one
// frame per source sample are preserved in every mode; no correction LUT.
struct SuntonDacQuality {
    int32_t error[2]={0,0};uint32_t random=0x7451a9c3U;
    void reset(){error[0]=error[1]=0;}
    uint8_t quantize(int32_t sample,unsigned channel,SuntonDacMode mode){
        const bool feedback=mode==SuntonDacMode::Feedback;
        const int32_t wanted=sample+(feedback?error[channel]:0);
        int32_t noise=0;
        if(mode==SuntonDacMode::Tpdf && wanted>=-32640 && wanted<=32384){
            random^=random<<13;random^=random>>17;random^=random<<5;
            // [-127,127] PCM units: approximately one DAC LSB peak-to-peak.
            noise=(int32_t(random&255)-int32_t((random>>8)&255))/2;
        }
        int32_t code=(wanted+noise+128)>>8;
        if(code<-128)code=-128;
        if(code>127)code=127;
        error[channel]=feedback?wanted-code*256:0;
        if(error[channel]<-128)error[channel]=-128;
        if(error[channel]>128)error[channel]=128;
        return uint8_t(code+128);
    }
    uint32_t convert(uint32_t pcm,SuntonDacMode mode=SuntonDacMode::Feedback){
        if(!pcm){reset();return 0x80008000U;}
        if(mode==SuntonDacMode::Raw)return pcm^0x80008000U;
        return (uint32_t(quantize(int16_t(pcm>>16),0,mode))<<24)
             | (uint32_t(quantize(int16_t(pcm),1,mode))<<8);
    }
};
