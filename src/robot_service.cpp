#include "robot_service.h"
#include "robot_control_policy.h"
#if defined(APP_ROBOT_BOARD) && defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(APP_DISABLE_WEB_UI)
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <driver/mcpwm.h>
#include <driver/ledc.h>
#include <WiFi.h>
#include <atomic>


namespace MiniExco {
namespace {
constexpr int motorPins[6]={16,17,18,19,43,44};
constexpr uint32_t leaseMs=RobotControlPolicy::leaseMs;
struct Command { int right=0,left=0,arm=0,bucket=90,aux=90;uint32_t color=0;uint8_t lightMode=0;uint32_t owner=0,at=0; };
Command desired;portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
AsyncWebSocket socket("/api/robot/ws");
Adafruit_NeoPixel lights(12,21,NEO_GRB+NEO_KHZ800);
TaskHandle_t controlTask=nullptr;
bool started=false,servoActive=false;uint32_t servoAt=0,lastTick=0;
std::atomic<float> chargerVoltage{0},batteryVoltage{0};
int bucket=90,aux=90;std::atomic<bool> inhibited{false};
void motors(int right,int left,int arm) {
 static int previous[3]={};
 const int values[3]={right,left,arm};
 for(int i=0;i<3;i++) {int value=constrain(values[i],-255,255);if(previous[i]&&value&&((previous[i]<0)!=(value<0)))value=0;ledcWrite(2+i*2,0);ledcWrite(3+i*2,0);if(value)ledcWrite(2+i*2+(value<0),abs(value));previous[i]=value;}
}
void initialize(){
 Serial.flush();Serial.end(); // UART0 shares the arm driver's GPIO43/44.
 for(int i=0;i<6;i++){pinMode(motorPins[i],OUTPUT);digitalWrite(motorPins[i],LOW);ledcSetup(2+i,20000,8);ledcAttachPin(motorPins[i],2+i);ledcWrite(2+i,0);}
 // Camera owns LEDC timer 0. Servos use the independent MCPWM peripheral.
 mcpwm_gpio_init(MCPWM_UNIT_0,MCPWM0A,0);mcpwm_gpio_init(MCPWM_UNIT_0,MCPWM0B,20);
 mcpwm_config_t config={};config.frequency=50;config.counter_mode=MCPWM_UP_COUNTER;config.duty_mode=MCPWM_DUTY_MODE_0;
 mcpwm_init(MCPWM_UNIT_0,MCPWM_TIMER_0,&config);
 mcpwm_set_signal_low(MCPWM_UNIT_0,MCPWM_TIMER_0,MCPWM_OPR_A);mcpwm_set_signal_low(MCPWM_UNIT_0,MCPWM_TIMER_0,MCPWM_OPR_B);
 lights.begin();lights.clear();lights.show();started=true;
}
}
static void service();
void begin(){
 if(started)return;initialize();
 if(xTaskCreatePinnedToCore([](void*){for(;;){service();vTaskDelay(pdMS_TO_TICKS(10));}},"robot-control",4096,nullptr,3,&controlTask,1)!=pdPASS){motors(0,0,0);controlTask=nullptr;}
}
void setBatteryVoltage(float value){batteryVoltage=value;}
void stop(){portENTER_CRITICAL(&lock);desired.right=desired.left=desired.arm=0;desired.owner=0;desired.at=0;portEXIT_CRITICAL(&lock);}
void tick(bool updating){inhibited=updating;if(updating)stop();}
static void service(){
 const bool updating=inhibited.load();
 if(updating||WiFi.getMode()==WIFI_OFF){stop();if(started){motors(0,0,0);mcpwm_set_signal_low(MCPWM_UNIT_0,MCPWM_TIMER_0,MCPWM_OPR_A);mcpwm_set_signal_low(MCPWM_UNIT_0,MCPWM_TIMER_0,MCPWM_OPR_B);servoActive=false;}return;}
 uint32_t now=millis();if(now-lastTick<20)return;lastTick=now;
 static uint32_t adcAt=0;if(now-adcAt>=500){adcAt=now;chargerVoltage=analogReadMilliVolts(1)*0.001f*(16800.f/6800.f);}
 Command command;portENTER_CRITICAL(&lock);if(desired.owner&&RobotControlPolicy::expired(now,desired.at)){desired.right=desired.left=desired.arm=0;desired.owner=0;}command=desired;portEXIT_CRITICAL(&lock);
 if(!started){if(!command.owner)return;initialize();}
 motors(command.right,command.left,command.arm);
 if(command.owner&&(bucket!=command.bucket||aux!=command.aux)){
  bucket=command.bucket;aux=command.aux;servoAt=now;servoActive=true;
  mcpwm_set_duty_type(MCPWM_UNIT_0,MCPWM_TIMER_0,MCPWM_OPR_A,MCPWM_DUTY_MODE_0);mcpwm_set_duty_type(MCPWM_UNIT_0,MCPWM_TIMER_0,MCPWM_OPR_B,MCPWM_DUTY_MODE_0);
  mcpwm_set_duty_in_us(MCPWM_UNIT_0,MCPWM_TIMER_0,MCPWM_OPR_A,1000+bucket*1000/180);mcpwm_set_duty_in_us(MCPWM_UNIT_0,MCPWM_TIMER_0,MCPWM_OPR_B,1000+aux*1000/180);
 }
 if(servoActive&&(!command.owner||now-servoAt>500)){mcpwm_set_signal_low(MCPWM_UNIT_0,MCPWM_TIMER_0,MCPWM_OPR_A);mcpwm_set_signal_low(MCPWM_UNIT_0,MCPWM_TIMER_0,MCPWM_OPR_B);servoActive=false;}
 static uint32_t lightAt=0;
 if(now-lightAt>=100){lightAt=now;lights.clear();
  if(command.lightMode==1)lights.fill(command.color);
  else if(command.lightMode==2)lights.fill(lights.Color(255,255,255));
  else if(command.lightMode==3 && (now/400)%2)for(int pixel:{0,5,6,11})lights.setPixelColor(pixel,lights.Color(255,180,0));
  else if(command.lightMode==4){for(int pixel=0;pixel<12;pixel++)lights.setPixelColor(pixel,((pixel/3+now/200)%2)?lights.Color(255,0,0):lights.Color(0,0,255));}
  lights.setBrightness(80);lights.show();
 }
 socket.cleanupClients(2);
}
void routes(AsyncWebServer& server,std::function<bool(AsyncWebServerRequest*)> authorize,std::function<bool()> unlocked){
 socket.addMiddleware([authorize](AsyncWebServerRequest* r,ArMiddlewareNext next){if(!authorize(r))return;if(r->hasHeader("Origin")&&r->header("Origin")!="http://"+r->host()&&r->header("Origin")!="https://"+r->host()){r->send(403);return;}if(socket.count()>=2){r->send(503);return;}next();});
 socket.onEvent([unlocked](AsyncWebSocket*,AsyncWebSocketClient* client,AwsEventType event,void* arg,uint8_t* data,size_t length){
  if(event==WS_EVT_DISCONNECT){portENTER_CRITICAL(&lock);if(desired.owner==client->id()){desired.right=desired.left=desired.arm=0;desired.owner=0;}portEXIT_CRITICAL(&lock);return;}
  if(event!=WS_EVT_DATA)return;auto* frame=static_cast<AwsFrameInfo*>(arg);
  if(frame->opcode!=WS_TEXT||!frame->final||frame->index||frame->len!=length||length>256){client->close(1009,"Invalid robot command");return;}
  StaticJsonDocument<384> doc;if(deserializeJson(doc,data,length)){client->close(1008,"Invalid JSON");return;}
  if(doc["stop"]==true){stop();return;}if(!controlTask||inhibited||!unlocked()){stop();client->close(1008,"Robot unavailable or locked");return;}
  for(const char* key:{"left","right","arm","bucket","aux"})if(!doc[key].is<int>()){client->close(1008,"Missing controls");return;}
  int right=doc["right"],left=doc["left"],arm=doc["arm"],b=doc["bucket"],a=doc["aux"];
  if(!RobotControlPolicy::valid(left,right,arm,b,a)){client->close(1008,"Control outside range");return;}
  bool busy=false;portENTER_CRITICAL(&lock);uint32_t now=millis();busy=RobotControlPolicy::ownedByOther(desired.owner,client->id(),now,desired.at);
  if(!busy){desired.right=right;desired.left=left;desired.arm=arm;desired.bucket=b;desired.aux=a;desired.owner=client->id();desired.at=now;if(doc["color"].is<uint32_t>())desired.color=doc["color"].as<uint32_t>()&0xffffff;if(doc["lightMode"].is<uint8_t>()&&doc["lightMode"].as<uint8_t>()<=4)desired.lightMode=doc["lightMode"].as<uint8_t>();}portEXIT_CRITICAL(&lock);
  if(busy)client->text("{\"busy\":true}");
 });server.addHandler(&socket);
 server.on("/api/robot",HTTP_GET,[authorize](AsyncWebServerRequest* r){if(!authorize(r))return;StaticJsonDocument<512> doc;doc["board"]="miniexco-v2";doc["leaseMs"]=leaseMs;doc["ready"]=started&&controlTask&&!inhibited.load();Command command;portENTER_CRITICAL(&lock);command=desired;portEXIT_CRITICAL(&lock);doc["moving"]=command.right||command.left||command.arm;doc["bucket"]=bucket;doc["aux"]=aux;doc["batteryVoltage"]=batteryVoltage.load();doc["chargerVoltage"]=chargerVoltage.load();String body;serializeJson(doc,body);auto* response=r->beginResponse(200,"application/json",body);response->addHeader("Cache-Control","no-store");r->send(response);});
}
}
#else
namespace MiniExco {void begin(){}void tick(bool){}void stop(){}void setBatteryVoltage(float){}
#ifndef APP_DISABLE_WEB_UI
void routes(AsyncWebServer&,std::function<bool(AsyncWebServerRequest*)>,std::function<bool()>){ }
#endif
}
#endif
