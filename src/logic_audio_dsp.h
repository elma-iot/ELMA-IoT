#pragma once
// Small offline English alert synthesizer. No network, voice database or external dependencies.
// Rule-based pronunciation is intentionally basic, not a neural/natural voice.
#include <cmath>
#include <cctype>
#include <cstring>

#include <cstdint>
namespace ElmaAudio {
class SpeechSynth {
 struct Filter {float a=0,b=0,g=0,y1=0,y2=0; void tune(float hz,float bw){float radius=std::exp(-3.14159265f*bw/16000);a=2*radius*std::cos(6.2831853f*hz/16000);b=-radius*radius;g=(1-radius)*.5f;y1=y2=0;}float tick(float x){float y=g*x+a*y1+b*y2;y2=y1;y1=y;return y;}};
 char phones[2048]={};size_t phoneCount=0,index=0;uint32_t frame=0,length=0;float phase=0;uint32_t noise=1;Filter filters[3];char current=' ';bool voiced=false;float amplitude=0,rate=0.85f,pitch=125.f,prosody=.65f;
 void push(char c){if(phoneCount<sizeof(phones))phones[phoneCount++]=c;}
 void append(const char* text){while(*text)push(*text++);}
 void pronounce(const char* word){
  struct Word {const char* text;const char* phones;};
  static const Word dictionary[]={{"chip","CIp"},{"is","Iz"},{"above","UbUv"},{"high","hI"},{"degrees","dIgrez"},{"hello","hElO"},{"fifty","fIfte"},{"sixty","sIkste"},{"seventy","sEvUnte"},{"eighty","Ete"},{"ninety","nInte"},{"ten","tEn"},{"eleven","IlEvUn"},{"twelve","twElv"},{"thirteen","TUrtEn"},{"fourteen","fOrtEn"},{"fifteen","fIftEn"},{"sixteen","sIkstEn"},{"seventeen","sEvUntEn"},{"eighteen","EtEn"},{"nineteen","nIntEn"},{"twenty","twEnte"},{"thirty","TUrte"},{"forty","fOrte"},{"battery","bAtEre"},{"low","lO"},{"voltage","vOltIj"},{"critical","krItIkUl"},{"temperature","tEmpUrUtSr"},{"warning","wOrnIN"},{"device","dIvIs"},{"enabled","EnAblUd"},{"disabled","dIsAblUd"},{"on","On"},{"off","Of"},{"fan","fAn"},{"water","wOtUr"},{"zero","zErO"},{"one","wUn"},{"two","tu"},{"three","Tre"},{"four","fOr"},{"five","fIv"},{"six","sIks"},{"seven","sEvUn"},{"eight","Et"},{"nine","nIn"},{"point","pOInt"}};
  for(const auto& entry:dictionary)if(std::strcmp(word,entry.text)==0){append(entry.phones);return;}
  size_t size=std::strlen(word);
  for(size_t i=0;i<size;++i){char c=word[i],n=i+1<size?word[i+1]:0;
   if((c=='s'&&n=='h')||(c=='c'&&n=='h')||(c=='t'&&n=='h')||(c=='n'&&n=='g')||(c=='p'&&n=='h')||(c=='e'&&(n=='e'||n=='a'))||(c=='o'&&n=='o')) {push(c=='s'?'S':c=='c'?'C':c=='t'?'T':c=='n'?'N':c=='p'?'f':c=='o'?'u':'e');++i;continue;}
   if(c=='e' && i+1==size && size>3)continue;
   switch(c){case 'a':push('A');break;case 'e':push('E');break;case 'i':push('I');break;case 'o':push('O');break;case 'u':push('U');break;case 'c':push(n && std::strchr("eiy",n)?'s':'k');break;case 'q':append("kw");break;case 'x':append("ks");break;case 'y':push('e');break;default:push(c);}
  }
 }
 void next(){current=phones[index++];frame=0;length=static_cast<uint32_t>((current==' '?1800:std::strchr("AEIOUeu",current)?1900:1100)/rate);voiced=std::strchr("AEIOUeurlmnbdgvwzjN",current)!=nullptr;amplitude=current==' '?0:voiced?1.f:.35f;
  float f1=400,f2=1500,f3=2500;
  switch(current){case 'A':f1=700;f2=1700;break;case 'E':f1=530;f2=1900;break;case 'I':f1=400;f2=2100;break;case 'e':f1=270;f2=2300;break;case 'O':f1=500;f2=900;break;case 'U':f1=600;f2=1200;break;case 'u':f1=300;f2=700;break;case 'r':f1=450;f2=1300;f3=1700;break;case 'l':f1=400;f2=1100;break;case 'm':case 'n':case 'N':f1=250;f2=1000;amplitude=.7;break;case 's':case 'z':f1=4500;f2=5800;f3=6800;break;case 'S':case 'C':case 'j':f1=2200;f2=3500;f3=4500;break;case 'f':case 'v':case 'T':f1=1500;f2=3500;f3=5500;break;default:break;}
  filters[0].tune(f1,100);filters[1].tune(f2,150);filters[2].tune(f3,220);
 }
 public:
 static bool validText(const char* text){if(!text || !*text || std::strlen(text)>256)return false;bool letters=false;for(const unsigned char* p=reinterpret_cast<const unsigned char*>(text);*p;++p){if(*p>127 || (*p<32 && !std::isspace(*p)))return false;if(std::isalnum(*p))letters=true;}return letters;}
 bool begin(const char* text,float speechRate=.85f,float voicePitch=125.f,float intonation=.65f){if(!validText(text)||!std::isfinite(speechRate)||speechRate<.5f||speechRate>1.5f||!std::isfinite(voicePitch)||voicePitch<80||voicePitch>220||!std::isfinite(intonation)||intonation<0||intonation>1)return false;rate=speechRate;pitch=voicePitch;prosody=intonation;phoneCount=0;char word[257];size_t length=0;
  static const char* digits[]={"zero","one","two","three","four","five","six","seven","eight","nine"};
  auto flush=[&](){if(length){word[length]=0;pronounce(word);push(' ');length=0;}};
  for(const unsigned char* p=reinterpret_cast<const unsigned char*>(text);*p;++p){if(std::isalpha(*p))word[length++]=static_cast<char>(std::tolower(*p));else {flush();if(std::isdigit(*p)){if(std::isdigit(p[1])&&!std::isdigit(p[2])&&(p==reinterpret_cast<const unsigned char*>(text)||!std::isdigit(p[-1])) && p[2]!='.'){
      unsigned value=(*p-'0')*10+(p[1]-'0');
      static const char* teens[]={"ten","eleven","twelve","thirteen","fourteen","fifteen","sixteen","seventeen","eighteen","nineteen"};
      static const char* tens[]={"","","twenty","thirty","forty","fifty","sixty","seventy","eighty","ninety"};
      if(value<10)pronounce(digits[value]);else if(value<20)pronounce(teens[value-10]);else{pronounce(tens[value/10]);if(value%10){push(' ');pronounce(digits[value%10]);}}++p;
     }else pronounce(digits[*p-'0']);push(' ');}else if(*p=='.' && p>reinterpret_cast<const unsigned char*>(text) && std::isdigit(p[-1]) && std::isdigit(p[1])){pronounce("point");push(' ');}}}flush();index=0;frame=this->length=0;phase=0;noise=1;return phoneCount>0;}
 size_t render(int16_t* output,size_t capacity){size_t used=0;while(used<capacity){if(frame>=length){if(index>=phoneCount)break;next();}noise=noise*1664525u+1013904223u;float random=(static_cast<int32_t>(noise)>>16)/32768.f;float contour=1.f+prosody*(.08f*std::sin(index*.85f)-.06f*frame/std::fmax(1.f,static_cast<float>(length)));phase+=pitch*contour/16000;if(phase>=1)phase-=1;float excitation=voiced?(phase<.12f?1.f-phase/.12f:-.07f):random;
   if(std::strchr("ptkbdgC",current) && frame<length*.55f)excitation=0;
   float signal=filters[0].tick(excitation)*1.3f+filters[1].tick(excitation)*.6f+filters[2].tick(excitation)*.3f;float envelope=std::fmin(1.f,frame/100.f)*std::fmin(1.f,(length-frame)/150.f);signal*=amplitude*envelope*18000;output[used++]=static_cast<int16_t>(std::fmax(-30000.f,std::fmin(30000.f,signal)));++frame;}return used;}
 uint32_t maximumFrames() const{return static_cast<uint32_t>(phoneCount*1900.f/rate+phoneCount);}
};
// Dual cross-faded delay taps produce an inexpensive independent pitch shift.
// Speed is set on the I2S clock; the shift compensates that clock's pitch change.
// Small polyphonic oscillator bank: recorded MIDI events, not sampled instrument assets.
struct PianoNote {uint8_t note=60;float start=0,duration=.25f,velocity=.75f;};
class PianoSynth {
 PianoNote notes[128];uint32_t starts[128]={},ends[128]={};float phases[128]={},steps[128]={};size_t count=0;uint32_t frame=0,total=0;unsigned instrument=0;float wave[256]={};
 float oscillator(float phase)const{return wave[static_cast<unsigned>(phase*256)&255];}
 public:
 static bool validInstrument(const char* name){return name && (!std::strcmp(name,"piano")||!std::strcmp(name,"bell")||!std::strcmp(name,"guitar")||!std::strcmp(name,"organ"));}
 bool begin(const char* name,const PianoNote* events,size_t length){
  if(!validInstrument(name)||length>128)return false;
  count=length;frame=total=0;instrument=!std::strcmp(name,"bell")?1:!std::strcmp(name,"guitar")?2:!std::strcmp(name,"organ")?3:0;
  for(unsigned i=0;i<256;++i)wave[i]=std::sin(i*6.283185307f/256);
  for(size_t i=0;i<count;++i){const auto& n=events[i];if(n.note<12||n.note>108||!std::isfinite(n.start)||!std::isfinite(n.duration)||!std::isfinite(n.velocity)||n.start<0||n.duration<=0||n.start+n.duration>60||n.velocity<=0||n.velocity>1)return false;notes[i]=n;starts[i]=n.start*16000;ends[i]=(n.start+n.duration+.08f)*16000;total=ends[i]>total?ends[i]:total;phases[i]=0;steps[i]=440*std::pow(2.f,(static_cast<int>(n.note)-69)/12.f)/16000;}
  total+=640;return true;
 }
 uint32_t maximumFrames()const{return total;}
 size_t render(int16_t* output,size_t capacity){size_t size=0;while(size<capacity&&frame<total){float sample=0;
  for(size_t i=0;i<count;++i){if(frame<starts[i]||frame>=ends[i])continue;float t=(frame-starts[i])/16000.f;float attack=t<.008f?t/.008f:1;float release=(ends[i]-frame)/1280.f;if(release>1)release=1;float decay=1/(1+t*(instrument==0?4:instrument==1?3:instrument==2?6:.1f));float phase=phases[i],tone=oscillator(phase);if(instrument==1)tone=.7f*tone+.3f*oscillator(phase*2.76f);else if(instrument==2)tone=.8f*tone+.2f*oscillator(phase*2);else if(instrument==3)tone=.65f*tone+.25f*oscillator(phase*2)+.1f*oscillator(phase*3);sample+=tone*attack*release*decay*notes[i].velocity*.22f;phases[i]+=steps[i];phases[i]-=std::floor(phases[i]);}
  if(sample>1)sample=1;if(sample<-1)sample=-1;output[size++]=static_cast<int16_t>(sample*32767);++frame;
 }return size;}
};

class PitchShift {
 static constexpr unsigned Size=2048,Mask=Size-1;int16_t left[Size]={},right[Size]={};unsigned write=0;float phase=0,ratio=1;
 float read(const int16_t* channel,float delay)const{float at=static_cast<float>(write)-delay;while(at<0)at+=Size;unsigned first=static_cast<unsigned>(at)&Mask;float frac=at-std::floor(at);return channel[first]*(1-frac)+channel[(first+1)&Mask]*frac;}
 public:
 void reset(float factor){ratio=factor;phase=0;write=0;std::memset(left,0,sizeof(left));std::memset(right,0,sizeof(right));}
 uint32_t process(uint32_t sample){if(std::fabs(ratio-1)<.0001f)return sample;left[write]=static_cast<int16_t>(sample>>16);right[write]=static_cast<int16_t>(sample);phase+=(1-ratio)/1024;phase-=std::floor(phase);float other=phase+.5f;other-=std::floor(other);float weight=1-std::fabs(phase*2-1);float d1=32+phase*1024,d2=32+other*1024;int16_t l=static_cast<int16_t>(read(left,d1)*weight+read(left,d2)*(1-weight)),r=static_cast<int16_t>(read(right,d1)*weight+read(right,d2)*(1-weight));write=(write+1)&Mask;return static_cast<uint32_t>(static_cast<uint16_t>(l))<<16|static_cast<uint16_t>(r);}
};
}
