"""Check unsigned DAC silence and the boot bias transition with fake drivers."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest
from test_json_allocator import VCVARS

ROOT = pathlib.Path(__file__).resolve().parents[1]


class SuntonDacTests(unittest.TestCase):
    @unittest.skipUnless(VCVARS, 'MSVC compiler unavailable')
    def test_dma_is_primed_before_dac_enable_and_bias_rises_gradually(self):
        with tempfile.TemporaryDirectory() as folder:
            work = pathlib.Path(folder)
            shutil.copyfile(ROOT / 'include/sunton_dac.h', work / 'sunton_dac.h')
            shutil.copyfile(ROOT / 'include/sunton_dac_quality.h', work / 'sunton_dac_quality.h')
            (work / 'driver').mkdir()
            (work / 'Arduino.h').write_text('''#pragma once
#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <cassert>
using std::min;
inline void delay(int n){assert(n==1);}
#define OUTPUT 1
#define LOW 0
inline void pinMode(int p,int m){assert(p==26&&m==OUTPUT);}
inline void digitalWrite(int p,int v){assert(p==26&&v==LOW);}
#define pdMS_TO_TICKS(x) (x)
''')
            (work / 'driver/i2s.h').write_text('''#pragma once
#include <Arduino.h>
using i2s_port_t=int;using i2s_dac_mode_t=int;
enum {ESP_OK=0,I2S_DAC_CHANNEL_DISABLE=0,I2S_DAC_CHANNEL_RIGHT_EN=1,I2S_DAC_CHANNEL_LEFT_EN=2};
inline int zeroCalls=0,writeCalls=0,mode=0;
inline size_t bytesPrimed=0;
inline int i2s_zero_dma_buffer(int){++zeroCalls;return 0;}
inline int i2s_write(int,const void* p,size_t n,size_t* written,int){
 auto words=static_cast<const uint32_t*>(p);
 for(size_t i=0;i<n/4;++i)assert(words[i]==0x80008000U);
 ++writeCalls;bytesPrimed+=n;*written=n;return 0;
}
inline int i2s_set_dac_mode(int next){if(next)assert(bytesPrimed>=4096);mode=next;return 0;}
''')
            (work / 'driver/dac.h').write_text('''#pragma once
#include <Arduino.h>
enum {DAC_CHANNEL_1=1,DAC_CHANNEL_2=2};
inline int lastLevel=-1,rampCalls=0;
inline void dac_i2s_disable(){mode=0;}
inline void dac_output_disable(int channel){assert(channel==2);}
inline void dac_output_enable(int channel){assert(channel==2);}
inline void dac_output_voltage(int channel,uint8_t level){
 assert(channel==2&&mode==0&&bytesPrimed>=4096);
 assert(level<=128);lastLevel=level;++rampCalls;
}
''')
            (work / 'soc').mkdir()
            (work / 'rom').mkdir()
            (work / 'driver/rtc_io.h').write_text('#pragma once\nenum {GPIO_NUM_26=26};\ninline void rtc_gpio_deinit(int pin){assert(pin==26);}\n')
            (work / 'soc/gpio_sig_map.h').write_text('#pragma once\n#define SIG_GPIO_OUT_IDX 256\n')
            (work / 'rom/gpio.h').write_text('#pragma once\ninline void gpio_matrix_out(int pin,int signal,bool a,bool b){assert(pin==26&&signal==256&&!a&&!b);}\n')
            (work / 'main.cpp').write_text('''#include "sunton_dac.h"
int main(){
 const uint32_t pcm=(uint32_t(uint16_t(30000))<<16)|uint16_t(-20000);
 for(int gain=0;gain<=100;++gain){uint32_t scaled=elmaScaleDacPcm(pcm,gain);assert(int16_t(scaled>>16)==30000*gain/100);assert(int16_t(scaled)==-20000*gain/100);}
 assert(elmaScaleDacPcm(pcm,255)==pcm);
 assert(elmaScaleDacPcm(pcm,0)==0);assert(elmaScaleDacPcm(pcm,100)==pcm);
 assert(elmaDacSampleRate(44100,true)==44100);
 assert(elmaDacSampleRate(48000,false)==48000);
 assert(elmaAudioFrameBytes(true)==4);
 assert(elmaAudioDmaLength(256,true)==256);
 size_t written=0;
 elmaWriteAudioSample(0,0,true,&written,100);
 assert(written==4);bytesPrimed=0;writeCalls=0;
 assert(!elmaAudioAutoClear(true));assert(elmaAudioAutoClear(false));
 elmaStartDacOutput(0,I2S_DAC_CHANNEL_LEFT_EN,4,256);
 assert(mode==0&&rampCalls==0&&writeCalls==16&&zeroCalls==0);
 elmaGateSuntonDac(true);assert(mode==2&&lastLevel==128&&rampCalls==65);
 elmaGateSuntonDac(true);assert(rampCalls==65);
 elmaGateSuntonDac(false);assert(mode==0&&lastLevel==0&&rampCalls==130);
 elmaGateSuntonDac(false);assert(rampCalls==130);
 elmaClearAudioDma(0,true,4,256);assert(bytesPrimed==8192&&zeroCalls==0);
 elmaClearAudioDma(0,false,4,256);assert(zeroCalls==1);
 elmaStartDacOutput(0,I2S_DAC_CHANNEL_LEFT_EN,4,256);assert(rampCalls==130&&mode==0);
}
''')
            (work / 'run.cmd').write_text(f'@echo off\ncall "{VCVARS}" >nul && cl /nologo /EHsc /std:c++17 /I. /DAPP_SUNTON_PANEL=3 /DCONFIG_IDF_TARGET_ESP32 main.cpp /Fe:test.exe && test.exe\n')
            result = subprocess.run(['cmd', '/d', '/c', 'run.cmd'], cwd=work, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
