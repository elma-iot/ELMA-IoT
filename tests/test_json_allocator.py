"""Exercise allocation reserves with the real LCD/storage allocator headers."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
VCVARS = next((p for p in (
    pathlib.Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat'),
    pathlib.Path('C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat'),
) if p.is_file()), None)


class JsonAllocatorTests(unittest.TestCase):
    @unittest.skipUnless(VCVARS, 'MSVC compiler unavailable')
    def test_shrink_succeeds_below_reserve_and_failed_growth_preserves_data(self):
        with tempfile.TemporaryDirectory() as folder:
            work = pathlib.Path(folder)
            for name in ('panel_memory.h', 'storage_memory.h'):
                shutil.copyfile(ROOT / 'include' / name, work / name)
            (work / 'ArduinoJson.h').write_text('''#pragma once
#include <cstddef>
namespace ArduinoJson {struct Allocator {
 virtual ~Allocator()=default;
 virtual void* allocate(size_t)=0;
 virtual void deallocate(void*)=0;
 virtual void* reallocate(void*,size_t)=0;
};}
''')
            (work / 'esp_heap_caps.h').write_text('''#pragma once
#include <cstdlib>
#include <cstring>
#include <algorithm>
using std::min;
enum { MALLOC_CAP_SPIRAM=1, MALLOC_CAP_8BIT=2, MALLOC_CAP_INTERNAL=4 };
inline size_t freeHeap=100000, liveBytes=0;
inline size_t heap_caps_get_allocated_size(void* p){return static_cast<size_t*>(p)[-1];}
inline size_t heap_caps_get_free_size(unsigned){return freeHeap;}
inline size_t heap_caps_get_largest_free_block(unsigned){return freeHeap;}
inline void* heap_caps_malloc(size_t n,unsigned caps){
 if((caps&MALLOC_CAP_SPIRAM)||n>freeHeap)return nullptr;
 auto p=static_cast<size_t*>(malloc(n+sizeof(size_t)));if(!p)return nullptr;
 *p=n;freeHeap-=n;liveBytes+=n;return p+1;
}
inline void heap_caps_free(void* p){if(!p)return;auto n=heap_caps_get_allocated_size(p);liveBytes-=n;freeHeap+=n;free(static_cast<size_t*>(p)-1);}
inline bool psramFound(){return false;}
''')
            (work / 'Arduino.h').write_text('#pragma once\n#include "esp_heap_caps.h"\n')
            (work / 'main.cpp').write_text('''
#include "panel_memory.h"
#include "storage_memory.h"
#include <cassert>
template<class T> void verify(){
 freeHeap=100000;T allocator;
 auto p=static_cast<char*>(allocator.allocate(128));assert(p);
 memset(p,0x5a,128);freeHeap=0;
 assert(allocator.reallocate(p,32)==p);
 assert(allocator.reallocate(p,128)==p);
 assert(allocator.reallocate(p,256)==nullptr);
 for(int i=0;i<128;i++)assert(p[i]==0x5a);
 allocator.deallocate(p);assert(liveBytes==0);
 freeHeap=100000;p=static_cast<char*>(allocator.allocate(128));assert(p);
 allocator.deallocate(p);assert(liveBytes==0);
}
int main(){verify<PanelJsonAllocator>();verify<StorageJsonAllocator>();verify<SettingsJsonAllocator>();verify<SettingsFlashJsonAllocator>();}
''')
            command = f'call "{VCVARS}" >nul && cl /nologo /EHsc /std:c++17 /I. /DAPP_SUNTON_PANEL=3 main.cpp /Fe:test.exe && test.exe'
            (work / 'run.cmd').write_text('@echo off\n' + command + '\n')
            result = subprocess.run(['cmd', '/d', '/c', 'run.cmd'], cwd=work, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
