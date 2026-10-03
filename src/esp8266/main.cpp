#ifdef APP_ESP8266_COMPACT
// Compact platform adapter. ESP82xx cooperative platform adapter. Shares the portable graph engine.
#include <Arduino.h>
#include "version.h"
#ifdef ESP8266
#include <ESP8266WiFi.h>
#include <coredecls.h>
#else
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include "../portable_peripherals.h"
#if defined(CONFIG_IDF_TARGET_ESP32C2)
#include <esp_cpu.h>
#include <soc/gpio_reg.h>
#include <soc/soc.h>
#else
#include <Adafruit_NeoPixel.h>
#endif
#include "../logic_runtime.h"
#include "../led_array.h"
#include "../logic_validation.h"
#include "generated_esp8266_assets.h"

#if defined(ESP8266) && defined(ELMA_ADC_VCC)
ADC_MODE(ADC_VCC);
#endif
namespace {
#ifdef ESP32
SemaphoreHandle_t stateMutex=nullptr;
struct StateGuard {StateGuard(){if(stateMutex)xSemaphoreTakeRecursive(stateMutex,portMAX_DELAY);}~StateGuard(){if(stateMutex)xSemaphoreGiveRecursive(stateMutex);}};
#else
struct StateGuard {};
#endif

constexpr size_t kConfigLimit=4096,kGraphLimit=12288,kRequestLimit=16384;
AsyncWebServer server(80);
ElmaLogic::Runtime runtime;
JsonDocument config,status;
String mode="playing",fault,pendingSettings,settingsResult;
uint32_t settingsOperation=0;bool settingsApplying=false;
bool ledOn=false,fsReady=false,ledManual=false,restoreGraphAfterRequest=false,provisioningBusy=false;
uint32_t wifiAt=0,lastTick=0,lastNetwork=0,wifiReconfigureAt=0;
#ifndef CONFIG_IDF_TARGET_ESP32C2
Adafruit_NeoPixel pixel(1,2,NEO_GRB+NEO_KHZ800);
#endif
// C2 has no RMT peripheral. One 24-bit LED needs a bounded 30 us critical section.
void IRAM_ATTR writePixel(int pin,uint8_t r,uint8_t g,uint8_t b){
#ifdef CONFIG_IDF_TARGET_ESP32C2
 pinMode(pin,OUTPUT);uint32_t mask=1u<<pin,bits=(uint32_t(g)<<16)|(uint32_t(r)<<8)|b;
 uint32_t mhz=getCpuFrequencyMhz(),period=mhz*125/100,shortHigh=mhz*35/100,longHigh=mhz*70/100;
 noInterrupts();
 for(int bit=23;bit>=0;--bit){uint32_t start=esp_cpu_get_cycle_count();REG_WRITE(GPIO_OUT_W1TS_REG,mask);uint32_t high=bits&(1u<<bit)?longHigh:shortHigh;while(uint32_t(esp_cpu_get_cycle_count()-start)<high){}REG_WRITE(GPIO_OUT_W1TC_REG,mask);while(uint32_t(esp_cpu_get_cycle_count()-start)<period){}}
 interrupts();
#else
 pixel.setPin(pin);pixel.begin();pixel.setPixelColor(0,pixel.Color(r,g,b));pixel.show();
#endif
}
String serialLine,serialOutput;size_t serialOffset=0;
File serialInput,serialSnapshot;uint8_t serialChunk[256];size_t serialChunkSize=0,serialReceived=0;uint32_t serialRunningCrc=~0u;bool serialWriteFailed=false;
uint32_t bootId=0;
size_t serialExpected=0;
uint32_t serialCrc=0,serialAt=0;
struct Sample {uint32_t sequence=0,at=0;double value=0;char plot[33]{},series[33]{},unit[17]{};};
Sample samples[8];uint32_t sampleSequence=0;

uint32_t chipId(){
#ifdef ESP8266
return ESP.getChipId();
#else
return uint32_t(ESP.getEfuseMac());
#endif
}
uint32_t flashBytes(){
#ifdef ESP8266
return ESP.getFlashChipRealSize();
#else
return ESP.getFlashChipSize();
#endif
}
uint32_t largestHeap(){
#ifdef ESP8266
return ESP.getMaxFreeBlockSize();
#else
return ESP.getMaxAllocHeap();
#endif
}
void pwmWrite(int pin,int hz,int value){
#ifdef ESP8266
analogWriteFreq(hz);analogWriteRange(1023);
#else
analogWriteFrequency(pin,hz);analogWriteResolution(pin,10);
#endif
analogWrite(pin,value);
}
bool digitalPin(int p){return compactGPIO(p);}
bool ledPin(int p){return compactLED(p);}
String readFile(const char* path,size_t limit){File f=LittleFS.open(path,"r");if(!f||f.size()>limit)return {};String text;if(!text.reserve(f.size()))return {};char buffer[128];while(f.available()){size_t count=f.read(reinterpret_cast<uint8_t*>(buffer),std::min(sizeof(buffer),size_t(f.available())));if(!count)break;text.concat(buffer,count);
#ifdef ESP8266
 if(can_yield())yield();
#else
 yield();
#endif
 }return text;}

bool saveFile(const char* path,const String& text){if(!fsReady)return false;String tmp=String(path)+".new";File f=LittleFS.open(tmp,"w");if(!f)return false;size_t written=f.print(text);f.flush();f.close();if(written!=text.length()){LittleFS.remove(tmp);return false;}return LittleFS.rename(tmp,path);}
bool saveJsonFile(const char* path,JsonVariantConst value){if(!fsReady)return false;String tmp=String(path)+".new";File file=LittleFS.open(tmp,"w");if(!file)return false;size_t expected=measureJson(value),written=serializeJson(value,file);file.flush();size_t stored=file.size();file.close();if(written!=expected||stored!=expected){LittleFS.remove(tmp);return false;}return LittleFS.rename(tmp,path);}

void networkReport(){if(serialOutput.length()||wifiReconfigureAt||provisioningBusy)return;Serial.printf("[elma-network] mode=%s ip=%s\n",WiFi.status()==WL_CONNECTED?"STA":"AP",(WiFi.status()==WL_CONNECTED?WiFi.localIP():WiFi.softAPIP()).toString().c_str());}
void connectWifi(){
 WiFi.persistent(false);WiFi.disconnect();WiFi.mode(WIFI_AP_STA);
 String ap=config["wifi"]["apSsid"]|"";if(ap.isEmpty())ap="ELMA-"+String(chipId(),HEX);
 WiFi.softAP(ap.c_str(),config["wifi"]["apPassword"]|"");
 if(config["wifi"]["useStaticIp"]|false){IPAddress ip,gateway,subnet,dns1,dns2;ip.fromString(config["wifi"]["staticIp"]|"");gateway.fromString(config["wifi"]["gateway"]|"");subnet.fromString(config["wifi"]["subnet"]|"");dns1.fromString(config["wifi"]["dns1"]|"0.0.0.0");dns2.fromString(config["wifi"]["dns2"]|"0.0.0.0");WiFi.config(ip,gateway,subnet,dns1,dns2);}
 else {IPAddress zero(0,0,0,0);WiFi.config(zero,zero,zero);}
 String ssid=config["wifi"]["ssid"]|"";if(ssid.length())WiFi.begin(ssid.c_str(),config["wifi"]["password"]|"");wifiAt=millis();
}
// Editor-only peripheral metadata is opaque JSON in SRAM. Both desktop and web
// clients accept its object or string representation; hardware bindings are trusted flash data.
void packPeripheralMetadata(JsonDocument& document){
 for(const char* key:{"peripheralProfiles","peripheralHelperBindings","logicPeripheralIds"}){
  JsonVariant value=document["ui"][key];if(value.is<JsonObject>()||value.is<JsonArray>()){String raw;serializeJson(value,raw);document["ui"][key]=raw;}
 }
 JsonDocument compact;compact.set(document);document=std::move(compact);
}
void defaults(){deserializeJson(config,FPSTR(ELMA_COMPACT_DEFAULTS));if(config["device"]["deviceName"].isNull())config["device"]["deviceName"]="ELMA-"+String(chipId(),HEX);if(config["device"]["friendlyName"].isNull())config["device"]["friendlyName"]="ELMA Device";}

void setLed(bool on,JsonVariantConst args={}){int pin=config["device"]["statusLedPin"]|2;if(!ledPin(pin))return;ledOn=on;if(config["device"]["statusLedType"]=="neopixel"){double brightness=args["brightness"]|20.;writePixel(pin,on?uint8_t((args["red"]|0.)*brightness/100):0,on?uint8_t((args["green"]|255.)*brightness/100):0,on?uint8_t((args["blue"]|0.)*brightness/100):0);}else if(config["device"]["statusLedType"]=="rgb") {const char* channels[]={"red","green","blue"};const char* keys[]={"statusLedPin","statusLedGreenPin","statusLedBluePin"};for(int i=0;i<3;i++){int p=config["device"][keys[i]]|-1;if(!ledPin(p))continue;int value=on?int((args[channels[i]]|255.)*(args["brightness"]|20.)*1023./25500.):0;if(config["device"]["statusLedActiveLow"]|false)value=1023-value;pwmWrite(p,1000,value);}}else{
 double brightness=constrain(args["brightness"]|100.,0.,100.);
 int duty=on?int(lround(brightness*1023./100.)):0;
 if(config["device"]["statusLedActiveLow"]|false)duty=1023-duty;
#ifdef ESP8266
 // Preserve the shared PWM frequency selected by other GPIO nodes.
 analogWriteRange(1023);analogWrite(pin,duty);
#else
 pwmWrite(pin,1000,duty);
#endif
}}
void telemetryStatus(){
 static uint32_t previous=millis();static uint64_t elapsed=0;
 uint32_t now=millis();elapsed+=uint32_t(now-previous);previous=now;
 status["system"]["uptime"]=double(elapsed)/1000.;
 status["hardware"]["cpuFreqMHz"]=ESP.getCpuFreqMHz();
 status["hardware"]["cpuCores"]=1;
 status["system"]["freeHeap"]=ESP.getFreeHeap();
 status["system"]["largestHeapBlockBytes"]=largestHeap();
 status["hardware"]["flashSizeBytes"]=flashBytes();
 static uint32_t sketch=ESP.getSketchSize();
 status["hardware"]["sketchSizeBytes"]=sketch;
 status["system"]["chipTemperatureAvailable"]=false;
#ifdef ESP8266
 static String resetReason=ESP.getResetReason();
 static String mac=WiFi.macAddress();
 static uint32_t freeSketch=ESP.getFreeSketchSpace();
 status["system"]["resetReason"]=resetReason;
 status["hardware"]["freeSketchSpaceBytes"]=freeSketch;
 status["network"]["mac"]=mac;
 if(WiFi.status()==WL_CONNECTED){status["network"]["channel"]=WiFi.channel();status["network"]["bssid"]=WiFi.BSSIDstr();}
#ifdef ELMA_ADC_VCC
 static uint32_t last=0;static double volts=0;
 if(!last||uint32_t(now-last)>=1000){last=now;volts=ESP.getVcc()/1000.;}
 status["system"]["vccAvailable"]=true;status["system"]["vccVolts"]=volts;
#else
 status["system"]["vccAvailable"]=false;
#endif
#endif
}
void updateStatus(){StateGuard guard;status.clear();status["firmware"]["version"]=APP_VERSION "-compact";status["device"].set(config["device"]);status["hardware"]["chipModel"]=ELMA_COMPACT_CHIP;status["hardware"]["boardProfile"]=ELMA_8266_BOARD;status["hardware"]["flashSize"]=flashBytes();status["hardware"]["freeHeap"]=ESP.getFreeHeap();status["hardware"]["largestFreeBlock"]=largestHeap();status["hardware"]["psramSize"]=0;status["wifi"]["connected"]=WiFi.status()==WL_CONNECTED;status["wifi"]["rssi"]=WiFi.RSSI();status["wifi"]["ip"]=WiFi.localIP().toString();status["wifi"]["ssid"]=WiFi.SSID();status["network"]["wifiConnected"]=WiFi.status()==WL_CONNECTED;status["network"]["apMode"]=WiFi.getMode()==WIFI_AP_STA||WiFi.getMode()==WIFI_AP;status["network"]["ip"]=WiFi.localIP().toString();status["network"]["ssid"]=WiFi.SSID();status["network"]["apSsid"]=String(config["wifi"]["apSsid"]|"").length()?String(config["wifi"]["apSsid"]|""):"ELMA-"+String(chipId(),HEX);status["network"]["wifiRssi"]=WiFi.RSSI();status["system"]["uptime"]=millis()/1000;status["builtinLed"]["digital"]=ledOn;status["error"]=fault;status["plotsAvailable"]=false;for(JsonObjectConst n:runtime.graph()["nodes"].as<JsonArrayConst>())if(strcmp(n["type"]|"","mainboard.plot")==0){status["plotsAvailable"]=true;break;}telemetryStatus();
 for(JsonObjectConst n:runtime.nodes())PortablePeripherals::sample(n,status.as<JsonObject>());
 for(JsonObjectConst n:runtime.nodes())if(n["type"]=="hardware.gpio"){int pin=n["parameters"]["pin"]|-1;auto v=status["gpio"][n["id"].as<const char*>()];if(digitalPin(pin))v["digital"]=digitalRead(pin)!=0;if(compactADC(pin)){v["analog"]=analogRead(pin);
#ifdef ESP32
v["millivolts"]=analogReadMilliVolts(pin);
#endif
}
#ifdef ESP8266
if(pin==17&&ELMA_COMPACT_ADC_PAD){int raw=analogRead(A0);v["analog"]=raw;v["millivolts"]=raw*(String(ELMA_8266_BOARD).indexOf("wemos")>=0?3200.:1000.)/1023.;}
#endif
}
}
bool action(JsonObjectConst node,JsonVariantConst args,std::string& error){error.clear();String type=node["type"]|"",command=args["action"]|"";
 if(LedArrays::matches(node))return LedArrays::action(node,args,error);
 if(PortablePeripherals::output(node))return PortablePeripherals::action(node,args,error);
 if(type=="hardware.led"){ledManual=command!="release";setLed(command=="toggle"?!ledOn:command!="off"&&command!="release",args);return true;}
 if(type=="hardware.gpio"){int pin=node["parameters"]["pin"]|-1;if(!digitalPin(pin)||pin==(config["device"]["statusLedPin"]|2)){error="GPIO unavailable or reserved";return false;}String m=node["parameters"]["mode"]|"input";if(m=="output"){pinMode(pin,OUTPUT);if(command=="write")digitalWrite(pin,args["state"]|false);else if(command=="toggle")digitalWrite(pin,!digitalRead(pin));else{error="Unsupported GPIO action";return false;}}else if(m=="pwm"&&command=="pwm"){double duty=args["duty"]|-1.;int hz=node["parameters"]["frequency"]|1000;if(!isfinite(duty)||duty<0||duty>100||hz<100||hz>ELMA_COMPACT_PWM_MAX){error="PWM frequency or duty exceeds the selected chip capability";return false;}pwmWrite(pin,hz,int(duty*10.23));}else{error="GPIO is not an output";return false;}return true;}
 if(type=="mainboard.plot"){double value=args["value"].as<double>();if(args["value"].isNull()||!isfinite(value)){error="Plot value unavailable";return false;}auto& s=samples[sampleSequence%8];s.sequence=++sampleSequence;s.at=millis();s.value=value;strlcpy(s.plot,node["parameters"]["plot"]|"Plot",sizeof(s.plot));strlcpy(s.series,node["parameters"]["series"]|"Value",sizeof(s.series));strlcpy(s.unit,node["parameters"]["unit"]|"",sizeof(s.unit));return true;}
 error="Action unsupported by ESP8266 compact profile";return false;
}
bool supported(const char* type){String needle="\""+String(type)+"\"";return String(FPSTR(ELMA_8266_TYPES)).indexOf(needle)>=0;}
bool applyArrayDefaultsPending=false;
bool devices(JsonDocument& d,JsonVariantConst graph={}){
 if(graph.isNull()){JsonDocument filter;for(const char* key:{"type","parameters","binding","peripheral","ports","name"})filter[0][key]=true;if(deserializeJson(d,FPSTR(ELMA_8266_DEVICES),DeserializationOption::Filter(filter)))return false;}
 else {JsonDocument filter;filter[0]["type"]=true;filter[0]["parameters"]=true;filter[0]["peripheral"]=true;
  for(const char* key:{"id","type","direction","required"})filter[0]["ports"][0][key]=true;
  for(const char* key:{"kind","pin","ledType","brightnessSupported","activeLow","greenPin","bluePin","pwmAllowed","maxPwmFrequency","group","index","pins","count","maxCount","layout","rows","columns","serpentine","defaults"})filter[0]["binding"][key]=true;
  for(JsonObjectConst n:graph["nodes"].as<JsonArrayConst>())if(n["type"]=="hardware.gpio")filter[0]["binding"]["allowedPins"][String(n["parameters"]["pin"]|-1)]=true;
  if(deserializeJson(d,FPSTR(ELMA_8266_DEVICES),DeserializationOption::Filter(filter)))return false;
 }
// Rebind configured arrays from saved settings; never accept graph-supplied pins.
JsonDocument helpers;
if(config["ui"]["peripheralHelperBindings"].is<const char*>())deserializeJson(helpers,config["ui"]["peripheralHelperBindings"].as<const char*>());
else helpers.set(config["ui"]["peripheralHelperBindings"]);
for(JsonObject definition:d.as<JsonArray>())if(LedArrays::matches(definition)){
 String key="control:"+String(definition["binding"]["index"]|0);std::string why;
 if(!LedArrays::updateBinding(definition["binding"],helpers[key],why))return false;
 int pin=definition["binding"]["pins"]["DIN"]|-1;if(!digitalPin(pin))return false;
 for(const char* led:{"statusLedPin","statusLedGreenPin","statusLedBluePin"})if(pin==(config["device"][led]|-1))return false;
}
for(JsonObjectConst first:d.as<JsonArrayConst>())if(LedArrays::matches(first)){
 int pin=first["binding"]["pins"]["DIN"]|-1;
 for(JsonObjectConst second:d.as<JsonArrayConst>())if(first["peripheral"]["id"]!=second["peripheral"]["id"])
  for(JsonPairConst port:second["binding"]["pins"].as<JsonObjectConst>())if(port.value().is<int>()&&port.value().as<int>()==pin)return false;
}
helpers.clear();
// Only retain definitions referenced by this graph. The editor catalogue includes
// every configured peripheral action; keeping it all during validation exhausts
// ESP8266 SRAM even when the graph never uses those peripherals.
if(!graph.isNull()){
 JsonDocument used;auto selected=used.to<JsonArray>();
 for(JsonObjectConst definition:d.as<JsonArrayConst>()){
  bool needed=false;
  for(JsonObjectConst node:graph["nodes"].as<JsonArrayConst>())if(node["type"]==definition["type"] &&
    (definition["peripheral"].isNull()||node["peripheral"]["id"]==definition["peripheral"]["id"])){needed=true;break;}
  if(needed)selected.add(definition);
 }
 if(used.overflowed())return false;
 d=std::move(used);
}
for(JsonObject n:d.as<JsonArray>()){if(n["type"]=="hardware.led"){n["binding"]["pin"]=config["device"]["statusLedPin"]|2;n["binding"]["ledType"]=config["device"]["statusLedType"]|"regular";n["binding"]["brightnessSupported"]=true;n["binding"]["activeLow"]=config["device"]["statusLedActiveLow"]|false;}if(n["type"]=="hardware.gpio"){n["binding"]["pwmAllowed"]=config["device"]["statusLedType"]!="rgb";for(const char* key:{"statusLedPin","statusLedGreenPin","statusLedBluePin"})n["binding"]["allowedPins"].remove(String(config["device"][key]|-1));}}return !d.overflowed();}
bool loadGraphDocument(JsonVariantConst input,String& error,bool validateOnly=false){StateGuard guard;if(measureJson(input)>kGraphLimit||input["nodes"].size()>24||input["connections"].size()>48){error="Compact runtime memory limit: 24 nodes, 48 links and 12 KiB after metadata reduction";return false;}for(JsonObjectConst n:input["nodes"].as<JsonArrayConst>())if(!supported(n["type"]|"")){error="Node unsupported on ESP8266";return false;}
 
#ifdef ESP8266
 int sharedFrequency=0;
 for(JsonObjectConst n:input["nodes"].as<JsonArrayConst>())if(n["type"]=="hardware.gpio"&&n["parameters"]["mode"]=="pwm") {int hz=n["parameters"]["frequency"]|1000;if(sharedFrequency&&sharedFrequency!=hz){error="ESP8266 PWM outputs must share one frequency";return false;}sharedFrequency=hz;}
#endif
 // The previous graph is already persisted; release its working set before validating its replacement.
 LedArrays::reset();std::string resetError;runtime.begin("{\"schemaVersion\":1,\"nodes\":[],\"connections\":[]}",action,resetError);
 if(provisioningBusy)Serial.printf("[clone] validating nodes=%u heap=%u\n",unsigned(input["nodes"].size()),ESP.getFreeHeap());
 JsonDocument defs,accepted;if(!devices(defs,input)){error="Insufficient memory for hardware definitions";return false;}std::string why;if(provisioningBusy)Serial.printf("[clone] definitions ready heap=%u\n",ESP.getFreeHeap());if(!ElmaLogic::validateEditable(input,defs.as<JsonArrayConst>(),accepted,why)){error=String(why.c_str())+" (free heap "+String(ESP.getFreeHeap())+" bytes)";return false;}
 if(provisioningBusy)Serial.printf("[clone] graph validated heap=%u\n",ESP.getFreeHeap());
 // Render metadata stays in LittleFS, not in the execution engine's SRAM copy.
 accepted.remove("devices");accepted.remove("view");
 for(JsonObject n:accepted["nodes"].as<JsonArray>()){
  n.remove("position");n.remove("name");n.remove("description");
  if(String(n["type"]|"").startsWith("hardware."))n.remove("binding");
  JsonArray ports=n["ports"].as<JsonArray>();
  for(size_t i=ports.size();i>0;--i){JsonObject p=ports[i-1];bool linked=false;
   for(JsonObjectConst e:accepted["connections"].as<JsonArrayConst>())if(e["target"]["node"]==n["id"]&&e["target"]["port"]==p["id"])linked=true;
   if(p["type"]=="execution" || (p["direction"]=="input"&&!linked)||p["enabled"]==false)ports.remove(i-1);
   else {p.remove("label");p.remove("required");p.remove("enabled");}
  }
 }
 for(JsonObject g:accepted["groups"].as<JsonArray>()){g.remove("rect");g.remove("name");g.remove("color");g.remove("label");}
 for(JsonObject edge:accepted["connections"].as<JsonArray>()){edge.remove("routingPoints");edge.remove("id");}
 defs.clear();
 if(validateOnly)return true;
 // Compact through a bounded storage stream, then transfer ownership; never hold two instruction trees.
 if(!saveJsonFile("/instructions.tmp",accepted.as<JsonVariantConst>())){error="Cannot stage runtime instructions";return false;}
 accepted.clear();JsonDocument executable;File instructions=LittleFS.open("/instructions.tmp","r");auto parsedInstructions=deserializeJson(executable,instructions);instructions.close();LittleFS.remove("/instructions.tmp");
 if(parsedInstructions){error="Cannot load runtime instructions";return false;}
 if(executable.overflowed()||ESP.getFreeHeap()<6000){error="Insufficient free heap for instruction graph";return false;}
 if(provisioningBusy)Serial.printf("[clone] starting runtime heap=%u\n",ESP.getFreeHeap());if(!runtime.begin(std::move(executable),action,why)){error=why.c_str();return false;}for(JsonObjectConst n:runtime.nodes())if(n["type"]=="hardware.gpio"){int pin=n["parameters"]["pin"]|-1;if(digitalPin(pin)){String m=n["parameters"]["mode"]|"input";
#ifdef ESP8266
int down=INPUT_PULLDOWN_16;
#else
int down=INPUT_PULLDOWN;
#endif
pinMode(pin,m=="input_pullup"?INPUT_PULLUP:m=="input_pulldown"?down:m=="output"||m=="pwm"?OUTPUT:INPUT);}}
 if(mode=="stopped")runtime.suspend();else if(mode=="paused")runtime.pause(millis());return true;
}
bool loadGraph(const String& raw,String& error,bool validateOnly=false){JsonDocument input;auto parsed=deserializeJson(input,raw);if(parsed){error=parsed.c_str();return false;}return loadGraphDocument(input.as<JsonVariantConst>(),error,validateOnly);}
bool loadGraphFile(const char* path,String& error,bool validateOnly=false){
 File file=LittleFS.open(path,"r");if(!file)return true;
 if(file.size()>kGraphLimit){error="Stored Logics exceeds runtime limit";return false;}
 // Opaque compressed editor layout stays in flash; validation never needs it.
 JsonDocument input,filter;filter["schemaVersion"]=true;filter["compactFormat"]=true;
 for(const char* key:{"id","type","parameters","peripheral"})filter["nodes"][0][key]=true;
 for(const char* key:{"source","target"})filter["connections"][0][key]=true;
 for(const char* key:{"id","mode","nodes","color"})filter["groups"][0][key]=true;
 auto parsed=deserializeJson(input,file,DeserializationOption::Filter(filter));file.close();filter.clear();
 if(parsed){error=parsed.c_str();return false;}return loadGraphDocument(input.as<JsonVariantConst>(),error,validateOnly);
}
bool loadSavedGraph(String& error,bool validateOnly=false){return loadGraphFile("/logics.json",error,validateOnly);}

bool copyStoredFile(const char* from,const char* to){File input=LittleFS.open(from,"r"),output=LittleFS.open(to,"w");if(!input||!output)return false;char buffer[256];while(input.available()){size_t count=input.readBytes(buffer,sizeof(buffer));if(output.write(reinterpret_cast<uint8_t*>(buffer),count)!=count)return false;}output.flush();return true;}
bool recoverConfiguration(){if(!LittleFS.exists("/config.pending"))return true;String marker=readFile("/config.pending",8);if(!copyStoredFile("/settings.backup","/settings.json"))return false;if(marker=="graph"){if(!copyStoredFile("/logics.backup","/logics.json"))return false;}else LittleFS.remove("/logics.json");return LittleFS.remove("/config.pending");}
// Transfer ownership to the asynchronous response: no second JSON tree and no
// contiguous serialized copy. This is essential for ESP8266's small heap.
class OwnedJsonResponse:public AsyncJsonResponse {
public:
 explicit OwnedJsonResponse(JsonDocument& source){_jsonBuffer=std::move(source);_root=_jsonBuffer.as<JsonVariant>();setLength();}
};
void jsonReply(AsyncWebServerRequest* req,JsonDocument& d,int code=200){
 if(d.overflowed()){req->send(503,"application/json","{\"error\":\"Insufficient heap for response; reduce graph size\"}");return;}
 auto* response=new(std::nothrow) OwnedJsonResponse(d);
 if(!response){req->send(503,"application/json","{\"error\":\"Device busy; retry\"}");return;}
 response->setCode(code);req->send(response);
}

void errorReply(AsyncWebServerRequest* req,const String& message,int code=400){JsonDocument d;d["error"]=message;jsonReply(req,d,code);}
bool applyConfiguration(JsonVariantConst v,String& error,const char* stagedGraph=nullptr,JsonDocument* incomingOwner=nullptr){StateGuard guard;const bool hasWifi=v["wifi"].is<JsonObjectConst>();JsonDocument next;next.set(config);for(const char* section:{"device","wifi","ui"})if(v[section].is<JsonObjectConst>())for(JsonPairConst pair:v[section].as<JsonObjectConst>())next[section][pair.key()].set(pair.value());
 packPeripheralMetadata(next);if(measureJson(next)>kConfigLimit){error="ESP8266 settings exceed 4 KiB";return false;}int pin=next["device"]["statusLedPin"]|2;if(pin!=-1&&!ledPin(pin)){error="LED pin unavailable on selected board";return false;}
 const char* kind=next["device"]["statusLedType"]|"regular";if(strcmp(kind,"regular")&&strcmp(kind,"neopixel")&&strcmp(kind,"rgb")){error="ESP8266 compact profile supports regular or NeoPixel LED";return false;}
 if(v["webAuth"]["enabled"]==true||String(v["mqtt"]["host"]|"").length()||v["sd"]["enabled"]==true||v["oled"]["enabled"]==true||v["audio"]["enabled"]==true){error="Requested service is not supported by ESP8266 compact profile";return false;}
 if(pin>=0&&String(kind)=="rgb"){int green=next["device"]["statusLedGreenPin"]|-1,blue=next["device"]["statusLedBluePin"]|-1;if(!ledPin(green)||!ledPin(blue)||pin==green||pin==blue||green==blue){error="RGB LED requires three different available GPIOs";return false;}}
 String apPassword=next["wifi"]["apPassword"]|"";if(apPassword.length()&&(apPassword.length()<8||apPassword.length()>63)){error="Access point password must contain 8 to 63 characters";return false;}
 if(next["wifi"]["useStaticIp"]|false){for(const char* key:{"staticIp","gateway","subnet"}){IPAddress address;if(!address.fromString(next["wifi"][key]|"")){error="Invalid static network address: "+String(key);return false;}}}
 // Array-only UI edits do not require rebuilding the entire running graph.
 // Keep timers/group state and avoid a second executable graph in small SRAM.
 if(!stagedGraph&&v["windowsLogic"].isNull()&&v["device"].isNull()&&v["wifi"].isNull()&&!v["ui"]["peripheralHelperBindings"].isNull()){
  if(incomingOwner)incomingOwner->clear();
  JsonDocument helpers,bindings,filter;
  if(deserializeJson(helpers,next["ui"]["peripheralHelperBindings"].as<const char*>())){error="Invalid peripheral options";return false;}
  filter[0]["type"]=true;filter[0]["peripheral"]["id"]=true;
  for(const char* key:{"pins","count","index","layout","rows","columns","serpentine","maxCount","defaults"})filter[0]["binding"][key]=true;
  if(deserializeJson(bindings,FPSTR(ELMA_8266_DEVICES),DeserializationOption::Filter(filter))){error="Cannot validate array configuration";return false;}filter.clear();
  for(JsonObject n:bindings.as<JsonArray>())if(LedArrays::matches(n)){
   String key="control:"+String(n["binding"]["index"]|0);std::string why;
   if(!LedArrays::updateBinding(n["binding"],helpers[key],why)){error=why.c_str();return false;}
   int p=n["binding"]["pins"]["DIN"]|-1;
   if(!digitalPin(p)){error="LED data GPIO is unavailable on this board";return false;}
   for(const char* led:{"statusLedPin","statusLedGreenPin","statusLedBluePin"})if(p==(next["device"][led]|-1)){error="LED data GPIO conflicts with built-in LED";return false;}
   for(JsonObjectConst other:runtime.nodes())if(other["type"]=="hardware.gpio"&&other["parameters"]["pin"]==p){error="LED data GPIO is used by Logics";return false;}
  }
  for(JsonObjectConst a:bindings.as<JsonArrayConst>())if(LedArrays::matches(a))for(JsonObjectConst b:bindings.as<JsonArrayConst>())if(a["peripheral"]["id"]!=b["peripheral"]["id"])
   for(JsonPairConst port:b["binding"]["pins"].as<JsonObjectConst>())if(port.value().is<int>()&&port.value().as<int>()==(a["binding"]["pins"]["DIN"]|-1)){error="LED data GPIO is used by another peripheral";return false;}
  helpers.clear();if(next.overflowed()||bindings.overflowed()){error="Insufficient memory for array configuration";return false;}
  if(!saveJsonFile("/settings.array.tmp",next)){error="Cannot save array settings";return false;}
  if(!LittleFS.rename("/settings.array.tmp","/settings.json")){error="Cannot commit array settings";return false;}
  config=std::move(next);LedArrays::reset();
  for(JsonObjectConst n:bindings.as<JsonArrayConst>())if(LedArrays::matches(n))runtime.setPeripheralBinding(n["peripheral"]["id"]|"",n["binding"]);
  applyArrayDefaultsPending=true;return true;
 }
 String previousSettings;serializeJson(config,previousSettings);
 setLed(false);config.set(next);next.clear();
 bool incoming=stagedGraph||v["windowsLogic"].is<JsonObjectConst>();
 if(incomingOwner&&(stagedGraph||!incoming))incomingOwner->clear();
 auto restore=[&](){deserializeJson(config,previousSettings);if(!stagedGraph){String ignored;loadSavedGraph(ignored);}};
 bool valid=stagedGraph?loadGraphFile(stagedGraph,error):incoming?loadGraphDocument(v["windowsLogic"],error):loadSavedGraph(error);
 if(!valid){restore();return false;}
 String raw;serializeJson(config,raw);
 // Durable rollback marker is removed only after both files and runtime are committed.
 bool hadGraph=LittleFS.exists("/logics.json");
 if(!saveFile("/settings.backup",previousSettings)||(hadGraph&&!copyStoredFile("/logics.json","/logics.backup"))||!saveFile("/config.pending",hadGraph?"graph":"empty")){error="Cannot prepare configuration backup";restore();return false;}
 bool saved=saveFile("/settings.json",raw);
 if(saved&&incoming)saved=stagedGraph?copyStoredFile(stagedGraph,"/logics.json"):saveJsonFile("/logics.json",v["windowsLogic"]);

 if(saved)saved=LittleFS.remove("/config.pending");
 if(!saved){bool recovered=recoverConfiguration();restore();if(error.isEmpty())error=recovered?"Configuration failed; previous settings restored":"Configuration recovery failed; restart required";return false;}
 LittleFS.remove("/settings.backup");LittleFS.remove("/logics.backup");

 setLed(false);applyArrayDefaultsPending=true;if(hasWifi)wifiReconfigureAt=millis()+500;return true;
}
class StoredGraphResponse:public AsyncAbstractResponse {
 File graph_;JsonDocument metadata_;size_t graphLength_,devicesLength_;
 static constexpr const char* devicePrefix=",\"devices\":";
 static constexpr const char* prefix="{\"graph\":";
 static constexpr const char* empty="{\"schemaVersion\":1,\"nodes\":[],\"connections\":[]}";
public:
 explicit StoredGraphResponse(JsonDocument& metadata):graph_(LittleFS.open("/logics.json","r")),metadata_(std::move(metadata)){
  graphLength_=graph_?graph_.size():strlen(empty);devicesLength_=strlen_P(ELMA_8266_DEVICES);_code=200;_contentType="application/json";_contentLength=strlen(prefix)+graphLength_+strlen(devicePrefix)+devicesLength_+measureJson(metadata_);_sendContentLength=true;
 }
 bool _sourceValid()const override{return true;}
 size_t _fillBuffer(uint8_t* data,size_t len)override{
  size_t offset=_sentLength,written=0,head=strlen(prefix);
  while(written<len&&offset<_contentLength){
   if(offset<head){size_t n=std::min(len-written,head-offset);memcpy(data+written,prefix+offset,n);written+=n;offset+=n;}
   else if(offset<head+graphLength_){size_t at=offset-head,n=std::min(len-written,graphLength_-at);if(graph_){graph_.seek(at);n=graph_.read(data+written,n);}else memcpy(data+written,empty+at,n);if(!n)break;written+=n;offset+=n;}
   else if(offset<head+graphLength_+strlen(devicePrefix)){size_t at=offset-head-graphLength_,n=std::min(len-written,strlen(devicePrefix)-at);memcpy(data+written,devicePrefix+at,n);written+=n;offset+=n;}
   else if(offset<head+graphLength_+strlen(devicePrefix)+devicesLength_){size_t at=offset-head-graphLength_-strlen(devicePrefix),n=std::min(len-written,devicesLength_-at);memcpy_P(data+written,ELMA_8266_DEVICES+at,n);written+=n;offset+=n;}
   else if(offset==head+graphLength_+strlen(devicePrefix)+devicesLength_){data[written++]=',';offset++;}
   else{size_t at=offset-head-graphLength_-strlen(devicePrefix)-devicesLength_,n=std::min(len-written,_contentLength-offset);ChunkPrint dest(data+written,at,n);serializeJson(metadata_,dest);written+=n;offset+=n;}
  }return written;
 }
};
void logicReply(AsyncWebServerRequest* req,bool live=false){StateGuard guard;if(provisioningBusy){req->send(503,"application/json","{\"error\":\"Provisioning in progress\"}");return;}JsonDocument out;if(!live){JsonDocument types;deserializeJson(types,FPSTR(ELMA_8266_TYPES));out["supportedTypes"].set(types);types.clear();out["audioEnabled"]=false;out["compactGraph"]=true;}out["ledBinding"]["brightnessSupported"]=true;out["ledBinding"]["kind"]="led";out["ledBinding"]["pin"]=config["device"]["statusLedPin"]|-1;out["ledBinding"]["ledType"]=config["device"]["statusLedType"]|"regular";out["ledBinding"]["greenPin"]=config["device"]["statusLedGreenPin"]|-1;out["ledBinding"]["bluePin"]=config["device"]["statusLedBluePin"]|-1;out["mode"]=mode;auto groups=out["groups"].to<JsonArray>();for(JsonObjectConst g:runtime.graph()["groups"].as<JsonArrayConst>()){auto state=groups.add<JsonObject>();state["id"].set(g["id"]);state["mode"].set(g["mode"]);}if(live)runtime.telemetry(out["live"].to<JsonObject>());if(live){jsonReply(req,out);return;}if(out.overflowed()){errorReply(req,"Insufficient memory for editor metadata",503);return;}req->send(new StoredGraphResponse(out));}
// Same bounded one-sample serial protocol as plot_telemetry.cpp on full targets.
void plotSerialReply(JsonDocument& out,uint32_t after,uint32_t boot){
 if(boot!=bootId||after>sampleSequence)after=0;
 const uint32_t count=std::min(sampleSequence,uint32_t(8));
 const uint32_t first=sampleSequence-count+1;
 const uint32_t wanted=std::max(after+1,first);
 const bool found=count&&wanted<=sampleSequence;
 out["boot"]=bootId;out["latest"]=sampleSequence;
 out["cursor"]=found?wanted:sampleSequence;
 out["dropped"]=after&&count&&after+1<first?first-after-1:0;
 if(found){const auto& s=samples[(wanted-1)%8];auto row=out["sample"].to<JsonObject>();
  row["plot"]=s.plot;row["series"]=s.series;row["unit"]=s.unit;
  row["t"]=s.at;row["value"]=s.value;row["epoch"]=0;
 }
}
void plotReply(JsonDocument& out,uint32_t after){out["boot"]=bootId;out["cursor"]=sampleSequence;out["sequence"]=sampleSequence;out["dropped"]=sampleSequence>after+8?sampleSequence-after-8:0;auto rows=out["samples"].to<JsonArray>();for(uint32_t seq=sampleSequence>8?sampleSequence-7:1;seq<=sampleSequence;seq++){auto& s=samples[(seq-1)%8];if(s.sequence<=after)continue;auto row=rows.add<JsonObject>();row["sequence"]=s.sequence;row["t"]=s.at;row["at"]=s.at;row["epoch"]=0;row["value"]=s.value;row["plot"]=s.plot;row["series"]=s.series;row["unit"]=s.unit;}}
// Large graph uploads are spooled to flash, avoiding a simultaneous raw body in SRAM.
class StoredGraphHandler:public AsyncWebHandler {
 ArJsonRequestHandlerFunction callback_;AsyncWebServerRequest* owner_=nullptr;File upload_;size_t received_=0;bool failed_=false;
 void cleanup(){upload_.close();LittleFS.remove("/graph-upload.tmp");owner_=nullptr;}
public:
 explicit StoredGraphHandler(ArJsonRequestHandlerFunction callback):callback_(std::move(callback)){}
 bool canHandle(AsyncWebServerRequest* r)const override{return r->method()==HTTP_POST&&r->url()=="/api/logics";}
 bool isRequestHandlerTrivial()const override{return false;}
 void handleBody(AsyncWebServerRequest* r,uint8_t* data,size_t len,size_t index,size_t total)override{
  StateGuard guard;if(provisioningBusy||total>kRequestLimit||!total)return;
  if(index==0&&!owner_){owner_=r;received_=0;failed_=false;upload_=LittleFS.open("/graph-upload.tmp","w");r->onDisconnect([this,r](){StateGuard guard;if(owner_==r)cleanup();});}
  if(owner_!=r)return;if(!upload_||index!=received_||upload_.write(data,len)!=len)failed_=true;received_+=len;
 }
 void handleRequest(AsyncWebServerRequest* r)override{
  StateGuard guard;if(r->contentLength()>kRequestLimit){r->send(413);return;}if(owner_!=r){r->send(409,"application/json","{\"error\":\"Another graph upload is active; retry\"}");return;}
  upload_.close();if(failed_||received_!=r->contentLength()){cleanup();r->send(400);return;}
  bool released=r->contentLength()>1024;if(released){std::string why;runtime.begin("{\"schemaVersion\":1,\"nodes\":[],\"connections\":[]}",action,why);}
  JsonDocument document;File source=LittleFS.open("/graph-upload.tmp","r");auto parsed=deserializeJson(document,source);source.close();cleanup();
  if(parsed){document.clear();if(released){String ignored;loadSavedGraph(ignored);}errorReply(r,"Invalid graph upload or insufficient memory");return;}
  JsonVariant value=document.as<JsonVariant>();if(released&&value["graph"].isNull()){String ignored;loadSavedGraph(ignored);}callback_(r,value);if(restoreGraphAfterRequest){restoreGraphAfterRequest=false;document.clear();String error;if(!loadSavedGraph(error))fault=error;}
 }
};
void setupHttp(){for(const auto& asset:ELMA_8266_ASSETS){auto* a=&asset;server.on(a->path,HTTP_GET,[a](AsyncWebServerRequest* r){auto* response=r->beginResponse_P(200,a->type,a->data,a->size);response->addHeader("Content-Encoding","gzip");response->addHeader("Cache-Control","no-cache");r->send(response);});}
 server.on("/api/board",HTTP_GET,[](AsyncWebServerRequest* r){JsonDocument out;deserializeJson(out,FPSTR(ELMA_COMPACT_BOARD_INFO));jsonReply(r,out);});
 server.on("/api/status",HTTP_GET,[](AsyncWebServerRequest* r){StateGuard guard;if(provisioningBusy){r->send(503,"application/json","{\"error\":\"Configuration transfer in progress; retry shortly\"}");return;}updateStatus();JsonDocument out;out.set(status);jsonReply(r,out);});
 server.on("/api/settings",HTTP_GET,[](AsyncWebServerRequest* r){StateGuard guard;JsonDocument out;out.set(config);out["wifi"].remove("password");jsonReply(r,out);});
 server.on("/api/config-operation",HTTP_GET,[](AsyncWebServerRequest* r){StateGuard guard;JsonDocument out;out["operation"]=settingsOperation;out["pending"]=settingsApplying||pendingSettings.length()>0;out["error"]=settingsResult;jsonReply(r,out);});
 auto* settings=new AsyncCallbackJsonWebHandler("/api/settings",[](AsyncWebServerRequest* r,JsonVariant& v){StateGuard guard;if(settingsApplying||pendingSettings.length()||provisioningBusy){errorReply(r,"Configuration is already being applied",409);return;}serializeJson(v,pendingSettings);settingsResult="";++settingsOperation;JsonDocument out;out["pending"]=true;out["operation"]=settingsOperation;jsonReply(r,out,202);});settings->setMaxContentLength(kConfigLimit);server.addHandler(settings);
 server.on("/api/logics",HTTP_GET,[](AsyncWebServerRequest* r){StateGuard guard;logicReply(r,r->hasParam("live"));});
 auto* logic=new StoredGraphHandler([](AsyncWebServerRequest* r,JsonVariant& v){StateGuard guard;String error;if(!v["graph"].isNull()){v["graph"].remove("devices");if(!loadGraphDocument(v["graph"],error)){restoreGraphAfterRequest=true;errorReply(r,error);return;}if(!saveJsonFile("/logics.json",v["graph"])){restoreGraphAfterRequest=true;errorReply(r,"Logics storage failed; restoring previous graph");return;}}
 if(v["mode"].is<const char*>()){String target=v["mode"].as<String>();if(target!="playing"&&target!="paused"&&target!="stopped"){errorReply(r,"Invalid mode");return;}String previousMode=mode;mode=target;if(mode=="playing"){if(previousMode=="paused")runtime.resume(millis());else runtime.restart();}else if(mode=="paused")runtime.pause(millis());else runtime.suspend();saveFile("/mode",mode);}
 if(v["group"].is<JsonObjectConst>()){if(!runtime.controlGroup(v["group"]["id"]|"",v["group"]["mode"]|"",millis())){errorReply(r,"Unknown group or mode");return;}}
 logicReply(r,true);});server.addHandler(logic);
 server.on("/api/plots",HTTP_GET,[](AsyncWebServerRequest* r){StateGuard guard;if(provisioningBusy){r->send(503,"application/json","{\"error\":\"Configuration transfer in progress; retry shortly\"}");return;}JsonDocument out;uint32_t after=r->hasParam("after")?r->getParam("after")->value().toInt():r->hasParam("since")?r->getParam("since")->value().toInt():0;uint32_t boot=r->hasParam("boot")?r->getParam("boot")->value().toInt():bootId;plotReply(out,boot==bootId?after:0);jsonReply(r,out);});server.begin();}
uint32_t crc32(const String& text){uint32_t crc=~0u;for(size_t i=0;i<text.length();i++){crc^=uint8_t(text[i]);for(int k=0;k<8;k++)crc=(crc>>1)^(0xEDB88320u&-(crc&1));}return ~crc;}
void serialTick(){if(serialOutput.length()){size_t available=Serial.availableForWrite();size_t count=std::min(available,serialOutput.length()-serialOffset);if(count)serialOffset+=Serial.write((const uint8_t*)serialOutput.c_str()+serialOffset,count);if(serialOffset==serialOutput.length()){serialOutput="";serialOffset=0;}return;}if(serialSnapshot){size_t count=std::min(size_t(Serial.availableForWrite()),size_t(256));if(count){uint8_t buffer[256];size_t read=serialSnapshot.read(buffer,count);if(read)Serial.write(buffer,read);if(!serialSnapshot.available()){serialSnapshot.close();serialOutput="}\n";serialOffset=0;}}return;}if(serialExpected&&millis()-serialAt>12000){serialExpected=0;provisioningBusy=false;serialInput.close();LittleFS.remove("/serial-upload.tmp");String ignored;loadSavedGraph(ignored);Serial.println("[clone] error=configuration receive timeout");}unsigned budget=256;while(Serial.available()&&budget--){char c=Serial.read();if(serialExpected){serialChunk[serialChunkSize++]=uint8_t(c);serialRunningCrc^=uint8_t(c);for(int bit=0;bit<8;bit++)serialRunningCrc=(serialRunningCrc>>1)^(0xEDB88320u&-(serialRunningCrc&1));serialReceived++;serialAt=millis();
 if(serialChunkSize==sizeof(serialChunk)||serialReceived==serialExpected){if(serialInput.write(serialChunk,serialChunkSize)!=serialChunkSize)serialWriteFailed=true;serialChunkSize=0;}
 if(serialReceived==serialExpected){serialExpected=0;serialInput.close();JsonDocument data;String error;File source=LittleFS.open("/serial-upload.tmp","r");
 if(serialWriteFailed)error="Configuration upload storage failed";else if(~serialRunningCrc!=serialCrc)error="configuration checksum mismatch";else if(deserializeJson(data,source))error="Invalid configuration JSON";source.close();LittleFS.remove("/serial-upload.tmp");
 bool staged=false;
 if(error.isEmpty()&&data["windowsLogic"].is<JsonObjectConst>()){
  staged=saveJsonFile("/incoming-graph.tmp",data["windowsLogic"]);
  if(!staged)error="Cannot stage incoming Logics";
  data.remove("windowsLogic");packPeripheralMetadata(data);
 }
 if(error.isEmpty()&&applyConfiguration(data.as<JsonVariantConst>(),error,staged?"/incoming-graph.tmp":nullptr,&data))Serial.println("[clone] configuration applied");
 LittleFS.remove("/incoming-graph.tmp");
 if(error.length()){Serial.println("[clone] error="+error);data.clear();String ignored;loadSavedGraph(ignored);}provisioningBusy=false;}
 continue;}if(c=='\n'){if(serialLine=="ELMA_CLONE_PING")Serial.println("[clone] provisioning ready");else if(serialLine=="ELMA_NETWORK_STATUS")networkReport();else if(serialLine.startsWith("ELMA_PLOTS_GET ")){unsigned after=0,boot=0;sscanf(serialLine.c_str(),"ELMA_PLOTS_GET %u %u",&after,&boot);JsonDocument out;plotSerialReply(out,after,boot);serialOutput="";serializeJson(out,serialOutput);serialOutput="@ELMA_PLOTS "+serialOutput;serialOutput+='\n';}else if(serialLine=="ELMA_CONFIG_SNAPSHOT"){JsonDocument out;out.set(config);out["usingSavedSettings"]=true;serialOutput="";serializeJson(out,serialOutput);serialOutput="[elma-config-settings] "+serialOutput;serialOutput+="\n[elma-config-logics] {\"graph\":";serialSnapshot=LittleFS.open("/logics.json","r");if(!serialSnapshot)serialOutput+="{\"nodes\":[],\"connections\":[]}}\n";}else if(serialLine.startsWith("ELMA_CLONE_CONFIG ")){unsigned bytes=0,crc=0;if(sscanf(serialLine.c_str(),"ELMA_CLONE_CONFIG %u %x",&bytes,&crc)==2&&bytes<=kRequestLimit&&bytes>0){std::string why;runtime.begin("{\"schemaVersion\":1,\"nodes\":[],\"connections\":[]}",action,why);status.clear();LedArrays::reset();if(ESP.getFreeHeap()<12000){String ignored;loadSavedGraph(ignored);Serial.println("[clone] error=Not enough free runtime memory for configuration");serialLine="";continue;}serialInput=LittleFS.open("/serial-upload.tmp","w");if(!serialInput){String ignored;loadSavedGraph(ignored);Serial.println("[clone] error=Cannot stage configuration");serialLine="";continue;}provisioningBusy=true;if(WiFi.status()==WL_CONNECTED)WiFi.softAPdisconnect(true);serialExpected=bytes;serialCrc=crc;serialReceived=0;serialChunkSize=0;serialRunningCrc=~0u;serialWriteFailed=false;serialAt=millis();Serial.printf("[clone] receiving configuration bytes=%u\n",bytes);}else Serial.println("[clone] error=ESP8266 configuration exceeds memory limit");}serialLine="";if(serialOutput.length())break;}else if(c!='\r'){if(serialLine.length()<160)serialLine+=c;else serialLine="";}}}
}
void setup(){
#ifdef ESP32
stateMutex=xSemaphoreCreateRecursiveMutex();
#endif
bootId=micros()^chipId();Serial.begin(115200);Serial.setRxBufferSize(512);WiFi.persistent(false);
#ifdef ESP8266
fsReady=LittleFS.begin();
#else
fsReady=LittleFS.begin(true,"/littlefs",4,"littlefs");
#endif
if(!recoverConfiguration())fault="Configuration recovery failed";
defaults();String saved=readFile("/settings.json",kConfigLimit);if(saved.length()){JsonDocument d;if(!deserializeJson(d,saved))config.set(d);}saved="";mode=readFile("/mode",16);if(mode!="paused"&&mode!="stopped")mode="playing";setLed(false);connectWifi();
if(!LittleFS.exists("/logics.json")&&config["windowsLogic"].is<JsonObject>()){if(!saveJsonFile("/logics.json",config["windowsLogic"]))fault="Cannot persist default Logics";}
config.remove("windowsLogic");packPeripheralMetadata(config);{JsonDocument lean;lean.set(config);config=std::move(lean);}loadSavedGraph(fault);setupHttp();Serial.println("[clone] provisioning ready");}
void loop(){StateGuard guard;serialTick();if(pendingSettings.length()){settingsApplying=true;provisioningBusy=true;Serial.println("[settings] applying");JsonDocument incoming;auto parsed=deserializeJson(incoming,pendingSettings);pendingSettings="";if(parsed)settingsResult="Invalid settings JSON";else if(!applyConfiguration(incoming.as<JsonVariantConst>(),settingsResult,nullptr,&incoming)){}incoming.clear();settingsApplying=false;provisioningBusy=false;Serial.println("[settings] complete "+settingsResult);return;}if(provisioningBusy){yield();return;}uint32_t now=millis();if(applyArrayDefaultsPending){applyArrayDefaultsPending=false;if(mode=="playing")for(JsonObjectConst n:runtime.nodes())if(LedArrays::matches(n)){JsonDocument args;args.set(n["binding"]["defaults"]);args["action"]="strip";std::string why;if(!LedArrays::action(n,args.as<JsonVariantConst>(),why))fault=why.c_str();}}LedArrays::tick(now,mode=="playing");if(wifiReconfigureAt&&int32_t(now-wifiReconfigureAt)>=0){wifiReconfigureAt=0;connectWifi();}if(now-lastTick>=25){lastTick=now;if(!ledManual){bool wanted=WiFi.status()==WL_CONNECTED||((now/500)%2);if(wanted!=ledOn)setLed(wanted);}updateStatus();if(mode=="playing")runtime.tick(now,status.as<JsonVariantConst>());fault=runtime.error().c_str();}if(now-lastNetwork>=5000){lastNetwork=now;networkReport();}if(WiFi.status()==WL_CONNECTED&&WiFi.getMode()==WIFI_AP_STA&&WiFi.softAPgetStationNum()==0)WiFi.softAPdisconnect(true);yield();}

#endif // ESP8266
