"""Run real logger code against recording NVS/SD adapters, without ESP hardware."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
VCVARS = pathlib.Path('C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat')
SHIM = r'''
#pragma once
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
using std::min;
class String : public std::string {
public:
    using std::string::string;
    String(const std::string& s):std::string(s){}
    String(int n):std::string(std::to_string(n)){}
    String(unsigned long n):std::string(std::to_string(n)){}
    String(unsigned int n):std::string(std::to_string(n)){}
    bool startsWith(const char* p) const {return find(p)==0;}
    void concat(const char* p,size_t n){append(p,n);}
    String substring(size_t offset) const {return substr(offset);}
};
class Print {public: virtual size_t write(uint8_t)=0; virtual size_t write(const uint8_t*,size_t)=0;};
inline struct SerialMock {size_t write(const uint8_t*,size_t n){return n;}} Serial;
inline unsigned long nowMs=0;
inline unsigned long millis(){return nowMs;}
using SemaphoreHandle_t=int*;
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) (void)0
#define portEXIT_CRITICAL(x) (void)0
#define pdTRUE 1
inline int semaphore=0;
inline SemaphoreHandle_t xSemaphoreCreateMutex(){return &semaphore;}
inline int xSemaphoreTake(SemaphoreHandle_t,int){return 1;}
inline void xSemaphoreGive(SemaphoreHandle_t){}
using vprintf_like_t=int(*)(const char*,va_list);
inline vprintf_like_t esp_log_set_vprintf(vprintf_like_t){return nullptr;}
inline uint32_t esp_random(){static uint32_t id=0;return ++id;}
inline int esp_reset_reason(){return 1;}
#define APP_VERSION "0.1.43"
inline std::map<std::string,std::string> nvs;
inline bool prefsOk=true, sdMounted=false, sdWriteOk=true, busy=false;
inline int nvsWrites=0;
inline size_t nvsMaxBytes=2048;
class Preferences {
public:
    bool begin(const char*,bool){return prefsOk;}
    void end(){}
    uint8_t getUChar(const char* k,uint8_t d){return nvs.count(k)?static_cast<uint8_t>(nvs[k][0]):d;}
    size_t putUChar(const char* k,uint8_t n){nvs[k]=std::string(1,n);return 1;}
    bool remove(const char* k){return nvs.erase(k)>0;}
    size_t getBytesLength(const char* k){return nvs[k].size();}
    size_t getBytes(const char* k,void* p,size_t n){memcpy(p,nvs[k].data(),n);return n;}
    size_t putBytes(const char* k,const void* p,size_t n){++nvsWrites;if(n>nvsMaxBytes)return 0;nvs[k]=std::string(static_cast<const char*>(p),n);return n;}
};
inline std::string disk;
class File {
    bool valid_=false;
    size_t pos_=0;
public:
    File()=default;
    File(const char* mode):valid_(sdMounted){if(*mode=='w')disk.clear();if(*mode=='a')pos_=disk.size();}
    explicit operator bool() const {return valid_;}
    size_t size(){return disk.size();}
    void close(){valid_=false;}
    void flush(){}
    bool seek(size_t n){pos_=n;return true;}
    size_t write(const uint8_t* p,size_t n){if(!sdWriteOk)return 0;disk.append(reinterpret_cast<const char*>(p),n);pos_+=n;return n;}
    int read(uint8_t* p,size_t n){n=min(n,disk.size()-pos_);memcpy(p,disk.data()+pos_,n);pos_+=n;return static_cast<int>(n);}
};
enum class StorageTarget {Sd};
inline bool storageMounted(StorageTarget){return sdMounted;}
inline bool storageBusy(StorageTarget){return busy;}
inline void beginStorageWrite(StorageTarget){}
inline void endStorageWrite(StorageTarget){}
inline void beginStorageRead(StorageTarget){}
inline void endStorageRead(StorageTarget){}
inline File storageOpen(StorageTarget,const char*,const char* mode){return File(mode);}
struct JsonObject {
    std::map<std::string,std::string>* values;
    struct Slot {
        std::string& value;
        void operator=(const char* s){value=s;}
        void operator=(const String& s){value=s;}
        void operator=(bool b){value=b?"true":"false";}
        void operator=(size_t n){value=std::to_string(n);}
    };
    Slot operator[](const char* key){return {(*values)[key]};}
};
'''
MAIN = r'''
#include "device_log.h"
using Values=std::map<std::string,std::string>;
Values snapshot(DeviceLogger& log,const String& since="") {Values out;assert(log.snapshot(JsonObject{&out},since));return out;}
int main(){
    DeviceLogger first;
    first.begin(); first.capture("first boot\n",11); first.service(true);
    const int writes=nvsWrites;
    first.capture("later\n",6); nowMs=3000; first.service(); assert(nvsWrites==writes);
    nowMs=60000; first.service(); assert(nvsWrites==writes+1);
    DeviceLogger second; second.begin(); second.capture("second boot\n",12); second.service(true);
    auto two=snapshot(second); assert(two["text"].find("first boot")!=std::string::npos);
    assert(two["text"].find("second boot")!=std::string::npos);
    DeviceLogger third; third.begin(); third.capture("third boot\n",11); third.service(true);
    auto three=snapshot(third); assert(three["text"].find("first boot")==std::string::npos);
    assert(three["text"].find("second boot")!=std::string::npos);
    std::string huge(20000,'X'); third.capture(huge.data(),huge.size()); third.service(true);
    assert(nvs["boot0"].size()<=2048 && nvs["boot1"].size()<=2048);
    auto bounded=snapshot(third); assert(bounded["text"].size()<=4096);
    assert(bounded["text"].find("=== Boot")!=std::string::npos);
    auto unchanged=snapshot(third,bounded["revision"]); assert(unchanged["unchanged"]=="true");
    assert(!unchanged.count("text"));
    sdMounted=true; disk.assign(DeviceLogger::ExternalBytes-2,'Z'); third.service(true);
    assert(disk.size()<=DeviceLogger::ExternalBytes && disk.size()<10000);
    assert(disk.find("Boot")!=std::string::npos);
    assert(snapshot(third)["source"]=="sd");
    const auto savedDisk=disk;
    third.capture("pending",7); busy=true; nowMs+=3000; third.service(); assert(disk==savedDisk);
    busy=false; sdWriteOk=false; third.service(true);
    assert(snapshot(third)["source"]=="internal");
    sdWriteOk=true; third.service(true); assert(snapshot(third)["source"]=="sd");
    disk.assign(DeviceLogger::ExternalBytes,'A');
    auto tail=snapshot(third); assert(tail["text"].size()<=DeviceLogger::ViewBytes);
    sdMounted=false; nowMs+=3000; third.service(); assert(snapshot(third)["source"]=="internal");
    const char* other=nvs["active"][0]?"boot0":"boot1";
    const auto previous=nvs[other];nvsMaxBytes=512;
    third.capture(huge.data(),huge.size());third.service(true);
    assert(snapshot(third)["source"]=="internal");
    assert(snapshot(third)["checkpointBytes"]=="512");
    assert(nvs[other]==previous);
    const char* active=nvs["active"][0]?"boot1":"boot0";
    assert(nvs[active].size()<=512 && nvs[active].find("=== Boot")!=std::string::npos);
    prefsOk=false; DeviceLogger ram; ram.begin(); ram.capture("still visible",13);
    auto live=snapshot(ram); assert(live["source"]=="ram"); assert(live["text"].find("still visible")!=std::string::npos);
}
'''

class DeviceLogTests(unittest.TestCase):
    @unittest.skipUnless(VCVARS.is_file(), 'MSVC compiler unavailable')
    def test_retention_rotation_failure_and_write_batching(self):
        with tempfile.TemporaryDirectory() as folder:
            work=pathlib.Path(folder)
            for name in ('device_log.h','device_log.cpp'):
                shutil.copyfile(ROOT/'src'/name,work/name)
            (work/'shim.h').write_text(SHIM)
            for name in ('Arduino.h','ArduinoJson.h','Preferences.h','storage_backend.h','version.h',
                         'esp_log.h','esp_system.h','freertos/FreeRTOS.h','freertos/semphr.h'):
                path=work/name; path.parent.mkdir(exist_ok=True); path.write_text('#include "shim.h"\n')
            (work/'main.cpp').write_text(MAIN)
            (work/'build.cmd').write_text(f'@call "{VCVARS}" >nul\ncl /nologo /std:c++17 /EHsc /I. main.cpp device_log.cpp /Fe:log-test.exe\nif errorlevel 1 exit /b 1\nlog-test.exe\n')
            result=subprocess.run(['cmd.exe','/c',str(work/'build.cmd')],cwd=work,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)

if __name__=='__main__': unittest.main()
