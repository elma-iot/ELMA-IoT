#include "plot_storage.h"
#include "storage_backend.h"
#include "storage_memory.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <sys/time.h>
#include <cmath>
#include <utility>
#include <atomic>
namespace {
std::atomic<bool> sleepGate{false};
std::atomic<unsigned> outstanding{0};
struct Pending {char plot[33],series[33],unit[17],folder[129];double value,epoch;uint32_t uptime;};
QueueHandle_t queue=nullptr;SemaphoreHandle_t io=nullptr;
StaticQueue_t queueControl;uint8_t* queueBytes=nullptr;
portMUX_TYPE statusMux=portMUX_INITIALIZER_UNLOCKED;char lastError[128]={};uint32_t written=0;
void status(const char* error){portENTER_CRITICAL(&statusMux);snprintf(lastError,sizeof(lastError),"%s",error);if(!*error)++written;portEXIT_CRITICAL(&statusMux);}
bool asciiJsonLine(String& line){
 String ascii;if(!ascii.reserve(line.length()*6+1))return false;
 const char* hex="0123456789ABCDEF";
 for(size_t i=0;i<line.length();){
  const uint8_t lead=uint8_t(line[i]);
  if(lead<128){if(!ascii.concat(char(lead)))return false;++i;continue;}
  unsigned bytes=lead>=0xF0&&lead<=0xF4?4:lead>=0xE0&&lead<=0xEF?3:lead>=0xC2&&lead<=0xDF?2:0;
  if(!bytes||i+bytes>line.length())return false;
  uint32_t code=lead&((1u<<(7-bytes))-1u);
  for(unsigned j=1;j<bytes;++j){uint8_t next=uint8_t(line[i+j]);if((next&0xC0)!=0x80)return false;code=(code<<6)|(next&0x3F);}
  if(code<(bytes==2?0x80u:bytes==3?0x800u:0x10000u)||code>0x10FFFFu||(code>=0xD800u&&code<=0xDFFFu))return false;
  auto appendEscape=[&](uint16_t value){char escaped[7]={'\\','u',hex[(value>>12)&15],hex[(value>>8)&15],hex[(value>>4)&15],hex[value&15],0};return ascii.concat(escaped);};
  if(code<=0xFFFF){if(!appendEscape(uint16_t(code)))return false;}
  else {code-=0x10000;if(!appendEscape(uint16_t(0xD800+(code>>10)))||!appendEscape(uint16_t(0xDC00+(code&0x3FF))))return false;}
  i+=bytes;
 }
 line=std::move(ascii);return true;
}
bool writeSample(const Pending& p){
 if(xSemaphoreTake(io,pdMS_TO_TICKS(100))!=pdTRUE){status("Recording storage busy");return false;}
 struct Release{~Release(){endStorageWrite(StorageTarget::Sd);xSemaphoreGive(io);}} release;
 beginStorageWrite(StorageTarget::Sd);auto* fs=getStorageFs(StorageTarget::Sd);if(!fs){status("External storage unavailable");return false;}
 String path;if(!plotRecordingPath(p.folder,p.plot,path)){status("Invalid recording path");return false;}
 for(size_t i=1;i<path.length();++i)if(path[i]=='/'){String folder=path.substring(0,i);if(!fs->exists(folder)&&!fs->mkdir(folder)){status("Cannot create recording folder");return false;}}
 File file=fs->open(path,FILE_APPEND);if(!file){status("Cannot open recording file");return false;}
 Pending current=p;
 for(unsigned batch=0;batch<8;++batch){
 const Pending& p=current;
 JsonDocument sample(storageJsonAllocator());sample["plot"]=p.plot;sample["series"]=p.series;sample["unit"]=p.unit;sample["t"]=p.uptime;sample["epoch"]=p.epoch;sample["value"]=p.value;
 String line;if(sample.overflowed()||!serializeJson(sample,line)||!asciiJsonLine(line)){file.close();status("Recording ASCII JSON memory exhausted or label is invalid UTF-8");return false;}line+='\n';size_t bytes=file.print(line);
 if(bytes!=line.length()){file.close();status("Recording write failed; check free space/card");return false;}status("");
 Pending next;if(batch==7||xQueuePeek(queue,&next,0)!=pdTRUE||strcmp(next.folder,p.folder)||strcmp(next.plot,p.plot))break;
 if(xQueueReceive(queue,&current,0)!=pdTRUE)break;
 --outstanding; // The original sample keeps this batch busy until flush/close completes.
 }
 file.flush();file.close();return true;
}
void worker(void*){Pending sample;for(;;)if(xQueueReceive(queue,&sample,portMAX_DELAY)==pdTRUE){writeSample(sample);--outstanding;vTaskDelay(1);}}
}
void beginPlotStorage(){
 static SemaphoreHandle_t initialization=xSemaphoreCreateMutex();
 if(!initialization||xSemaphoreTake(initialization,pdMS_TO_TICKS(50))!=pdTRUE)return;
 struct Unlock{SemaphoreHandle_t value;~Unlock(){xSemaphoreGive(value);}} unlock{initialization};
 if(queue)return;io=xSemaphoreCreateMutex();queueBytes=static_cast<uint8_t*>(allocateStorageBuffer(16*sizeof(Pending)));
 if(queueBytes)queue=xQueueCreateStatic(16,sizeof(Pending),queueBytes,&queueControl);
 if(!io||!queue||xTaskCreate(worker,"plot-writer",4096,nullptr,1,nullptr)!=pdPASS){if(queue)vQueueDelete(queue);queue=nullptr;if(queueBytes)heap_caps_free(queueBytes);queueBytes=nullptr;if(io)vSemaphoreDelete(io);io=nullptr;}
}
bool plotRecordingPath(const char* folder,const char* plot,String& path){
 String base=folder;if(base.isEmpty())base="/";if(base.length()>128||!base.startsWith("/")||base.indexOf("..")>=0||base.indexOf('\\')>=0)return false;
 for(size_t i=0;i<base.length();++i)if(static_cast<unsigned char>(base[i])<32)return false;
 while(base.length()>1&&base.endsWith("/"))base.remove(base.length()-1);
 size_t length=strlen(plot);if(!length||length>32)return false;
 path=base;if(!path.endsWith("/"))path+='/';path+="plot-";
 const char* hex="0123456789abcdef";for(size_t i=0;i<length;++i){unsigned char c=plot[i];path+=hex[c>>4];path+=hex[c&15];}path+=".jsonl";return true;
}
bool queuePlotRecording(JsonVariantConst args,std::string& error){
 if(!storageMounted(StorageTarget::Sd)){error="Save Data requires mounted external SD/SDMMC storage";return false;}
 Pending p{};const char* plot=args["plot"]|"";const char* series=args["series"]|"";const char* unit=args["unit"]|"";const char* folder=args["path"]|"/";String path;
 if(!plotRecordingPath(folder,plot,path)||!*series||strlen(series)>32||strlen(unit)>16||!args["value"].is<double>()||!std::isfinite(args["value"].as<double>())){error="Invalid recording labels, path or value";return false;}
 timeval now;gettimeofday(&now,nullptr);if(now.tv_sec<1577836800){error="Save Data requires synchronized device date/time";return false;}
 snprintf(p.plot,sizeof(p.plot),"%s",plot);snprintf(p.series,sizeof(p.series),"%s",series);snprintf(p.unit,sizeof(p.unit),"%s",unit);snprintf(p.folder,sizeof(p.folder),"%s",folder);
 p.value=args["value"].as<double>();p.epoch=double(now.tv_sec)*1000+now.tv_usec/1000;p.uptime=millis();
 if(!queue)beginPlotStorage();
 if(!queue){error="Plot recording writer unavailable";return false;}
 ++outstanding;
 if(sleepGate){--outstanding;error="Sleep preparation is draining recordings";return false;}
 if(xQueueSend(queue,&p,0)!=pdTRUE){--outstanding;error="Recording queue full; reduce sampling rate";return false;}return true;
}
void plotRecordingStatus(JsonObject result){char error[128];uint32_t count;portENTER_CRITICAL(&statusMux);memcpy(error,lastError,sizeof(error));count=written;portEXIT_CRITICAL(&statusMux);result["error"]=error;result["written"]=count;result["available"]=storageMounted(StorageTarget::Sd);result["queued"]=queue?uxQueueMessagesWaiting(queue):0;result["clockSynced"]=time(nullptr)>1577836800;}
static bool readPlotHistoryPage(const String& path,uint32_t offset,double from,double to,JsonDocument& response,String& error){
 if(!io)beginPlotStorage();
 if(!io||xSemaphoreTake(io,pdMS_TO_TICKS(100))!=pdTRUE){error="Recording storage busy; retry";return false;}
 struct Release{~Release(){endStorageRead(StorageTarget::Sd);xSemaphoreGive(io);}} release;beginStorageRead(StorageTarget::Sd);
 auto* fs=getStorageFs(StorageTarget::Sd);if(!fs){error="External storage unavailable";return false;}
 JsonArray samples=response["samples"].to<JsonArray>();if(!fs->exists(path)){response["eof"]=true;response["next"]=0;return true;}
 File file=fs->open(path,FILE_READ);if(!file||offset>file.size()||!file.seek(offset)){error="Cannot read recording";return false;}
 struct Reader {
  File& file;uint8_t bytes[512];size_t at=0,size=0;
  bool available(){return at<size||file.available();}
  int read(){if(at==size){size=file.read(bytes,sizeof(bytes));at=0;if(!size)return -1;}return bytes[at++];}
  uint32_t position(){return file.position()-uint32_t(size-at);}
  void seek(uint32_t pos){file.seek(pos);at=size=0;}
 } reader{file};
 uint32_t started=millis();size_t scanned=0,bytes=0;
 const size_t sampleLimit=psramFound()?64:16;
 while(reader.available()&&samples.size()<sampleLimit&&scanned<128&&bytes<32768&&uint32_t(millis()-started)<25){
  uint32_t lineStart=reader.position();String line;line.reserve(768);bool complete=false;bool oversized=false;
  while(reader.available()&&bytes<32768&&uint32_t(millis()-started)<25){int c=reader.read();if(c<0)break;++bytes;if(c=='\n'){complete=true;break;}if(line.length()<768)line+=char(c);else oversized=true;}
  ++scanned;if(!complete&&!oversized){reader.seek(lineStart);break;}if(!complete||oversized)continue;
  JsonDocument sample(storageJsonAllocator());if(deserializeJson(sample,line))continue;double epoch=sample["epoch"]|0.0;
  if(epoch>=from&&epoch<=to)samples.add(sample.as<JsonVariantConst>());
 }
 if(response.overflowed()){error="History response memory exhausted; retry a smaller range";return false;}
 response["next"]=reader.position();response["eof"]=!reader.available();file.close();return true;
}

