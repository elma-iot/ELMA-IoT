#pragma once
#if defined(ESP32) && !defined(APP_DISABLE_AUDIO)
#include "led_audio.h"
#include <driver/i2s.h>
#include <soc/soc_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
namespace LedMicrophone {
struct Config {int clock=-1,word=-1,data=-1;bool playback=false;};
inline QueueHandle_t& commands(){static QueueHandle_t q=nullptr;return q;}
inline std::atomic<int>& status(){static std::atomic<int> value{0};return value;}
inline void worker(void*){
#if SOC_I2S_NUM > 1
 constexpr i2s_port_t port=I2S_NUM_1;
#else
 constexpr i2s_port_t port=I2S_NUM_0;
#endif
 bool installed=false;Config next;int32_t samples[64];
 for(;;){
  if(xQueueReceive(commands(),&next,0)==pdTRUE){
   if(installed){i2s_driver_uninstall(port);installed=false;}
   status().store(0);
   bool allowed=next.clock>=0&&next.word>=0&&next.data>=0;
#if SOC_I2S_NUM == 1
   allowed=allowed&&!next.playback;
#endif
   if(allowed){
    i2s_config_t config{};config.mode=i2s_mode_t(I2S_MODE_MASTER|I2S_MODE_RX);config.sample_rate=32000;config.bits_per_sample=I2S_BITS_PER_SAMPLE_32BIT;config.channel_format=I2S_CHANNEL_FMT_ONLY_LEFT;config.communication_format=I2S_COMM_FORMAT_STAND_I2S;config.intr_alloc_flags=0;config.dma_buf_count=3;config.dma_buf_len=128;
    i2s_pin_config_t pins{};pins.bck_io_num=next.clock;pins.ws_io_num=next.word;pins.data_out_num=I2S_PIN_NO_CHANGE;pins.data_in_num=next.data;pins.mck_io_num=I2S_PIN_NO_CHANGE;
    installed=i2s_driver_install(port,&config,0,nullptr)==ESP_OK;
    if(installed&&i2s_set_pin(port,&pins)!=ESP_OK){i2s_driver_uninstall(port);installed=false;}
    status().store(installed?1:-1);
   }else if(next.data>=0)status().store(-2);
  }
  size_t received=0;
  if(installed&&i2s_read(port,samples,sizeof(samples),&received,0)==ESP_OK)
   for(unsigned i=0;i<received/sizeof(int32_t);i++){int16_t value=int16_t(samples[i]>>16);LedAudio::microphone().push(value,value,32000);}
  vTaskDelay(1);
 }
}
inline void configure(Config config){
 if(!commands()){commands()=xQueueCreate(1,sizeof(Config));if(!commands())return;if(xTaskCreate(worker,"led-mic",2048,nullptr,tskIDLE_PRIORITY,nullptr)!=pdPASS){vQueueDelete(commands());commands()=nullptr;return;}}
 xQueueOverwrite(commands(),&config);
}
}
#endif
