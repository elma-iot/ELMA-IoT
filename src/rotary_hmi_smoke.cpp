#if APP_ROTARY_HMI_SMOKE
#include <Arduino.h>
#include "panel_display.h"
#include "rotary_hmi.h"
namespace { PanelDisplay panel;uint32_t reportAt=0;String previous; }
void setup(){
    Serial.begin(115200);delay(200);
    Serial.printf("[rotary-smoke] flash=%u psram=%u free_psram=%u\n",ESP.getFlashChipSize(),ESP.getPsramSize(),ESP.getFreePsram());
    if(!panel.begin(true,0,80)||!RoundHmi::begin(panel,0,""))Serial.println("[rotary-smoke] panel/HMI initialization FAILED");
    else Serial.println("[rotary-smoke] ready: turn, tap, swipe, short/long/double press; serial n/p/a/b = next/previous/activate/back");
}
void loop(){
    RoundHmi::loop();
    if(Serial.available()){
        char c=Serial.read();const char* navigation=c=='n'?"next":c=='p'?"previous":c=='a'?"activate":c=='b'?"back":nullptr;
        if(navigation){JsonDocument args;args["action"]="navigate";args["navigation"]=navigation;String error;if(!RoundHmi::command(args.as<JsonVariantConst>(),error))Serial.println(error);}
    }
    if(millis()-reportAt>=100){reportAt=millis();JsonDocument state;RoundHmi::snapshot(state.to<JsonObject>());String text;serializeJson(state,text);if(text!=previous){Serial.println(text);previous=text;}}
    delay(1);
}
#endif
