#include "plot_telemetry.h"
#include <esp_system.h>
#include "storage_memory.h"
#include <sys/time.h>
namespace {
struct Sample {char plot[33],series[33],unit[17];uint32_t time,sequence;double value,epoch;};
constexpr size_t Capacity=64;
Sample* samples=nullptr;uint32_t sequence=0;size_t count=0;
portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
uint32_t bootId(){static uint32_t id=esp_random();return id;}
}
void publishPlotSample(const char* plot,const char* series,const char* unit,uint32_t time,double value){
 // Allocate outside the critical section; racing publishers discard the extra buffer.
 portENTER_CRITICAL(&mux);bool needsBuffer=samples==nullptr;portEXIT_CRITICAL(&mux);
 if(needsBuffer){
  Sample* candidate=static_cast<Sample*>(allocateStorageBuffer(sizeof(Sample)*Capacity));
  if(!candidate)return;
  portENTER_CRITICAL(&mux);
  if(!samples){samples=candidate;candidate=nullptr;}
  portEXIT_CRITICAL(&mux);
  if(candidate)heap_caps_free(candidate);
 }
 Sample sample{};snprintf(sample.plot,sizeof(sample.plot),"%s",plot);snprintf(sample.series,sizeof(sample.series),"%s",series);snprintf(sample.unit,sizeof(sample.unit),"%s",unit);sample.time=time;sample.value=value;timeval wall;gettimeofday(&wall,nullptr);sample.epoch=wall.tv_sec>1577836800?double(wall.tv_sec)*1000+wall.tv_usec/1000:0;
 portENTER_CRITICAL(&mux);sample.sequence=++sequence;samples[(sequence-1)%Capacity]=sample;if(count<Capacity)++count;portEXIT_CRITICAL(&mux);
}
void plotSamplesSince(uint32_t after,uint32_t boot,JsonDocument& response){
 portENTER_CRITICAL(&mux);const size_t snapshotCapacity=count;const uint32_t snapshotSequence=sequence;portEXIT_CRITICAL(&mux);
 Sample* copy=snapshotCapacity?static_cast<Sample*>(allocateStorageBuffer(sizeof(Sample)*snapshotCapacity)):nullptr;
 if(snapshotCapacity && !copy){response["error"]="Plot snapshot memory reserve reached";return;}
 struct Release{Sample* p;~Release(){heap_caps_free(p);}} release{copy};
 uint32_t latest;size_t size;
 portENTER_CRITICAL(&mux);latest=snapshotCapacity?sequence:snapshotSequence;size=count<snapshotCapacity?count:snapshotCapacity;for(size_t i=0;i<size;++i)copy[i]=samples[(latest-size+i)%Capacity];portEXIT_CRITICAL(&mux);
 if(boot!=bootId()||after>latest)after=0;
 response["boot"]=bootId();response["cursor"]=latest;response["dropped"]=after&&latest-after>size?latest-after-size:0;
 JsonArray array=response["samples"].to<JsonArray>();
 for(size_t i=0;i<size;++i){const auto& s=copy[i];if(s.sequence<=after)continue;JsonObject item=array.add<JsonObject>();item["plot"]=s.plot;item["series"]=s.series;item["unit"]=s.unit;item["t"]=s.time;item["value"]=s.value;item["epoch"]=s.epoch;}
}
void plotSerialNext(uint32_t after,uint32_t boot,JsonDocument& response){
 Sample selected{};bool found=false;uint32_t latest;size_t size;const uint32_t currentBoot=bootId();
 portENTER_CRITICAL(&mux);
 latest=sequence;size=count;
 if(boot!=currentBoot||after>latest)after=0;
 const uint32_t first=latest-size+1;
 const uint32_t wanted=after+1<first?first:after+1;
 if(size && wanted<=latest){selected=samples[(wanted-1)%Capacity];found=true;}
 portEXIT_CRITICAL(&mux);
 response["boot"]=currentBoot;response["latest"]=latest;
 response["cursor"]=found?selected.sequence:latest;
 response["dropped"]=after && size && after+1<first ? first-after-1 : 0;
 if(found){auto item=response["sample"].to<JsonObject>();item["plot"]=selected.plot;item["series"]=selected.series;item["unit"]=selected.unit;item["t"]=selected.time;item["value"]=selected.value;item["epoch"]=selected.epoch;}
}
