#include "plot_telemetry.h"
#include <esp_system.h>
#include "storage_memory.h"
#include <sys/time.h>
namespace {
struct Sample {char plot[33],series[33],unit[17];uint32_t time,sequence;double value,epoch;};
constexpr size_t Capacity=64;
Sample samples[Capacity];uint32_t sequence=0;size_t count=0;
portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
uint32_t bootId(){static uint32_t id=esp_random();return id;}
}
void publishPlotSample(const char* plot,const char* series,const char* unit,uint32_t time,double value){
 Sample sample{};snprintf(sample.plot,sizeof(sample.plot),"%s",plot);snprintf(sample.series,sizeof(sample.series),"%s",series);snprintf(sample.unit,sizeof(sample.unit),"%s",unit);sample.time=time;sample.value=value;timeval wall;gettimeofday(&wall,nullptr);sample.epoch=wall.tv_sec>1577836800?double(wall.tv_sec)*1000+wall.tv_usec/1000:0;
 portENTER_CRITICAL(&mux);sample.sequence=++sequence;samples[(sequence-1)%Capacity]=sample;if(count<Capacity)++count;portEXIT_CRITICAL(&mux);
}
void plotSamplesSince(uint32_t after,uint32_t boot,JsonDocument& response){
 Sample* copy=static_cast<Sample*>(allocateStorageBuffer(sizeof(Sample)*Capacity));
 if(!copy){response["error"]="Plot snapshot memory reserve reached";return;}
 struct Release{Sample* p;~Release(){heap_caps_free(p);}} release{copy};
 uint32_t latest;size_t size;
 portENTER_CRITICAL(&mux);latest=sequence;size=count;for(size_t i=0;i<size;++i)copy[i]=samples[(latest-size+i)%Capacity];portEXIT_CRITICAL(&mux);
 if(boot!=bootId()||after>latest)after=0;
 response["boot"]=bootId();response["cursor"]=latest;response["dropped"]=after&&latest-after>size?latest-after-size:0;
 JsonArray array=response["samples"].to<JsonArray>();
 for(size_t i=0;i<size;++i){const auto& s=copy[i];if(s.sequence<=after)continue;JsonObject item=array.add<JsonObject>();item["plot"]=s.plot;item["series"]=s.series;item["unit"]=s.unit;item["t"]=s.time;item["value"]=s.value;item["epoch"]=s.epoch;}
}
