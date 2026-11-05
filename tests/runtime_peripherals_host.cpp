#include "runtime_peripherals.h"
#include "led_array.h"
#include <cassert>
#include <iostream>
int main(){
 JsonDocument profiles,helpers,config,original,devices;std::string error;
 original.to<JsonArray>().add<JsonObject>()["type"]="hardware.gpio";
 profiles["controls"][0]="ws2812-neopixel-led-strip";
 helpers["control:0"]["DIN"]="3";helpers["control:0"]["LED_COUNT"]=32;helpers["control:0"]["LED_LAYOUT"]="ring";helpers["control:0"]["LED_DEFAULT_EFFECT"]="chase";
 assert(RuntimePeripherals::validate(profiles.as<JsonObjectConst>(),{},error));
 assert(RuntimePeripherals::reconcile(devices,original.as<JsonArrayConst>(),profiles.as<JsonObjectConst>(),helpers.as<JsonObjectConst>(),config.as<JsonObjectConst>(),error));
 unsigned arrays=0;std::string identity;
 for(JsonObject n:devices.as<JsonArray>())if(LedArrays::matches(n)){
  ++arrays;assert(n["binding"]["pins"]["DIN"]==3);
  assert(LedArrays::updateBinding(n["binding"],helpers["control:0"],error));
  assert(n["binding"]["count"]==32);assert(n["binding"]["defaults"]["effect"]=="chase");
  JsonDocument action;action["action"]="strip";action["configured"]=true;
  assert(LedArrays::action(n,action.as<JsonVariantConst>(),error));identity=n["peripheral"]["id"].as<std::string>();
 }
 assert(arrays==5);assert(!identity.empty());assert(LedArrays::arrays[0].pin==3);assert(LedArrays::arrays[0].count==32);assert(LedArrays::arrays[0].on);
 JsonDocument restored;assert(RuntimePeripherals::reconcile(restored,devices.as<JsonArrayConst>(),profiles.as<JsonObjectConst>(),helpers.as<JsonObjectConst>(),config.as<JsonObjectConst>(),error));
 for(JsonObjectConst n:restored.as<JsonArrayConst>())if(n["binding"]["kind"]=="peripheral")assert(n["peripheral"]["id"]==identity);
 // Restore saved dynamic IDs before validating a persisted graph on reboot.
 JsonDocument cold;assert(RuntimePeripherals::reconcile(cold,original.as<JsonArrayConst>(),profiles.as<JsonObjectConst>(),helpers.as<JsonObjectConst>(),config.as<JsonObjectConst>(),error));
 for(JsonObjectConst n:cold.as<JsonArrayConst>())if(n["binding"]["kind"]=="peripheral")assert(n["peripheral"]["id"]==identity);
 profiles["controls"][0]="servo";assert(!RuntimePeripherals::validate(profiles.as<JsonObjectConst>(),{},error));
 profiles["controls"][0]="none";assert(RuntimePeripherals::reconcile(restored,devices.as<JsonArrayConst>(),profiles.as<JsonObjectConst>(),helpers.as<JsonObjectConst>(),config.as<JsonObjectConst>(),error));assert(restored.size()==1);
 profiles["inputs"][0]="physical-button";helpers["input:0"]["OUT"]=0;
 assert(RuntimePeripherals::reconcile(restored,original.as<JsonArrayConst>(),profiles.as<JsonObjectConst>(),helpers.as<JsonObjectConst>(),config.as<JsonObjectConst>(),error));
 bool input=false;for(JsonObjectConst n:restored.as<JsonArrayConst>())if(n["binding"]["group"]=="input")input=true;assert(input);
 JsonDocument caps;RuntimePeripherals::capabilities(caps.to<JsonObject>());assert(caps["dynamic"]==true);
 assert(!RuntimePeripherals::available("control","servo"));
 JsonDocument budget,budgetHelpers,led;
 for(unsigned i=0;i<4;i++)budget["controls"][i]="pwm-fan";
 led["statusLedPin"]=8;led["statusLedType"]="rgb";
 assert(RuntimePeripherals::pwmUsed(budget.as<JsonObjectConst>(),budgetHelpers.as<JsonObjectConst>(),led.as<JsonObjectConst>())==7);
#if defined(CONFIG_IDF_TARGET_ESP32C3)
 assert(!RuntimePeripherals::validateResources(budget.as<JsonObjectConst>(),budgetHelpers.as<JsonObjectConst>(),led.as<JsonObjectConst>(),error));
 led["statusLedType"]="neopixel";assert(RuntimePeripherals::validateResources(budget.as<JsonObjectConst>(),budgetHelpers.as<JsonObjectConst>(),led.as<JsonObjectConst>(),error));
#endif
 budget["sensors"][0]="bno055";budget["sensors"][1]="bno055";
 assert(!RuntimePeripherals::validate(budget.as<JsonObjectConst>(),{},error));
 // Row removal shifts helper objects, not the identity of the remaining device.
 JsonDocument two,twoHelpers,twoDevices;unsigned sequence=0;
 two["controls"][0]="ws2812-neopixel-led-strip";two["controls"][1]="ws2812-neopixel-led-strip";
 twoHelpers["control:0"]["DIN"]=3;twoHelpers["control:1"]["DIN"]=4;
 auto create=[&](){return std::string("test:")+std::to_string(++sequence);};
 assert(RuntimePeripherals::assignIds(two.as<JsonObjectConst>(),twoHelpers.as<JsonObject>(),original.as<JsonArrayConst>(),create,error));
 const std::string remaining=twoHelpers["control:1"]["_runtimeId"].as<std::string>();
 assert(RuntimePeripherals::reconcile(twoDevices,original.as<JsonArrayConst>(),two.as<JsonObjectConst>(),twoHelpers.as<JsonObjectConst>(),config.as<JsonObjectConst>(),error));
 two["controls"].as<JsonArray>().remove(0);twoHelpers["control:0"].set(twoHelpers["control:1"]);twoHelpers.remove("control:1");
 assert(RuntimePeripherals::assignIds(two.as<JsonObjectConst>(),twoHelpers.as<JsonObject>(),twoDevices.as<JsonArrayConst>(),create,error));
 assert(twoHelpers["control:0"]["_runtimeId"]==remaining);
 assert(RuntimePeripherals::reconcile(cold,original.as<JsonArrayConst>(),two.as<JsonObjectConst>(),twoHelpers.as<JsonObjectConst>(),config.as<JsonObjectConst>(),error));
 for(JsonObjectConst n:cold.as<JsonArrayConst>())if(n["binding"]["kind"]=="peripheral")assert(n["peripheral"]["id"]==remaining);
 two["controls"][1]="ws2812-neopixel-led-strip";twoHelpers["control:1"].set(twoHelpers["control:0"]);
 assert(!RuntimePeripherals::assignIds(two.as<JsonObjectConst>(),twoHelpers.as<JsonObject>(),cold.as<JsonArrayConst>(),create,error));
#ifndef APP_ESP8266_COMPACT
 assert(RuntimePeripherals::available("audio","buzzer"));
 assert(!RuntimePeripherals::available("display","i2c-oled",1));
#if !APP_DISABLE_DISPLAY
 assert(RuntimePeripherals::available("display","spi-tft",1));
#endif
#endif
#if defined(APP_DISABLE_AUDIO) || defined(APP_ESP8266_COMPACT)
 assert(!RuntimePeripherals::available("audio","max98357a-i2s-amp"));
#else
 assert(RuntimePeripherals::available("audio","max98357a-i2s-amp"));
#endif
 std::cout<<"PASS dynamic ring activation, GPIO0 input, stable saved IDs, removal, capability exclusions\n";
}
