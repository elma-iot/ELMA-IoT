"""Execute the patched library's actual frame pump with injected DMA failures."""
from pathlib import Path
import subprocess,tempfile,unittest
from test_json_allocator import VCVARS
ROOT=Path(__file__).resolve().parents[1]

class AudioDmaRetryTests(unittest.TestCase):
    @unittest.skipUnless(VCVARS,'MSVC compiler unavailable')
    def test_failed_frame_resumes_without_drop_repeat_or_second_filter(self):
        sources=list((ROOT/'.pio/libdeps/sunton_3248s035c').glob('ESP32-audioI2S*/src/Audio.cpp'))
        self.assertTrue(sources,'Run the pinned dependency patches first')
        source=sources[0].read_text()
        def function(signature):
            start=source.index(signature);a=source.index('{',start);depth=1;b=a+1
            while depth:
                depth+=(source[b]=='{')-(source[b]=='}');b+=1
            return source[start:b]
        cpp=r'''
#include <cstdint>
#include <cstddef>
#include <cassert>
#include <vector>
#define APP_SUNTON_PANEL 3
#define LEFTCHANNEL 0
#define RIGHTCHANNEL 1
#define log_e(...) ((void)0)
using esp_err_t=int;using i2s_port_t=int;
constexpr int ESP_OK=0;
std::vector<uint32_t> output;
int calls=0,failAt=0,shortAt=0;
int elmaWriteAudioSample(int,uint32_t pcm,bool,size_t* n,int){++calls;if(calls==failAt){*n=0;return -1;}if(calls==shortAt){*n=0;return 0;}*n=4;output.push_back(pcm);return 0;}
size_t elmaAudioFrameBytes(bool){return 4;}
void (*audio_process_i2s)(uint32_t*,bool*)=nullptr;
struct Audio{
 int bits=16,channels=2,filters=0,gains=0;bool m_f_forceMono=false,m_f_internalDAC=true;
 int m_validSamples=0,m_curSample=0,m_i2s_num=0;size_t m_i2s_bytesWritten=0;
 bool m_elmaPcm8HalfPending=false,m_elmaI2sPending=false;uint32_t m_elmaPendingPcm=0;
 int16_t m_outBuff[16]{};
 int getBitsPerSample(){return bits;}int getChannels(){return channels;}
 int16_t* IIR_filterChain0(int16_t* s){++filters;return s;}
 int16_t* IIR_filterChain1(int16_t* s){return s;}
 int16_t* IIR_filterChain2(int16_t* s){return s;}
 uint32_t Gain(int16_t* s){++gains;return (uint32_t(uint16_t(s[0]))<<16)|uint16_t(s[1]);}
 void stopSong(){}
 bool playChunk();bool playSample(int16_t*);
};
'''+function('bool Audio::playChunk()')+'\n'+function('bool Audio::playSample(int16_t sample[2])')+r'''
int main(){
 for(int bits:{8,16})for(int channels:{1,2})for(int failure=1;failure<=4;++failure)for(bool shortWrite:{false,true}){
   Audio a;a.bits=bits;a.channels=channels;a.m_validSamples=4;
   for(int i=0;i<8;++i)a.m_outBuff[i]=int16_t(0x9181+i*0x101);
   calls=failAt=shortAt=0;output.clear();assert(a.playChunk());const auto reference=output;
   a=Audio{};a.bits=bits;a.channels=channels;a.m_validSamples=4;
   for(int i=0;i<8;++i)a.m_outBuff[i]=int16_t(0x9181+i*0x101);
   output.clear();calls=0;if(shortWrite)shortAt=failure;else failAt=failure;
   assert(!a.playChunk());assert(a.playChunk());assert(a.m_validSamples==0&&a.m_curSample==0);
   assert(output==reference);assert(a.filters==int(reference.size()));assert(a.gains==int(reference.size()));
 }
}
'''
        with tempfile.TemporaryDirectory() as folder:
            work=Path(folder);(work/'main.cpp').write_text(cpp)
            (work/'run.cmd').write_text(f'@echo off\ncall "{VCVARS}" >nul && cl /nologo /EHsc /std:c++17 main.cpp /Fe:test.exe && test.exe\n')
            r=subprocess.run(['cmd','/d','/c','run.cmd'],cwd=work,capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)

if __name__=='__main__':unittest.main()
