#include "rs485_service.h"
#include "modbus_rtu.h"
#include <Arduino.h>
#include <cmath>
#if APP_HAS_ONBOARD_PANEL && !APP_SUNTON_PANEL && !APP_ROTARY_HMI
#include <HardwareSerial.h>
#include <Preferences.h>
#include <freertos/semphr.h>
namespace {
HardwareSerial bus(1);
SemaphoreHandle_t mutex=nullptr;
struct Config {uint32_t baud=9600;uint16_t address=0,count=1;uint8_t unit=1,function=3;char parity='N';uint8_t stops=1;} config;
struct Frame {uint32_t sequence=0,at=0;String direction,hex,info;bool valid=false;uint8_t unit=0,function=0;};
Frame history[8],received;
uint32_t sequence=0,validSequence=0,began=0,lastByte=0;
bool pending=false,transmit=false,waiting=false,ready=false,opened=false,serialDirty=false;
uint8_t response[256];uint16_t values[32];size_t used=0;
String message="Listening. No request sent.";
bool baudValid(int baud){for(int b:{1200,2400,4800,9600,19200,38400,57600,115200})if(baud==b)return true;return false;}
void init(){static const bool once=[](){
 mutex=xSemaphoreCreateMutex();if(!mutex)return false;
 Preferences p;if(p.begin("elma_rs485",true)){config.baud=p.getUInt("baud",9600);config.parity=p.getChar("parity",'N');config.stops=p.getUChar("stop",1);p.end();}
 if(!baudValid(config.baud))config.baud=9600;
 if(config.parity!='N'&&config.parity!='E'&&config.parity!='O')config.parity='N';
 if(config.stops!=1&&config.stops!=2)config.stops=1;
 return true;}();(void)once;}
struct Guard {bool ok;Guard(){init();ok=mutex&&xSemaphoreTake(mutex,0)==pdTRUE;}~Guard(){if(ok)xSemaphoreGive(mutex);}};
bool crcValid(const uint8_t* data,size_t size){return size>=4&&ModbusRtu::crc(data,size-2)==uint16_t(data[size-2]|data[size-1]<<8);}
void logFrame(const char* direction,const uint8_t* data,size_t size,const String& info,bool valid){
 Frame frame;frame.sequence=++sequence;frame.at=millis();frame.direction=direction;frame.info=info;frame.valid=valid;
 if(size){frame.unit=data[0];if(size>1)frame.function=data[1];frame.hex.reserve(min(size,size_t(96))*3+4);
 for(size_t i=0;i<min(size,size_t(96));i++){char byte[4];snprintf(byte,sizeof(byte),"%02X ",data[i]);frame.hex+=byte;}if(size>96)frame.hex+="...";}
 history[(sequence-1)%8]=frame;
 if(String(direction)=="RX"&&valid){received=frame;++validSequence;}
}
void finishFrame(){
 bool valid=crcValid(response,used);String info=valid?"Unsolicited frame":"CRC error / incomplete frame";
 if(waiting&&used>=2&&response[0]==config.unit&&(response[1]&0x7f)==config.function){
  waiting=false;
  if(valid&&used==5&&response[1]==(config.function|0x80))message="Modbus exception "+String(response[2]);
  else {ready=ModbusRtu::validReply(response,used,config.unit,config.function,config.count);
   message=ready?"Reply received":"Invalid reply: function, length or CRC mismatch";
   if(ready)for(int i=0;i<config.count;i++)values[i]=config.function<=2?((response[3+i/8]>>(i%8))&1):uint16_t(response[3+i*2]<<8|response[4+i*2]);
  }info=message;
 }
 logFrame("RX",response,used,info,valid);used=0;
}
void openBus(){
 uint32_t mode=config.parity=='E'?(config.stops==2?SERIAL_8E2:SERIAL_8E1):config.parity=='O'?(config.stops==2?SERIAL_8O2:SERIAL_8O1):(config.stops==2?SERIAL_8N2:SERIAL_8N1);
 bus.end();bus.setRxBufferSize(512);bus.begin(config.baud,mode,44,43);opened=true;used=0;
 // The MS1285 circuit controls direction automatically; there is no DE GPIO.
}
}
#endif
namespace Rs485 {
bool available(){
#if APP_HAS_ONBOARD_PANEL && !APP_SUNTON_PANEL && !APP_ROTARY_HMI
 return true;
#else
 return false;
#endif
}
bool command(JsonVariantConst args,String& error){
#if APP_HAS_ONBOARD_PANEL && !APP_SUNTON_PANEL && !APP_ROTARY_HMI
 Guard guard;if(!guard.ok||pending||waiting){error="RS485 is busy";return false;}
 int baud=args["baud"]|int(config.baud),unit=args["unit"]|1,address=args["address"]|0,count=args["count"]|1,function=args["function"]|3,stops=args["stops"]|int(config.stops);String parity=args["parity"]|String(config.parity);
 for(const char* key:{"baud","unit","address","count","function","stops"})if(!args[key].isNull()){double value=args[key].as<double>();if(!args[key].is<double>()||!std::isfinite(value)||std::floor(value)!=value){error="Modbus numeric parameters must be whole numbers";return false;}}
 String action=args["action"]|"read";if(action!="read"&&action!="configure"){error="Unsupported RS485 action";return false;}
 if(!baudValid(baud)||unit<1||unit>247||address<0||address>65535||count<1||count>32||address+count>65536||function<1||function>4||(stops!=1&&stops!=2)||(parity!="N"&&parity!="E"&&parity!="O")){error="Invalid Modbus parameters (unit 1-247; address 0-65535; count 1-32)";return false;}
 serialDirty=config.baud!=uint32_t(baud)||config.parity!=parity[0]||config.stops!=stops;
 config={uint32_t(baud),uint16_t(address),uint16_t(count),uint8_t(unit),uint8_t(function),parity[0],uint8_t(stops)};
 pending=true;transmit=action=="read";ready=false;message=transmit?"Queued":"Applying serial settings";return true;
#else
 error="No built-in RS485 transceiver on this board";return false;
#endif
}
void tick(){
#if APP_HAS_ONBOARD_PANEL && !APP_SUNTON_PANEL && !APP_ROTARY_HMI
 Guard guard;if(!guard.ok)return;
 if(!opened)openBus();
 if(pending){
  pending=false;if(serialDirty)openBus();
  if(serialDirty){Preferences p;if(p.begin("elma_rs485",false)){p.putUInt("baud",config.baud);p.putChar("parity",config.parity);p.putUChar("stop",config.stops);p.end();}serialDirty=false;}
  if(transmit){uint8_t tx[8];ModbusRtu::readRequest(tx,config.unit,config.function,config.address,config.count);bus.write(tx,sizeof(tx));logFrame("TX",tx,sizeof(tx),"Read request",true);began=lastByte=millis();waiting=true;message="Waiting for reply";}
  else message="Listening";
 }
 // Bound work per loop; never wait for serial bytes or a response.
 for(int n=0;n<256&&bus.available();n++){
  int byte=bus.read();if(used==sizeof(response))finishFrame();response[used++]=byte;lastByte=millis();
  // Recognize common complete RTU frames, including batched arrivals.
  size_t expected=used>=3?((response[1]&0x80)?5:(response[1]>=1&&response[1]<=4?size_t(response[2])+5:0)):0;
  if((used==expected||used==8)&&crcValid(response,used))finishFrame();
 }
 uint32_t now=millis(),gap=config.baud>19200?2:max<uint32_t>(2,40000/config.baud);
 if(used&&now-lastByte>=gap)finishFrame();
 if(waiting&&now-began>1500){waiting=false;message="Timeout: check unit, baud/parity and A/B wiring";logFrame("ERROR",nullptr,0,message,false);}
#endif
}
void snapshot(JsonObject out,bool includeLog){out["available"]=available();
#if APP_HAS_ONBOARD_PANEL && !APP_SUNTON_PANEL && !APP_ROTARY_HMI
 Guard guard;if(!guard.ok){out["busy"]=true;return;}
 out["chip"]="MS1285";out["tx"]=43;out["rx"]=44;out["automaticDirection"]=true;out["busy"]=pending||waiting;out["message"]=message;
 out["baud"]=config.baud;out["parity"]=String(config.parity);out["stops"]=config.stops;out["unit"]=config.unit;out["address"]=config.address;out["count"]=config.count;out["function"]=config.function;out["ready"]=ready;
 auto data=out["values"].to<JsonArray>();if(ready)for(int i=0;i<config.count;i++)data.add(values[i]);
 out["rxSequence"]=validSequence;auto rx=out["received"].to<JsonObject>();rx["unit"]=received.unit;rx["function"]=received.function;rx["hex"]=received.hex;rx["valid"]=received.sequence>0;rx["ageMs"]=received.sequence?millis()-received.at:0;
 if(includeLog){auto logs=out["frames"].to<JsonArray>();uint32_t first=sequence>8?sequence-7:1;for(uint32_t i=first;i<=sequence;i++){const auto& frame=history[(i-1)%8];auto entry=logs.add<JsonObject>();entry["sequence"]=frame.sequence;entry["atMs"]=frame.at;entry["direction"]=frame.direction;entry["hex"]=frame.hex;entry["info"]=frame.info;entry["valid"]=frame.valid;}}
#endif
}
}
