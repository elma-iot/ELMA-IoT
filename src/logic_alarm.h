#pragma once
#include <ArduinoJson.h>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>

namespace ElmaLogic {
inline bool alarmTime(const char* text,int& seconds) {
    if(!text||strlen(text)!=8||text[2]!=':'||text[5]!=':')return false;
    for(int i:{0,1,3,4,6,7})if(text[i]<'0'||text[i]>'9')return false;
    int h=(text[0]-'0')*10+text[1]-'0',m=(text[3]-'0')*10+text[4]-'0',s=(text[6]-'0')*10+text[7]-'0';
    seconds=h*3600+m*60+s;return h<24&&m<60&&s<60;
}
inline bool alarmDate(const char* text,int64_t& day) {
    if(!text||strlen(text)!=10||text[4]!='-'||text[7]!='-')return false;
    for(int i:{0,1,2,3,5,6,8,9})if(text[i]<'0'||text[i]>'9')return false;
    int y=0;for(int i=0;i<4;++i)y=y*10+text[i]-'0';
    int m=(text[5]-'0')*10+text[6]-'0',d=(text[8]-'0')*10+text[9]-'0';
    if(y<2000||y>2099||m<1||m>12)return false;
    const int lengths[]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(d<1||d>lengths[m-1]+(m==2&&y%4==0))return false;
    day=10957;for(int year=2000;year<y;++year)day+=365+(year%4==0);
    for(int month=1;month<m;++month)day+=lengths[month-1]+(month==2&&y%4==0);
    day+=d-1;return true;
}
inline bool alarmDateTime(const char* date,const char* time,int64_t& epoch) {
    int64_t day;int second;if(!alarmDate(date,day)||!alarmTime(time,second))return false;
    epoch=day*86400+second;return true;
}
inline bool validAlarm(JsonVariantConst p) {
    const char* source=p["clockSource"]|"";const char* mode=p["schedule"]|"";int second;int64_t day;
    if(strcmp(source,"utc")&&strcmp(source,"manual")&&strcmp(source,"rtc"))return false;
    if(!alarmTime(p["alarmTime"]|"",second))return false;
    if(!strcmp(mode,"once"))return alarmDate(p["alarmDate"]|"",day);
    if(strcmp(mode,"weekdays"))return false;
    const char* days=p["weekdays"]|"";if(strlen(days)!=7)return false;
    bool selected=false;for(int i=0;i<7;++i){if(days[i]!='0'&&days[i]!='1')return false;selected|=days[i]=='1';}return selected;
}
struct AlarmState {int64_t previous=0,fired=0;};
inline std::string alarmClockText(int64_t epoch) {
    if(epoch<946684800LL||epoch>=4102444800LL)return {};
    int day=int(epoch/86400)-10957,year=2000,month=1;
    while(day>=365+(year%4==0)){day-=365+(year%4==0);++year;}
    const int lengths[]={31,28,31,30,31,30,31,31,30,31,30,31};
    while(day>=lengths[month-1]+(month==2&&year%4==0)){day-=lengths[month-1]+(month==2&&year%4==0);++month;}
    int second=int(epoch%86400);char text[25];snprintf(text,sizeof(text),"%04d-%02d-%02d %02d:%02d:%02dZ",year,month,day+1,second/3600,(second/60)%60,second%60);return text;
}
inline bool alarmDue(JsonVariantConst p,int64_t now,bool enabled,AlarmState& state) {
    if(now<946684800LL||now>=4102444800LL||!enabled){state.previous=0;return false;}
    int seconds;if(!alarmTime(p["alarmTime"]|"",seconds))return false;
    int64_t day=now/86400;
    if(now%86400<seconds && state.previous && now>=state.previous && now-state.previous<=60)--day;
    int64_t due=day*86400+seconds;
    if(p["schedule"]=="once") {int64_t date;if(!alarmDate(p["alarmDate"]|"",date))return false;due=date*86400+seconds;}
    else {const char* days=p["weekdays"]|"";if(strlen(days)!=7||days[(day+3)%7]!='1'){state.previous=now;return false;}}
    // No backlog replay on boot, re-enable, loss of time or large clock corrections.
    const int64_t previous=state.previous?state.previous:now-1;state.previous=now;
    if(now<previous||now-previous>60||due<=state.fired||due<=previous||due>now)return false;
    state.fired=due;return true;
}
}