namespace {
struct HistoryJob {
 String path,error;uint32_t offset=0,completedAt=0;double from=0,to=0;
 enum State {Idle,Pending,Ready} state=Idle;bool success=false;
 JsonDocument result{storageJsonAllocator()};
};
HistoryJob historyJob;
SemaphoreHandle_t historyMutex=nullptr;TaskHandle_t historyTask=nullptr;
void historyWorker(void*) {
 for(;;){
  ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
  xSemaphoreTake(historyMutex,portMAX_DELAY);
  String path=historyJob.path;uint32_t offset=historyJob.offset;double from=historyJob.from,to=historyJob.to;
  xSemaphoreGive(historyMutex);
  JsonDocument result(storageJsonAllocator());String error;
  bool success=readPlotHistoryPage(path,offset,from,to,result,error);
  xSemaphoreTake(historyMutex,portMAX_DELAY);
  historyJob.result=std::move(result);historyJob.error=error;historyJob.success=success;
  historyJob.completedAt=millis();historyJob.state=HistoryJob::Ready;
  xSemaphoreGive(historyMutex);
 }
}
}
bool readPlotHistory(const String& path,uint32_t offset,double from,double to,JsonDocument& response,String& error) {
 // HTTP callbacks only submit/collect bounded jobs, never access the card.
 static SemaphoreHandle_t initialization=xSemaphoreCreateMutex();
 if(!initialization||xSemaphoreTake(initialization,0)!=pdTRUE){error="History initialization busy; retry";return false;}
 if(!historyMutex)historyMutex=xSemaphoreCreateMutex();
 if(historyMutex&&!historyTask)xTaskCreate(historyWorker,"plot-history",4096,nullptr,1,&historyTask);
 xSemaphoreGive(initialization);
 if(!historyMutex||!historyTask){error="History worker memory unavailable";return false;}
 if(xSemaphoreTake(historyMutex,0)!=pdTRUE){response["pending"]=true;return true;}
 struct Unlock{~Unlock(){xSemaphoreGive(historyMutex);}} unlock;
 bool match=historyJob.path==path&&historyJob.offset==offset&&historyJob.from==from&&historyJob.to==to;
 if(historyJob.state==HistoryJob::Ready && match){
  bool success=historyJob.success;error=historyJob.error;response.set(historyJob.result.as<JsonVariantConst>());
  historyJob.result.clear();historyJob.state=HistoryJob::Idle;
  if(response.overflowed()){error="History response memory exhausted";return false;}
  return success;
 }
 if(historyJob.state==HistoryJob::Ready&&uint32_t(millis()-historyJob.completedAt)>3000){historyJob.result.clear();historyJob.state=HistoryJob::Idle;}
 if(historyJob.state==HistoryJob::Idle){
  historyJob.path=path;historyJob.offset=offset;historyJob.from=from;historyJob.to=to;historyJob.state=HistoryJob::Pending;
  xTaskNotifyGive(historyTask);
 }
 response["pending"]=true;return true;
}

void setPlotSleepGate(bool blocked){sleepGate=blocked;}
bool plotSleepReady(){return outstanding.load()==0;}
