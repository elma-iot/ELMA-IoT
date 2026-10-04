#pragma once
#include <atomic>
#include <cstdint>
#include <cmath>
#include <algorithm>
// Playback only publishes PCM. It never waits, allocates, analyses or renders.
// A full mailbox is deliberately dropped; speaker samples are never modified.
namespace LedAudio {
constexpr unsigned sampleCount=512,bandCount=8;
struct Tap {
 std::atomic<unsigned> ready{0},enabled{0};
 int16_t samples[sampleCount]{};unsigned used=0,rate=44100;
 void push(int16_t left,int16_t right,unsigned sampleRate){
  if(!enabled.load(std::memory_order_relaxed)){used=0;return;}
  if(ready.load(std::memory_order_acquire))return;
  if(!used)rate=std::max(1u,sampleRate);
  samples[used++]=int16_t((int32_t(left)+right)/2);
  if(used==sampleCount){used=0;ready.store(1,std::memory_order_release);}
 }
};
static_assert(ATOMIC_INT_LOCK_FREE==2,"Audio visual mailbox requires lock-free atomics");
inline Tap& playback(){static Tap tap;return tap;}
inline Tap& microphone(){static Tap tap;return tap;}
struct Spectrum {float bands[bandCount]{},level=0;uint32_t updated=0;};
inline Spectrum& streamSpectrum(){static Spectrum value;return value;}
inline Spectrum& micSpectrum(){static Spectrum value;return value;}
inline void analyse(Tap& tap,Spectrum& out,uint32_t now){
 if(uint32_t(now-out.updated)<40)return;
 if(!tap.ready.load(std::memory_order_acquire)){
  if(uint32_t(now-out.updated)>200){for(auto& b:out.bands)b=0;out.level=0;}
  return;
 }
 constexpr float frequencies[]={125,250,500,1000,2000,4000,8000,12000};
 float energy=0,mean=0;for(auto sample:tap.samples)mean+=sample;mean/=sampleCount;
 for(auto sample:tap.samples){float x=(sample-mean)/32768.f;energy+=x*x;}
 out.level=std::min(1.f,std::sqrt(energy/sampleCount)*4.f);
 for(unsigned band=0;band<bandCount;band++){
  float amplitude=0;
  if(tap.rate>2*frequencies[band]){
   const unsigned bin=std::max(1u,unsigned(frequencies[band]*sampleCount/tap.rate+.5f));
   const float coefficient=2*std::cos(6.28318530718f*bin/sampleCount);
   float q1=0,q2=0;
   for(auto sample:tap.samples){float q=(sample-mean)/32768.f+coefficient*q1-q2;q2=q1;q1=q;}
   amplitude=2*std::sqrt(std::max(0.f,q1*q1+q2*q2-coefficient*q1*q2))/sampleCount;
  }
  float level=std::max(0.f,std::min(1.f,(20*std::log10(std::max(amplitude,.0001f))+60)/60));
  out.bands[band]=std::max(level,out.bands[band]*.8f);
 }
 out.updated=now;tap.ready.store(0,std::memory_order_release);
}
#if defined(ESP32) && !defined(APP_DISABLE_AUDIO)
}
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <Arduino.h>
namespace LedAudio {
struct AnalysisFrame {Spectrum stream,mic;};
inline QueueHandle_t& results(){static QueueHandle_t q=nullptr;return q;}
inline void analysisWorker(void*){
 AnalysisFrame frame;
 for(;;){uint32_t now=millis();analyse(playback(),frame.stream,now);analyse(microphone(),frame.mic,now);xQueueOverwrite(results(),&frame);vTaskDelay(pdMS_TO_TICKS(40));}
}
inline void service(uint32_t){
 if(!results()&&(playback().enabled.load()||microphone().enabled.load())){
  results()=xQueueCreate(1,sizeof(AnalysisFrame));
  if(results()&&xTaskCreate(analysisWorker,"led-spectrum",2048,nullptr,tskIDLE_PRIORITY,nullptr)!=pdPASS){vQueueDelete(results());results()=nullptr;}
 }
 AnalysisFrame frame;if(results()&&xQueueReceive(results(),&frame,0)==pdTRUE){streamSpectrum()=frame.stream;micSpectrum()=frame.mic;}
}
#else
inline void service(uint32_t now){analyse(playback(),streamSpectrum(),now);analyse(microphone(),micSpectrum(),now);}
#endif
}
