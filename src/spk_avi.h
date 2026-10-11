#pragma once
#include <FS.h>
// MJPEG + uncompressed mono PCM. No transcoding and no in-memory index.
class SpkAvi {
 File file_;uint32_t frames_=0,audio_=0,started_=0,audioRate_=16000;uint16_t width_=0,height_=0;uint32_t movi_=0,frameCountOffset_=0,videoLength_=0,audioLength_=0,rateOffset_=0;
 void u32(uint32_t v){uint8_t b[4]={uint8_t(v),uint8_t(v>>8),uint8_t(v>>16),uint8_t(v>>24)};file_.write(b,4);}
 void u16(uint16_t v){uint8_t b[2]={uint8_t(v),uint8_t(v>>8)};file_.write(b,2);}
 void tag(const char* s){file_.write((const uint8_t*)s,4);}
 void zeros(int n){while(n--)file_.write(uint8_t(0));}
 void patch(uint32_t offset,uint32_t value){uint32_t position=file_.position();file_.seek(offset);u32(value);file_.seek(position);}
 void list(const char* type){tag("LIST");u32(0);tag(type);}
 public:
 bool begin(File file,uint16_t width,uint16_t height,uint32_t rate=16000){audioRate_=rate;file_=file;if(!file_)return false;width_=width;height_=height;started_=millis();frames_=audio_=0;
  tag("RIFF");u32(0);tag("AVI ");list("hdrl");
  tag("avih");u32(56);u32(100000);u32(0);u32(0);u32(0);frameCountOffset_=file_.position();u32(0);u32(0);u32(2);u32(width*height);u32(width);u32(height);zeros(16);
  uint32_t videoList=file_.position();list("strl");tag("strh");u32(56);tag("vids");tag("MJPG");u32(0);u16(0);u16(0);u32(0);u32(1000);rateOffset_=file_.position();u32(10000);u32(0);videoLength_=file_.position();u32(0);u32(width*height);u32(0xffffffff);u32(0);u16(0);u16(0);u16(width);u16(height);
  tag("strf");u32(40);u32(40);u32(width);u32(height);u16(1);u16(24);tag("MJPG");u32(width*height*3);zeros(16);patch(videoList+4,file_.position()-videoList-8);
  uint32_t audioList=file_.position();list("strl");tag("strh");u32(56);tag("auds");u32(0);u32(0);u16(0);u16(0);u32(0);u32(2);u32(audioRate_*2);u32(0);audioLength_=file_.position();u32(0);u32(6400);u32(0xffffffff);u32(2);zeros(8);
  tag("strf");u32(16);u16(1);u16(1);u32(audioRate_);u32(audioRate_*2);u16(2);u16(16);patch(audioList+4,file_.position()-audioList-8);patch(16,file_.position()-20);
  movi_=file_.position();list("movi");return true;
 }
 bool chunk(const char* id,const uint8_t* data,size_t n){tag(id);u32(n);if(file_.write(data,n)!=n)return false;if(n&1)file_.write(uint8_t(0));return true;}
 bool video(const uint8_t* data,size_t n){if(!chunk("00dc",data,n))return false;frames_++;return true;}
 bool audio(const uint8_t* data,size_t n){if(!chunk("01wb",data,n))return false;audio_+=n;return true;}
 bool active()const{return bool(file_);}
 void end(){if(!file_)return;patch(movi_+4,file_.size()-movi_-8);patch(4,file_.size()-8);patch(frameCountOffset_,frames_);patch(videoLength_,frames_);patch(audioLength_,audio_/2);uint32_t duration=std::max(uint32_t(1),audio_?uint32_t(uint64_t(audio_)*1000/(audioRate_*2)):uint32_t(millis()-started_));patch(rateOffset_,uint32_t(uint64_t(frames_)*1000000/duration));patch(32,frames_?duration*1000/frames_:100000);file_.flush();file_.close();}
};
