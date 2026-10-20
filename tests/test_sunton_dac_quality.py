"""Check fractional native-rate DAC conversion without claiming speaker THD."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from test_json_allocator import VCVARS
ROOT=Path(__file__).resolve().parents[1]
class DacQualityTests(unittest.TestCase):
    @unittest.skipUnless(VCVARS,'MSVC compiler unavailable')
    def test_native_fractional_mean_bounds_and_silence(self):
        with tempfile.TemporaryDirectory() as directory:
            work=Path(directory)
            shutil.copyfile(ROOT/'include/sunton_dac_quality.h',work/'sunton_dac_quality.h')
            (work/'test.cpp').write_text(r'''
#include "sunton_dac_quality.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>
int main(){
 SuntonDacQuality q;
 assert(elmaDacMode()==SuntonDacMode::Raw);
 assert(elmaSaturateAudioPcm(40000,true)==32767);
 assert(elmaSaturateAudioPcm(-40000,true)==-32768);
 assert(elmaSaturateAudioPcm(NAN,true)==0);
 assert(elmaSaturateAudioPcm(INFINITY,true)==0);
 assert(elmaDacCounters().filterClips==2&&elmaDacCounters().nonFinite==2);
 for(int mode=0;mode<=3;++mode){
  for(int v=-32768;v<=32767;v+=127){uint32_t pcm=(uint32_t(uint16_t(v))<<16)|uint16_t(-v);
   uint32_t out=q.convert(pcm,SuntonDacMode(mode));
   if(mode==0)assert(out==(pcm^0x80008000U));else assert((out&0x00ff00ffU)==0);
  }
  assert(q.convert(0,SuntonDacMode(mode))==0x80008000U);
 }
 int high=0;for(int i=0;i<4096;++i){auto w=q.convert(0x00800080U,SuntonDacMode::Tpdf);int code=int(w>>24);assert(code==128||code==129);high+=code==129;}
 assert(high>1800&&high<2300);

 for(int value: {-1000,-79,17,47,1000}){
  q.reset();long long sum=0;const int n=4096;
  for(int i=0;i<n;++i){uint32_t word=q.convert((uint32_t(uint16_t(value))<<16)|uint16_t(-value));
   assert((word&0x00ff00ffU)==0);
   sum+=int(word>>24)-128;
   assert(q.error[0]>=-128&&q.error[0]<=128);
  }
  assert(std::abs(double(sum)/n-double(value)/256)<1.0/n);
 }
 for(int value=-32768;value<=32767;value+=127){
  uint32_t word=q.convert((uint32_t(uint16_t(value))<<16)|uint16_t(value));
  assert((word&0x00ff00ffU)==0);
 }
 assert(q.convert(0)==0x80008000U);
 assert(q.error[0]==0&&q.error[1]==0);
 // DAC midpoint stays exact during silence; there is no random dither source.
 for(int i=0;i<1000;++i)assert(q.convert(0)==0x80008000U);
 puts("PASS native-frame DAC fractional means, bounds and exact silence");
}
''')
            (work/'run.cmd').write_text(f'@echo off\ncall "{VCVARS}" >nul && cl /nologo /EHsc /std:c++17 /DAPP_SUNTON_PANEL=3 test.cpp /Fe:test.exe && test.exe\n')
            result=subprocess.run(['cmd','/d','/c','run.cmd'],cwd=work,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
