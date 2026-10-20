"""Check the board's clock pair and HTTP lease without hardware clock writes."""
from pathlib import Path
import subprocess,tempfile,unittest,shutil
from test_json_allocator import VCVARS
ROOT=Path(__file__).resolve().parents[1]
class CpuTests(unittest.TestCase):
    @unittest.skipUnless(VCVARS,'MSVC compiler unavailable')
    def test_web_boost_wins_over_idle_and_expires_across_wrap(self):
        with tempfile.TemporaryDirectory() as folder:
            w=Path(folder);shutil.copyfile(ROOT/'include/sunton_cpu.h',w/'sunton_cpu.h')
            (w/'Arduino.h').write_text('''#pragma once
#include <cstdint>
#include <cassert>
inline uint32_t now=1,mhz=160;
inline uint32_t millis(){return now;}
struct Esp{uint32_t getCpuFreqMHz(){return mhz;}};inline Esp ESP;
using SemaphoreHandle_t=int;struct StaticSemaphore_t{};
inline int xSemaphoreCreateMutexStatic(StaticSemaphore_t*){return 1;}
inline int xSemaphoreTake(int,int){return 1;}inline void xSemaphoreGive(int){}
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
''')
            (w/'esp32-hal-cpu.h').write_text('''#pragma once
inline bool setCpuFrequencyMhz(uint32_t f){assert(f==80||f==160);mhz=f;return true;}
''')
            (w/'main.cpp').write_text('''#include "sunton_cpu.h"
int main(){
assert(suntonSetCpu(80)&&mhz==80);suntonWebBoost();assert(mhz==160&&suntonWebBusy());
assert(suntonSetCpu(80)&&mhz==160);now+=10001;assert(!suntonWebBusy());assert(suntonSetCpu(80)&&mhz==80);
assert(!suntonSetCpu(240)&&mhz==80);
now=0xfffffff0U;suntonWebBoost();now=20;assert(suntonWebBusy());assert(suntonSetCpu(80)&&mhz==160);
now=20000;assert(!suntonWebBusy());assert(suntonSetCpu(80)&&mhz==80);
}
''')
            (w/'run.cmd').write_text(f'@echo off\ncall "{VCVARS}" >nul && cl /nologo /EHsc /std:c++17 /I. /DAPP_SUNTON_PANEL=3 main.cpp /Fe:test.exe && test.exe\n')
            r=subprocess.run(['cmd','/d','/c','run.cmd'],cwd=w,capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
