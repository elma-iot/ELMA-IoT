#include "../src/logic_audio_dsp.h"
#include <cassert>
#include <vector>
#include <string>
#include <cstdio>
#include <algorithm>
int main(){
 {ElmaAudio::SpeechSynth slow,fast;assert(slow.begin("Chip temperature is above 60 degrees",.5f,100,1));assert(fast.begin("Chip temperature is above 60 degrees",1.5f,180,0));assert(slow.maximumFrames()>fast.maximumFrames());int16_t pcm[512];size_t slowCount=0,fastCount=0,n;while((n=slow.render(pcm,512)))slowCount+=n;while((n=fast.render(pcm,512)))fastCount+=n;assert(slowCount>fastCount);assert(!fast.begin("Hello",0,125,.5));assert(!fast.begin("Hello",1,250,.5));assert(!fast.begin("Hello",1,125,2));}

 // Three simultaneous notes must all survive synthesis; waveform instruments stay compact.
 ElmaAudio::PianoNote chord[3];for(unsigned i=0;i<3;++i){chord[i].note=i==0?60:i==1?64:67;chord[i].start=0;chord[i].duration=.5f;chord[i].velocity=.8f;}
 std::vector<int16_t> previous;for(const char* instrument:{"piano","bell","guitar","organ"}){ElmaAudio::PianoSynth piano;assert(piano.begin(instrument,chord,3));std::vector<int16_t> pcm;int16_t block[512];size_t n;while((n=piano.render(block,512)))pcm.insert(pcm.end(),block,block+n);assert(pcm.size()==piano.maximumFrames());double energy=0;for(auto sample:pcm)energy+=double(sample)*sample;assert(energy/pcm.size()>1000);if(!std::strcmp(instrument,"piano")){for(double hz:{261.625565,329.627557,391.995436}){double real=0,imag=0;for(unsigned at=1000;at<7000;++at){double phase=at*6.283185307*hz/16000;real+=pcm[at]*std::cos(phase);imag+=pcm[at]*std::sin(phase);}assert(real*real+imag*imag>1e11);}}if(!previous.empty())assert(previous!=pcm);previous=pcm;}
 ElmaAudio::PianoSynth invalid;assert(!invalid.begin("unknown",chord,3));assert(!invalid.begin("piano",chord,129));chord[0].duration=-1;assert(!invalid.begin("piano",chord,3));
 ElmaAudio::SpeechSynth synth;assert(synth.begin("Battery voltage low. 11.8"));std::vector<int16_t> frames;int16_t chunk[512];size_t count;while((count=synth.render(chunk,512)))frames.insert(frames.end(),chunk,chunk+count);assert(frames.size()>16000&&frames.size()<=synth.maximumFrames());double energy=0;for(auto sample:frames)energy+=double(sample)*sample;assert(energy/frames.size()>1000);assert(!synth.begin(""));assert(!synth.begin("\xc3\xa9"));assert(!synth.begin(std::string(257,'a').c_str()));
 // A 220 Hz stereo tone must change pitch while retaining exactly the frame count.
 for(float ratio:{.5f,1.f,2.f}){ElmaAudio::PitchShift processor;processor.reset(ratio);std::vector<double> samples;for(unsigned n=0;n<32000;++n){int16_t sample=static_cast<int16_t>(std::sin(n*6.283185307*220/16000)*10000);uint32_t stereo=static_cast<uint32_t>(static_cast<uint16_t>(sample))<<16|static_cast<uint16_t>(sample);uint32_t output=processor.process(stereo);assert(static_cast<int16_t>(output>>16)==static_cast<int16_t>(output));if(n>=16000)samples.push_back(static_cast<int16_t>(output));if(ratio==1)assert(output==stereo);}
  double bestPower=0;unsigned best=0;for(unsigned hz=80;hz<520;++hz){double real=0,imag=0;for(unsigned n=0;n<samples.size();++n){double phase=n*6.283185307*hz/16000;real+=samples[n]*std::cos(phase);imag+=samples[n]*std::sin(phase);}double power=real*real+imag*imag;if(power>bestPower){bestPower=power;best=hz;}}assert(std::abs(static_cast<int>(best)-static_cast<int>(220*ratio))<20);std::printf("pitch ratio %.2f: %u Hz\n",ratio,best);
 }
 std::printf("speech: %zu frames; RMS %.2f; bounded synthesis and pitch verified\n",frames.size(),std::sqrt(energy/frames.size()));
}
