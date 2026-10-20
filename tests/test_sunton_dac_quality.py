"""Measure actual DAC quantizer harmonics on a quiet coherent sine wave."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from test_json_allocator import VCVARS
ROOT=Path(__file__).resolve().parents[1]
class DacQualityTests(unittest.TestCase):
    @unittest.skipUnless(VCVARS,'MSVC compiler unavailable')
    def test_quiet_tone_harmonics_and_silence(self):
        with tempfile.TemporaryDirectory() as directory:
            work=Path(directory)
            shutil.copyfile(ROOT/'include/sunton_dac_quality.h',work/'sunton_dac_quality.h')
            (work/'test.cpp').write_text(r'''
#include "sunton_dac_quality.h"
#include <cmath>
#include <vector>
#include <cassert>
#include <cstdio>
double thd(const std::vector<double>& samples,double rate){
 double fundamental=0,harmonics=0;
 for(int h=1;h<=10;++h){double re=0,im=0;
  for(size_t i=0;i<samples.size();++i){double phase=6.283185307179586*1000*h*i/rate;re+=samples[i]*cos(phase);im+=samples[i]*sin(phase);}
  double power=re*re+im*im;if(h==1)fundamental=power;else harmonics+=power;
 }
 return sqrt(harmonics/fundamental);
}
int main(){
 SuntonDacQuality quality;std::vector<double> raw,improved;
 for(int i=0;i<4410;++i){int16_t value=int16_t(512*sin(6.283185307179586*1000*i/44100));
  raw.push_back(value>>8);uint32_t output[2];quality.expand((uint32_t(uint16_t(value))<<16)|uint16_t(value),output);
  for(auto word:output){assert((word&0x00ff00ffU)==0);improved.push_back(int(word>>24)-128);}
 }
 double before=thd(raw,44100),after=thd(improved,88200);
 printf("Quiet tone model THD: raw %.2f%%, shaped 2x %.2f%%\n",100*before,100*after);
 assert(after<before/4);
 uint32_t output[2];quality.expand(0,output);quality.expand(0,output);
 assert(output[0]==0x80008000U&&output[1]==0x80008000U);
 for(int value=-32768;value<=32767;value+=127){quality.expand((uint32_t(uint16_t(value))<<16)|uint16_t(value),output);for(auto word:output)assert((word&0x00ff00ffU)==0);}
}
''')
            (work/'run.cmd').write_text(f'@echo off\ncall "{VCVARS}" >nul && cl /nologo /EHsc /std:c++17 test.cpp /Fe:test.exe && test.exe\n')
            result=subprocess.run(['cmd','/d','/c','run.cmd'],cwd=work,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
