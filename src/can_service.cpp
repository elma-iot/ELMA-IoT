#include "can_service.h"
#include "can_contract.h"
#include <cmath>
#include <driver/gpio.h>
#include <driver/twai.h>
#include <freertos/semphr.h>

// Commands originate in the loop-owned Logics runtime. No UART, audio or SPI
// resources are used. Driver queues and the work per loop are bounded.
namespace CanBus { namespace {
int tx=-1,rx=-1;bool selected=false,installed=false;String message="Not configured";
uint32_t sequence=0;twai_message_t latest{};
SemaphoreHandle_t mutex(){static StaticSemaphore_t storage;static SemaphoreHandle_t handle=xSemaphoreCreateMutexStatic(&storage);return handle;}
struct Guard {bool held=false;Guard(){held=xSemaphoreTake(mutex(),0)==pdTRUE;}~Guard(){if(held)xSemaphoreGive(mutex());}explicit operator bool()const{return held;}};
bool integer(JsonVariantConst v,int64_t lo,int64_t hi){if(v.is<bool>()||!v.is<double>())return false;double d=v.as<double>();return std::isfinite(d)&&d>=lo&&d<=hi&&std::floor(d)==d;}
bool start(int bitrate,bool listen,String& error){
 if(installed){twai_stop();twai_driver_uninstall();installed=false;}
 twai_timing_config_t timing=TWAI_TIMING_CONFIG_500KBITS();
 switch(bitrate){case 125000:timing=TWAI_TIMING_CONFIG_125KBITS();break;case 250000:timing=TWAI_TIMING_CONFIG_250KBITS();break;case 500000:break;case 1000000:timing=TWAI_TIMING_CONFIG_1MBITS();break;default:error="Unsupported CAN bitrate";return false;}
 twai_general_config_t config=TWAI_GENERAL_CONFIG_DEFAULT(gpio_num_t(tx),gpio_num_t(rx),listen?TWAI_MODE_LISTEN_ONLY:TWAI_MODE_NORMAL);
 config.tx_queue_len=8;config.rx_queue_len=16;
 twai_filter_config_t filter=TWAI_FILTER_CONFIG_ACCEPT_ALL();
 esp_err_t code=twai_driver_install(&config,&timing,&filter);
 if(code==ESP_OK){installed=true;code=twai_start();}
 if(code!=ESP_OK){if(installed)twai_driver_uninstall();installed=false;error=String("CAN start failed: ")+esp_err_to_name(code);message=error;return false;}
 message=listen?"Listening (no transmit or ACK)":"Running";return true;
}
}
bool available(){return selected;}
void configure(const SettingsBundle& settings){
 Guard guard;if(!guard)return;
 JsonDocument profiles,bindings;deserializeJson(profiles,settings.ui.peripheralProfileSelections);deserializeJson(bindings,settings.ui.peripheralHelperBindings);
 int index=-1,i=0,count=0;for(JsonVariantConst p:profiles["communication"].as<JsonArrayConst>()){if(p=="mcp2551"){index=i;count++;}i++;}
 int newTx=-1,newRx=-1;if(index>=0){auto pins=bindings["communication:"+String(index)];newTx=CanContract::pin(pins["CTX"]);newRx=CanContract::pin(pins["CRX"]);}
 bool valid=count==1&&newTx!=newRx&&GPIO_IS_VALID_OUTPUT_GPIO(newTx)&&GPIO_IS_VALID_GPIO(newRx);
 if(valid&&selected&&tx==newTx&&rx==newRx)return;
 if(installed){twai_stop();twai_driver_uninstall();installed=false;}
 selected=valid;tx=newTx;rx=newRx;latest={};sequence=0;
 message=count>1?"Only one CAN transceiver is supported":index>=0?"Assign valid CTX and CRX GPIOs":"Not configured";
 if(valid){String error;start(500000,true,error);}
}
void tick(){
 Guard guard;if(!guard)return;
 if(!installed)return;
 twai_message_t frame{};
 for(int i=0;i<8&&twai_receive(&frame,0)==ESP_OK;i++){latest=frame;++sequence;}
}
bool command(JsonVariantConst args,String& error){
 Guard guard;if(!guard){error="CAN controller is busy";return false;}
 if(!selected){error="Configure one MCP2551 with valid GPIOs first";return false;}
 String action=args["action"]|"";
 if(action=="configure"){
  if(!CanContract::valid("hardware.can.configure",args)){error="Invalid CAN settings";return false;}
  return start(args["bitrate"],args["listenOnly"],error);
 }
 if(action!="send"||!installed){error="CAN controller is not running";return false;}
 twai_message_t frame{};
 if(!args["extended"].is<bool>()||!args["remote"].is<bool>()||!integer(args["identifier"],0,args["extended"]==true?0x1fffffff:0x7ff)||!integer(args["length"],0,8)||!args["data"].is<const char*>()){error="Invalid CAN frame";return false;}
 frame.identifier=args["identifier"];frame.extd=args["extended"].as<bool>();frame.rtr=args["remote"].as<bool>();frame.ss=1;
 uint8_t length=0;if(!CanContract::bytes(args["data"],frame.data,length)){error="Invalid CAN data bytes";return false;}
 if(frame.rtr&&length){error="Remote frames require empty Data";return false;}
 frame.data_length_code=frame.rtr?args["length"].as<uint8_t>():length;
 esp_err_t code=twai_transmit(&frame,0);
 if(code!=ESP_OK){error=String("CAN transmit rejected: ")+esp_err_to_name(code);message=error;return false;}
 message="Frame queued (single attempt)";return true;
}
void snapshot(JsonObject out){
 Guard guard;if(!guard){out["ready"]=false;out["message"]="CAN controller is busy";return;}
 twai_status_info_t status{};bool ready=installed&&twai_get_status_info(&status)==ESP_OK;
 out["ready"]=ready&&status.state==TWAI_STATE_RUNNING;out["busOff"]=ready&&status.state==TWAI_STATE_BUS_OFF;
 out["rxMissed"]=status.rx_missed_count;out["txFailed"]=status.tx_failed_count;out["message"]=out["busOff"]==true?"Bus off: correct wiring/bitrate, then Configure":message;
 out["rxSequence"]=sequence;
 if(sequence){auto frame=out["received"].to<JsonObject>();frame["identifier"]=latest.identifier;frame["extended"]=bool(latest.extd);frame["remote"]=bool(latest.rtr);frame["length"]=latest.data_length_code;
  char text[24]{};if(!latest.rtr)for(int i=0;i<latest.data_length_code&&i<8;i++)snprintf(text+i*3,sizeof(text)-i*3,"%02X ",latest.data[i]);String hex=text;hex.trim();frame["data"]=hex;
 }
}
}
