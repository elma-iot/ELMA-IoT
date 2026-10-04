#pragma once
#include <Adafruit_NeoPixel.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <atomic>
// Transmission runs below the Arduino/audio loop priority. No LED transmission
// waits in the decoder loop: a saturated queue drops the visual frame instead.
namespace LedOutput {
constexpr unsigned maximumPixels=256;
struct Frame {uint16_t count;int16_t pin;uint8_t slot;uint32_t ticket;uint8_t bytes[maximumPixels*3];};
inline QueueHandle_t* queues(){static QueueHandle_t value[4]{};return value;}
inline std::atomic<uint32_t>* completed(){static std::atomic<uint32_t> value[4]{};return value;}
inline void worker(void*){
 Adafruit_NeoPixel outputs[4];Frame frame;
 for(;;){for(unsigned slot=0;slot<4;slot++)if(xQueueReceive(queues()[slot],&frame,0)==pdTRUE){
  auto& output=outputs[frame.slot];
  if(output.numPixels()!=frame.count){output.updateType(NEO_GRB+NEO_KHZ800);output.updateLength(frame.count);}
  if(!output.getPixels())continue;
  if(output.getPin()!=frame.pin){output.setPin(frame.pin);output.begin();}
  memcpy(output.getPixels(),frame.bytes,frame.count*3);output.show();completed()[frame.slot].store(frame.ticket,std::memory_order_release);
 }vTaskDelay(1); }
}
inline bool begin(){
 if(queues()[0])return true;
 for(unsigned i=0;i<4;i++)if(!(queues()[i]=xQueueCreate(1,sizeof(Frame)))){for(unsigned j=0;j<i;j++){vQueueDelete(queues()[j]);queues()[j]=nullptr;}return false;}
 if(xTaskCreate(worker,"led-tx",3072,nullptr,tskIDLE_PRIORITY,nullptr)!=pdPASS){for(auto i=0;i<4;i++){vQueueDelete(queues()[i]);queues()[i]=nullptr;}return false;}
 return true;
}
class Pixels:public Adafruit_NeoPixel {
 uint8_t slot_;
 uint32_t ticket_=0;
public:
 Pixels(){static uint8_t next=0;slot_=next++;}
 void show(){
  if(slot_>=4||numPixels()>maximumPixels||!LedOutput::begin())return;
  Frame frame{};frame.slot=slot_;frame.pin=getPin();frame.count=numPixels();frame.ticket=++ticket_;
  if(frame.count)memcpy(frame.bytes,getPixels(),frame.count*3);
  xQueueOverwrite(queues()[slot_],&frame);
 }
 // Only used during orderly shutdown, never in the audio decoding loop.
 bool showAndWait(){
  show();if(slot_>=4||!queues()[slot_])return false;
  uint32_t started=millis();
  while(completed()[slot_].load(std::memory_order_acquire)!=ticket_){if(uint32_t(millis()-started)>=250)return false;delay(1);}
  delayMicroseconds(300);return true;
 }
};
}
