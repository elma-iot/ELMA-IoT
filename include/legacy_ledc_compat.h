#pragma once
#include <Arduino.h>
#include <esp_arduino_version.h>
#if ESP_ARDUINO_VERSION_MAJOR >= 3
// Existing ELMA PWM callers address channels. Arduino 3 addresses pins.
// Keep the old contract for those callers only; the LCD uses its native driver.
namespace ElmaLedc {
struct Settings { uint32_t frequency=1000; uint8_t bits=8; int pin=-1; };
inline Settings settings[8];
inline double setup(uint8_t channel,double frequency,uint8_t bits){if(channel>=8||frequency<=0||bits<1||bits>20)return 0;settings[channel].frequency=frequency;settings[channel].bits=bits;return frequency;}
inline void attach(uint8_t pin,uint8_t channel){if(channel<8&&ledcAttachChannel(pin,settings[channel].frequency,settings[channel].bits,channel))settings[channel].pin=pin;}
inline bool write(uint8_t channel,uint32_t duty){return ledcWriteChannel(channel,duty);}
inline double tone(uint8_t channel,double frequency){return channel<8&&settings[channel].pin>=0?ledcWriteTone(settings[channel].pin,frequency):0;}
}
#define ledcSetup ElmaLedc::setup
#define ledcAttachPin ElmaLedc::attach
#define ledcDetachPin ledcDetach
#define ledcWrite ElmaLedc::write
#define ledcWriteTone ElmaLedc::tone
#endif
