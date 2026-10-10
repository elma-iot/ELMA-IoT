#pragma once
#include <ArduinoJson.h>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cstdlib>
namespace CanContract {
inline int pin(JsonVariantConst value){
 if(value.is<int>()&&!value.is<bool>())return value.as<int>();
 if(value.is<const char*>()){const char* text=value;if(!*text)return -1;for(const char* p=text;*p;p++)if(!isdigit((unsigned char)*p))return -1;char* end=nullptr;long number=strtol(text,&end,10);return number>=0&&number<=255?int(number):-1;}
 return -1;
}
inline bool integer(JsonVariantConst v,int64_t lo,int64_t hi){if(v.is<bool>()||!v.is<double>())return false;double d=v.as<double>();return std::isfinite(d)&&d>=lo&&d<=hi&&std::floor(d)==d;}
inline bool bytes(JsonVariantConst value,uint8_t* data,uint8_t& length){
 length=0;if(!value.is<const char*>())return false;const char* s=value;
 auto hex=[](char c)->int{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;};
 while(*s){while(*s&&isspace((unsigned char)*s))s++;if(!*s)break;
  if(length>=8||hex(s[0])<0||!s[1]||hex(s[1])<0||(s[2]&&!isspace((unsigned char)s[2])))return false;
  data[length++]=uint8_t(hex(s[0])*16+hex(s[1]));s+=2;
 }return true;
}
inline bool valid(const char* type,JsonVariantConst p){
 if(!strcmp(type,"hardware.can.configure"))return (p["bitrate"]==125000||p["bitrate"]==250000||p["bitrate"]==500000||p["bitrate"]==1000000)&&p["listenOnly"].is<bool>();
 if(!strcmp(type,"hardware.can.send")){uint8_t data[8],length=0;return p["extended"].is<bool>()&&p["remote"].is<bool>()&&integer(p["identifier"],0,p["extended"]==true?0x1fffffff:0x7ff)&&integer(p["length"],0,8)&&bytes(p["data"],data,length)&&!(p["remote"]==true&&length);}
 if(!strcmp(type,"hardware.can.received"))return integer(p["identifierFilter"],-1,0x1fffffff);
 return !strcmp(type,"hardware.can.status");
}
}
