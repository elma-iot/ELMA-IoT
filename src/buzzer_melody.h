#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <driver/gpio.h>
#include "settings_schema.h"
#include "logic_audio_dsp.h"
// Cooperative single-voice buzzer playback. Active devices use timings, passive devices use pitches.
class BuzzerMelody {
 ElmaAudio::PianoNote events_[128];size_t count_=0;int pin_=-1;bool active_=false,running_=false;uint32_t began_=0;int current_=-2;float speed_=1,pitch_=0;
 static int configuredPin(JsonVariantConst source,const SettingsBundle& settings){
  const String slot=source["output"]["slot"] | "";int colon=slot.indexOf(':');if(colon<0)return -1;
  String group=slot.substring(0,colon),number=slot.substring(colon+1);if(group!="audio"&&group!="control")return -1;
  for(unsigned i=0;i<number.length();++i)if(!isDigit(number[i]))return -1;if(number.isEmpty())return -1;int index=number.toInt();
  JsonDocument profiles,bindings;deserializeJson(profiles,settings.ui.peripheralProfileSelections);deserializeJson(bindings,settings.ui.peripheralHelperBindings);
  const char* key=group=="audio"?"audioProfiles":"controls";String profile=profiles[key][index] | "";if(profile!="buzzer")return -1;
  String value=bindings[slot]["SIG"] | "";if(value.isEmpty())return -1;for(unsigned i=0;i<value.length();++i)if(!isDigit(value[i]))return -1;int pin=value.toInt();return GPIO_IS_VALID_OUTPUT_GPIO(pin)?pin:-1;
 }
 public:
 static bool validate(JsonVariantConst source,const SettingsBundle& settings,String& error){
  String kind=source["output"]["kind"] | "";if((kind!="passive"&&kind!="active")||configuredPin(source,settings)<0){error="Select a configured active/passive buzzer with a valid SIG pin";return false;}
  auto melody=source["melody"];auto notes=melody["notes"];if(!source["path"].isNull()||!source["text"].isNull()||!melody.is<JsonObjectConst>()||!notes.is<JsonArrayConst>()||notes.size()>128||!ElmaAudio::PianoSynth::validInstrument(melody["instrument"] | "")){error="Buzzers require a recorded Piano melody";return false;}
  for(const char* key:{"speed","pitch"})if(!source[key].isNull()&&!source[key].is<float>()){error="Buzzer speed/pitch must be numeric";return false;}
  float speed=source["speed"] | 1.f,pitch=source["pitch"] | 0.f;if(!std::isfinite(speed)||speed<.25f||speed>4||!std::isfinite(pitch)||pitch<-24||pitch>24){error="Invalid buzzer speed or pitch";return false;}
  if(!source["equalizer"].isNull()||!source["volume"].isNull()){error="Buzzer output has no equalizer or volume; use a DAC for audio effects";return false;}
  size_t ai=0;for(JsonVariantConst a:notes.as<JsonArrayConst>()){
   if(!a["note"].is<int>()||!a["start"].is<float>()||!a["duration"].is<float>()||!a["velocity"].is<float>()){error="Invalid buzzer note";return false;}
   int note=a["note"];float start=a["start"],duration=a["duration"],velocity=a["velocity"];
   if(note<12||note>108||!std::isfinite(start)||!std::isfinite(duration)||!std::isfinite(velocity)||start<0||duration<=0||start+duration>60||velocity<=0||velocity>1){error="Invalid buzzer note range";return false;}
   for(size_t bi=0;bi<ai;++bi){auto b=notes[bi];if(start < (b["start"].as<float>()+b["duration"].as<float>())-.0001f && b["start"].as<float>() < start+duration-.0001f){error="Buzzers cannot play chords; use a DAC";return false;}}++ai;
  }
  error="";return true;
 }
 void stop(){if(pin_>=0){if(!active_){ledcWriteTone(7,0);ledcDetachPin(pin_);}pinMode(pin_,OUTPUT);digitalWrite(pin_,LOW);}pin_=-1;running_=false;}
 bool begin(JsonVariantConst source,const SettingsBundle& settings,String& error){
  if(!validate(source,settings,error))return false;stop();pin_=configuredPin(source,settings);active_=String(source["output"]["kind"] | "")=="active";speed_=source["speed"] | 1.f;pitch_=source["pitch"] | 0.f;count_=0;
  for(JsonVariantConst n:source["melody"]["notes"].as<JsonArrayConst>()){auto& e=events_[count_++];e.note=n["note"];e.start=n["start"];e.duration=n["duration"];e.velocity=n["velocity"];}
  if(active_)pinMode(pin_,OUTPUT);else{if(!ledcSetup(7,440,10)){error="Buzzer PWM unavailable";stop();return false;}ledcAttachPin(pin_,7);}began_=millis();current_=-2;running_=true;return true;
 }
 void loop(){if(!running_)return;float now=(millis()-began_)*.001f*speed_;int current=-1;float end=0;
  for(size_t i=0;i<count_;++i){auto& e=events_[i];end=e.start+e.duration>end?e.start+e.duration:end;if(now>=e.start&&now<e.start+e.duration)current=i;}
  if(now>=end){stop();return;}if(current==current_)return;current_=current;if(active_)digitalWrite(pin_,current>=0?HIGH:LOW);else ledcWriteTone(7,current>=0?440*std::pow(2.f,(events_[current].note-69+pitch_)/12.f):0);
 }
};
