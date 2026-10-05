#include "bno055_service.h"
#include <Wire.h>
#include "shared_i2c.h"
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include "bno055_units.h"

// Register definitions and units: Bosch BNO055 datasheet / BNO055_driver.
// Transactions run only in the main loop, alongside OLED updates. HTTP handlers
// read a cached snapshot and queue controls; they never touch the shared bus.
namespace Bno055 { namespace {
struct State {bool configured=false,enabled=true,ready=false;uint8_t address=0,mode=12,calibration=0;float accel[3]{},gyro[3]{},mag[3]{},heading=0,roll=0,pitch=0;uint32_t sampled=0;char error[120]{};};
State state;portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
int sda=-1,scl=-1,phase=0;uint32_t next=0;bool validBus=false;int pendingMode=-1,pendingEnabled=-1;bool pendingRestart=false;int selectedAddress=-1;
void problem(const char* text){portENTER_CRITICAL(&mux);state.ready=false;strlcpy(state.error,text,sizeof(state.error));portEXIT_CRITICAL(&mux);}
bool read(uint8_t reg,uint8_t* bytes,uint8_t size){SharedI2c::wire().setClock(100000);SharedI2c::wire().beginTransmission(state.address);SharedI2c::wire().write(reg);if(SharedI2c::wire().endTransmission(false))return false;if(SharedI2c::wire().requestFrom(state.address,size)!=size)return false;for(int i=0;i<size;i++)bytes[i]=SharedI2c::wire().read();return true;}
bool write(uint8_t reg,uint8_t value){SharedI2c::wire().setClock(100000);SharedI2c::wire().beginTransmission(state.address);SharedI2c::wire().write(reg);SharedI2c::wire().write(value);return SharedI2c::wire().endTransmission()==0;}
}
void configure(const SettingsBundle& settings){
 JsonDocument profiles,bindings;deserializeJson(profiles,settings.ui.peripheralProfileSelections);deserializeJson(bindings,settings.ui.peripheralHelperBindings);
 int index=-1,count=0;for(JsonVariantConst profile:profiles["sensors"].as<JsonArrayConst>()){if(profile=="bno055"){index=count;break;}count++;}
 portENTER_CRITICAL(&mux);const bool enabled=state.enabled;const uint8_t mode=state.mode;state=State{};state.enabled=enabled;state.mode=mode;state.configured=index>=0;pendingMode=pendingEnabled=-1;pendingRestart=false;portEXIT_CRITICAL(&mux);validBus=false;phase=0;next=millis();
 if(index<0)return;
 auto pins=bindings["sensor:"+String(index)];sda=pins["SDA"].isNull()?-1:pins["SDA"].as<int>();scl=pins["SCL"].isNull()?-1:pins["SCL"].as<int>();
 selectedAddress=pins["I2C_ADDRESS"].isNull()||pins["I2C_ADDRESS"]==""?-1:pins["I2C_ADDRESS"].is<const char*>()?int(strtol(pins["I2C_ADDRESS"].as<const char*>(),nullptr,0)):pins["I2C_ADDRESS"].as<int>();
 if(sda==scl||!GPIO_IS_VALID_OUTPUT_GPIO(sda)||!GPIO_IS_VALID_OUTPUT_GPIO(scl)){problem("Assign two valid SDA/SCL GPIOs in Configuration");return;}
 const bool oled=settings.oled.enabled&&settings.oled.displayType!="panel"&&settings.oled.displayType!="wape";
 if(oled&&(sda!=settings.oled.sdaPin||scl!=settings.oled.sclPin)){problem("BNO055 and OLED must share the same SDA/SCL pair");return;}
 if(oled&&(settings.oled.i2cAddress==0x28||settings.oled.i2cAddress==0x29)){problem("OLED address conflicts with BNO055");return;}
 size_t sensorIndex=0;for(JsonVariantConst profile:profiles["sensors"].as<JsonArrayConst>()){
  if(profile=="ds3231-rtc"){auto rtcPins=bindings["sensor:"+String(sensorIndex)];if(rtcPins["SDA"].as<int>()!=sda||rtcPins["SCL"].as<int>()!=scl){problem("RTC and BNO055 must share the same SDA/SCL pair");return;}}
  ++sensorIndex;
 }
 SharedI2c::Guard bus;if(!bus||!SharedI2c::begin(sda,scl)){problem("Could not initialize I2C bus");return;}
 SharedI2c::wire().setTimeOut(10);validBus=true;
}
void tick(){
 if(!state.configured||!validBus)return;
 SharedI2c::Guard bus(0);if(!bus)return;
 int mode,enabled;bool restart;portENTER_CRITICAL(&mux);mode=pendingMode;enabled=pendingEnabled;restart=pendingRestart;pendingMode=pendingEnabled=-1;pendingRestart=false;portEXIT_CRITICAL(&mux);
 if(enabled>=0){portENTER_CRITICAL(&mux);state.enabled=enabled;state.ready=false;portEXIT_CRITICAL(&mux);phase=0;next=millis();}
 if(mode>=0){portENTER_CRITICAL(&mux);state.mode=mode;state.ready=false;portEXIT_CRITICAL(&mux);phase=0;next=millis();}
 if(restart&&state.address){write(0x3d,0);write(0x3f,0x20);phase=0;next=millis()+1000;problem("Reinitializing sensor; calibration will restart");}
 if(!state.enabled||int32_t(millis()-next)<0)return;
 if(phase==0){
  uint8_t id=0;bool found=false;for(uint8_t address:{uint8_t(0x28),uint8_t(0x29)}){if(selectedAddress>=0&&address!=selectedAddress)continue;portENTER_CRITICAL(&mux);state.address=address;portEXIT_CRITICAL(&mux);if(read(0,&id,1)&&id==0xa0){found=true;break;}}
  if(!found){problem("BNO055 not detected at 0x28/0x29. Check power, SDA and SCL.");next=millis()+2000;return;}
  if(!write(0x07,0)||!write(0x3d,0)){problem("I2C configuration write failed");next=millis()+2000;return;}phase=1;next=millis()+30;return;
 }
 if(phase==1){if(!write(0x3e,0)||!write(0x3b,0)||!write(0x3d,state.mode)){problem("I2C mode write failed");phase=0;next=millis()+2000;return;}phase=2;next=millis()+30;return;}
 uint8_t bytes[24],calibration=0;if(!read(0x08,bytes,sizeof(bytes))||!read(0x35,&calibration,1)){problem("Sensor read failed; retrying");phase=0;next=millis()+1000;return;}
 auto decoded=Bno055Units::decode(bytes);
 portENTER_CRITICAL(&mux);for(int i=0;i<3;i++){state.accel[i]=decoded.accel[i];state.mag[i]=decoded.mag[i];state.gyro[i]=decoded.gyro[i];}
 state.heading=decoded.heading;state.roll=decoded.roll;state.pitch=decoded.pitch;state.calibration=calibration;state.ready=true;state.sampled=millis();state.error[0]=0;portEXIT_CRITICAL(&mux);next=millis()+50;
}
void snapshot(JsonObject root){
 State data;portENTER_CRITICAL(&mux);data=state;portEXIT_CRITICAL(&mux);
 root["configured"]=data.configured;root["enabled"]=data.enabled;root["ready"]=data.ready;root["address"]=data.address;root["mode"]=data.mode==12?"ndof":"imu";root["compass"]=data.mode==12;root["error"]=data.error;root["ageMs"]=data.sampled?millis()-data.sampled:0;
 root["heading"]=data.heading;root["roll"]=data.roll;root["pitch"]=data.pitch;
 auto accel=root["accel"].to<JsonArray>(),gyro=root["gyro"].to<JsonArray>(),mag=root["mag"].to<JsonArray>();for(int i=0;i<3;i++){accel.add(data.accel[i]);gyro.add(data.gyro[i]);mag.add(data.mag[i]);}
 auto cal=root["calibration"].to<JsonObject>();cal["system"]=(data.calibration>>6)&3;cal["gyro"]=(data.calibration>>4)&3;cal["accel"]=(data.calibration>>2)&3;cal["mag"]=data.calibration&3;
}
bool command(JsonVariantConst args,String& error){
 bool configured;portENTER_CRITICAL(&mux);configured=state.configured;portEXIT_CRITICAL(&mux);
 if(!configured){error="Add DFRobot BNO055 in Configuration first";return false;}
 String mode=args["mode"]|"";if(mode.length()&&mode!="ndof"&&mode!="imu"){error="Invalid fusion mode";return false;}
 if(!args["enabled"].isNull()&&!args["enabled"].is<bool>()){error="Enabled must be true or false";return false;}
 portENTER_CRITICAL(&mux);if(mode.length())pendingMode=mode=="ndof"?12:8;if(args["enabled"].is<bool>())pendingEnabled=args["enabled"].as<bool>();if(args["reinitialize"]==true)pendingRestart=true;portEXIT_CRITICAL(&mux);return true;
}
}
