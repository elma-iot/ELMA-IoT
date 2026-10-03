#include "logic_alarm.h"
#include <cassert>
#include <iostream>
using namespace ElmaLogic;
int64_t epoch(const char* d,const char* t){int64_t value;assert(alarmDateTime(d,t,value));return value;}
int main(){
 int64_t ignored;int second;
 assert(!alarmDate("2025-02-29",ignored));assert(alarmDate("2024-02-29",ignored));assert(!alarmDate("2100-01-01",ignored));assert(!alarmTime("24:00:00",second));assert(!alarmTime("07:00",second));
 assert(alarmClockText(epoch("2024-02-29","23:59:59"))=="2024-02-29 23:59:59Z");
 JsonDocument parameters;deserializeJson(parameters,R"({"clockSource":"utc","schedule":"weekdays","alarmTime":"07:00:00","weekdays":"1111100"})");
 auto p=parameters.as<JsonVariantConst>();assert(validAlarm(p));AlarmState state;
 auto monday=epoch("2026-10-05","07:00:00");
 assert(!alarmDue(p,monday-1,true,state));assert(alarmDue(p,monday,true,state));assert(!alarmDue(p,monday,true,state));assert(!alarmDue(p,monday+1,true,state));
 assert(!alarmDue(p,monday-10,true,state));assert(!alarmDue(p,monday,true,state)); // backwards must not double-fire
 state={};assert(!alarmDue(p,epoch("2026-10-03","07:00:00"),true,state)); // Saturday off
 state={};assert(!alarmDue(p,monday+30,true,state)); // boot after deadline: no replay
 state={};assert(!alarmDue(p,monday-3600,true,state));assert(!alarmDue(p,monday+30,true,state)); // forward correction
 state={};assert(!alarmDue(p,0,true,state));assert(alarmDue(p,monday,true,state)); // clock becomes valid at deadline
 state={};assert(!alarmDue(p,monday,false,state));assert(!alarmDue(p,monday+1,true,state));
 state={};assert(!alarmDue(p,monday-1,true,state));assert(alarmDue(p,monday+1,true,state)); // polling crosses second
 parameters["schedule"]="once";parameters["alarmDate"]="2026-10-05";assert(validAlarm(p));state={};assert(alarmDue(p,monday,true,state));assert(!alarmDue(p,monday+86400,true,state));
 parameters["alarmDate"]="2026-02-30";assert(!validAlarm(p));
 parameters["schedule"]="weekdays";parameters["alarmTime"]="23:59:59";state={};
 assert(!alarmDue(p,epoch("2026-10-02","23:59:58"),true,state));assert(alarmDue(p,epoch("2026-10-03","00:00:00"),true,state)); // Friday occurrence across midnight
 std::cout<<"PASS: alarm dates, weekdays, one-shot, corrections, invalid/disabled clocks and duplicate suppression\n";
}
