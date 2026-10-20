#pragma once
#include <Arduino.h>
#include <driver/i2s.h>
#include "sunton_dac_quality.h"
#if defined(CONFIG_IDF_TARGET_ESP32)
#include <driver/dac.h>
#include <driver/rtc_io.h>
#include <soc/gpio_sig_map.h>
#include <rom/gpio.h>
#endif

// Scale signed PCM before the DAC bias is added; never attenuate the bias.
inline uint32_t elmaScaleDacPcm(uint32_t sample,uint8_t percent) {
    const int32_t left=int16_t(sample>>16),right=int16_t(sample);
    return (uint32_t(uint16_t(left*percent/100))<<16)|uint16_t(right*percent/100);
}

inline SuntonDacQuality& elmaDacQuality(){static SuntonDacQuality quality;return quality;}
// Keep the native decoder clock and one output frame per input frame.
// The experimental doubled-rate noise shaper degraded the physical board.
inline uint32_t elmaDacSampleRate(uint32_t rate,bool internal){
    (void)internal;return rate;
}
inline size_t elmaAudioFrameBytes(bool internal){
    (void)internal;return sizeof(uint32_t);
}
inline int elmaAudioDmaLength(int frames,bool internal){
    (void)internal;return frames;
}
inline int elmaWriteAudioSample(i2s_port_t port,uint32_t sample,bool internal,size_t* written,int timeout){
    if(internal){
        // Bias each signed channel independently, without inter-channel carry.
        sample=(uint32_t(uint16_t(uint16_t(sample>>16)^0x8000U))<<16)
              |uint16_t(uint16_t(sample)^0x8000U);
    }
    return i2s_write(port,&sample,sizeof(sample),written,timeout);
}

// Signed PCM zero is 0x8000 after conversion to the ESP32's unsigned DAC.
// Clearing DMA to binary zero instead makes a full-scale DC voltage step.
inline void elmaClearAudioDma(i2s_port_t port,bool internal,int count,int frames) {
#if APP_SUNTON_PANEL && defined(CONFIG_IDF_TARGET_ESP32)
    if(internal) {
        uint32_t silence[64];
        for(auto& frame:silence)frame=0x80008000U;
        size_t remaining=size_t(count)*size_t(frames)*sizeof(uint32_t);
        while(remaining) {
            size_t written=0,bytes=min(remaining,sizeof(silence));
            if(i2s_write(port,silence,bytes,&written,pdMS_TO_TICKS(100))!=ESP_OK || !written)break;
            remaining-=written;
        }
        return;
    }
#else
    (void)internal;(void)count;(void)frames;
#endif
    i2s_zero_dma_buffer(port);
}

inline bool elmaAudioAutoClear(bool internal) {
#if APP_SUNTON_PANEL && defined(CONFIG_IDF_TARGET_ESP32)
    // Repeating midpoint silence is correct; auto-clear would replace it by 0V.
    return !internal;
#else
    (void)internal;return true;
#endif
}

// The fitted analogue amplifier has no mute GPIO. Ground its input while idle
// instead of leaving the powered DAC or a floating GPIO connected to it.
inline bool elmaDacAudible=false;
inline void elmaGateSuntonDac(bool audible) {
#if APP_SUNTON_PANEL && defined(CONFIG_IDF_TARGET_ESP32)
    if(audible==elmaDacAudible)return;
    dac_i2s_disable();
    dac_output_enable(DAC_CHANNEL_2);
    if(audible) {
        for(int level=0;level<=128;level+=2){dac_output_voltage(DAC_CHANNEL_2,level);delay(1);}
        i2s_set_dac_mode(I2S_DAC_CHANNEL_LEFT_EN);
    } else {
        for(int level=128;level>=0;level-=2){dac_output_voltage(DAC_CHANNEL_2,level);delay(1);}
        dac_output_disable(DAC_CHANNEL_2);
        rtc_gpio_deinit(GPIO_NUM_26);
        gpio_matrix_out(26,SIG_GPIO_OUT_IDX,false,false);
        digitalWrite(26,LOW);pinMode(26,OUTPUT);
    }
    elmaDacAudible=audible;
#else
    (void)audible;
#endif
}

#if defined(CONFIG_IDF_TARGET_ESP32)
inline void elmaStartDacOutput(i2s_port_t port,i2s_dac_mode_t mode,int count,int frames) {
#if APP_SUNTON_PANEL
    elmaDacQuality().reset();
    i2s_set_dac_mode(I2S_DAC_CHANNEL_DISABLE);
    elmaClearAudioDma(port,true,count,frames);
    dac_i2s_disable();
    // Start grounded. Playback gain unmutes only after prebuffering.
    dac_output_disable(DAC_CHANNEL_2);
    rtc_gpio_deinit(GPIO_NUM_26);
    gpio_matrix_out(26,SIG_GPIO_OUT_IDX,false,false);
    digitalWrite(26,LOW);pinMode(26,OUTPUT);
    elmaDacAudible=false;
#else
    (void)port;(void)count;(void)frames;
#endif
#if !APP_SUNTON_PANEL
    i2s_set_dac_mode(mode);
#else
    (void)mode;
#endif
}
#endif
