#pragma once
#if defined(ESP32) && !defined(APP_DISABLE_AUDIO)
#include "led_audio.h"
#include <driver/i2s.h>
#include <soc/soc_caps.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
namespace LedMicrophone {
struct Config {int clock=-1,word=-1,data=-1;bool playback=false;bool stereo=false;bool spk=false;};
inline QueueHandle_t& commands(){static QueueHandle_t q=nullptr;return q;}
inline std::atomic<int>& status(){static std::atomic<int> value{0};return value;}
inline std::atomic<bool>& enabled(){static std::atomic<bool> value{true};return value;}
inline std::atomic<unsigned>& sampleRate(){static std::atomic<unsigned> value{16000};return value;}
inline std::atomic<unsigned>& sampleBits(){static std::atomic<unsigned> value{16};return value;}
inline std::atomic<bool>& pdmMode(){static std::atomic<bool> value{false};return value;}
inline std::atomic<unsigned>& channels(){static std::atomic<unsigned> value{1};return value;}
inline std::atomic<int>& gain(){static std::atomic<int> value{1};return value;}
inline std::atomic<int>& peakLeft(){static std::atomic<int> value{0};return value;}
inline std::atomic<int>& peakRight(){static std::atomic<int> value{0};return value;}
// Bounded PCM queue; capture never waits for HTTP or storage.
struct PcmBlock {int16_t samples[128];uint16_t count=0;};
inline std::atomic<uint32_t>& pcmDropped(){static std::atomic<uint32_t> count{0};return count;}
inline QueueHandle_t& pcm(){static StaticQueue_t state;static QueueHandle_t q=[](){auto* memory=static_cast<uint8_t*>(heap_caps_malloc(512*sizeof(PcmBlock),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));return memory?xQueueCreateStatic(512,sizeof(PcmBlock),memory,&state):xQueueCreate(64,sizeof(PcmBlock));}();return q;}
inline std::atomic<uint32_t>& mediaUntil(){static std::atomic<uint32_t> t{0};return t;}
#if defined(APP_SPK_BOARD) || SOC_I2S_NUM == 1
inline constexpr i2s_port_t port=I2S_NUM_0;
#else
inline constexpr i2s_port_t port=I2S_NUM_1;
#endif
inline std::atomic<int>& clockPin(){static std::atomic<int> value{-1};return value;}
inline std::atomic<int>& wordPin(){static std::atomic<int> value{-1};return value;}
inline std::atomic<int>& dataPin(){static std::atomic<int> value{-1};return value;}
inline std::atomic<uint32_t>& rawLeft(){static std::atomic<uint32_t> value{0};return value;}
inline std::atomic<uint32_t>& rawRight(){static std::atomic<uint32_t> value{0};return value;}
inline TaskHandle_t& task(){static TaskHandle_t value=nullptr;return value;}
inline void worker(void*){
 bool installed=false,paused=false,activePdm=false;Config next;int32_t samples[64];int32_t dcLeft=0,dcRight=0;
 for(;;){
  bool changed=xQueueReceive(commands(),&next,0)==pdTRUE;
  if(changed || (next.spk && activePdm!=pdmMode().load())){
   activePdm=next.spk && pdmMode().load();dcLeft=dcRight=0;
   if(installed){i2s_driver_uninstall(port);installed=false;}paused=false;
   status().store(0);
   bool allowed=next.clock>=0&&next.word>=0&&next.data>=0;
#if SOC_I2S_NUM == 1
   allowed=allowed&&!next.playback;
#endif
   if(allowed){
    i2s_config_t config{};config.mode=i2s_mode_t(I2S_MODE_MASTER|I2S_MODE_RX);config.sample_rate=next.spk?16000:32000;config.bits_per_sample=activePdm?I2S_BITS_PER_SAMPLE_16BIT:I2S_BITS_PER_SAMPLE_32BIT;
#if SOC_I2S_SUPPORTS_PDM_RX
    if(activePdm)config.mode=i2s_mode_t(config.mode|I2S_MODE_PDM);
#endif
    config.channel_format=next.stereo?I2S_CHANNEL_FMT_RIGHT_LEFT:I2S_CHANNEL_FMT_ONLY_LEFT;config.communication_format=I2S_COMM_FORMAT_STAND_I2S;config.intr_alloc_flags=ESP_INTR_FLAG_LEVEL1;config.dma_buf_count=4;config.dma_buf_len=256;
    i2s_pin_config_t pins{};pins.bck_io_num=activePdm?I2S_PIN_NO_CHANGE:next.clock;pins.ws_io_num=activePdm?next.clock:next.word;pins.data_out_num=I2S_PIN_NO_CHANGE;pins.data_in_num=next.data;pins.mck_io_num=I2S_PIN_NO_CHANGE;
    if(activePdm)pinMode(next.word,INPUT_PULLDOWN);
    installed=i2s_driver_install(port,&config,0,nullptr)==ESP_OK;
    if(installed&&i2s_set_pin(port,&pins)!=ESP_OK){i2s_driver_uninstall(port);installed=false;}
    clockPin()=next.clock;wordPin()=next.word;dataPin()=next.data;sampleRate()=config.sample_rate;sampleBits()=activePdm?16:32;channels()=next.stereo?2:1;
    status().store(installed?1:-1);
   }else if(next.data>=0)status().store(-2);
  }
  size_t received=0;
  if(installed&&paused==enabled().load()){paused=!enabled().load();if(paused)i2s_stop(port);else i2s_start(port);}
  if(paused){peakLeft()=0;peakRight()=0;}
  if(installed&&!paused&&i2s_read(port,samples,sizeof(samples),&received,pdMS_TO_TICKS(10))==ESP_OK){
   rawLeft()=activePdm?uint16_t(reinterpret_cast<int16_t*>(samples)[0]):uint32_t(samples[0]);rawRight()=activePdm?uint16_t(reinterpret_cast<int16_t*>(samples)[1]):uint32_t(samples[next.stereo&&received>=8?1:0]);
   int l=0,r=0,multiplier=gain().load();PcmBlock block;
   const unsigned words=received/(activePdm?sizeof(int16_t):sizeof(int32_t));
   for(unsigned i=0;i<words;i+=next.stereo?2:1){
    int16_t left=activePdm?reinterpret_cast<int16_t*>(samples)[i]:int16_t(samples[i]>>16);
    int16_t right=next.stereo&&i+1<words?(activePdm?reinterpret_cast<int16_t*>(samples)[i+1]:int16_t(samples[i+1]>>16)):left;
    dcLeft+=(int32_t(left)*256-dcLeft)/256;dcRight+=(int32_t(right)*256-dcRight)/256;
    left=constrain(int(left)-dcLeft/256,-32768,32767);right=constrain(int(right)-dcRight/256,-32768,32767);
    left=constrain(int(left)*multiplier,-32768,32767);right=constrain(int(right)*multiplier,-32768,32767);
    l=std::max(l,abs(int(left)));r=std::max(r,abs(int(right)));
    LedAudio::microphone().push(left,right,sampleRate().load());
    block.samples[block.count++]=int16_t((int(left)+int(right))/2);
   }
   peakLeft()=l;peakRight()=r;
   if(pcm()&&int32_t(mediaUntil().load()-millis())>0&&block.count&&xQueueSend(pcm(),&block,0)!=pdTRUE)++pcmDropped();
  }
  // A fixed scheduler tick per 64 words discards most 32 kHz samples. Drain
  // DMA continuously while it supplies data; the blocking read yields when
  // empty. Keep the disabled/unconfigured path asleep.
  if(received)taskYIELD();else vTaskDelay(1);
 }
}
inline void configure(Config config){
 if(!commands()){commands()=xQueueCreate(1,sizeof(Config));if(!commands())return;if(xTaskCreate(worker,"led-mic",3072,nullptr,tskIDLE_PRIORITY+2,&task())!=pdPASS){vQueueDelete(commands());commands()=nullptr;return;}}
 xQueueOverwrite(commands(),&config);
}
}
#endif
