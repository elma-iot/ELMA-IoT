"""Exercise the firmware's actual MQTT allocation/backpressure methods with a fake transport."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
VCVARS = pathlib.Path('C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat')
SHIM = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
using std::max;
unsigned heap=100000, largest=50000;
struct { unsigned getFreeHeap(){return heap;} } ESP;
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
unsigned heap_caps_get_largest_free_block(int){return largest;}
int task=1;
int xTaskGetCurrentTaskHandle(){return task;}
struct Transport {
    bool online=true, reject=false;
    unsigned calls=0;
    bool connected(){return online;}
    uint16_t publish(const char* topic,uint8_t,bool,const char* payload,size_t){
        assert(topic && *topic && payload); ++calls; return reject?0:42;
    }
};
class MqttManager {
public:
    Transport client_;
    std::atomic<uint8_t> pendingPublishes_{0};
    std::atomic<bool> statePublishPending_{false}, discoveryRestartPending_{false}, discoveryPublishPending_{false};
    int publisherTask_=1;
    bool statePassActive_=false,statePassBlocked_=false;
    size_t statePassIndex_=0,stateCursor_=0,discoveryCursor_=0;
    void releasePublishSlot();
    uint16_t publishPacket(const char*,uint8_t,bool,const char*,size_t=0);
    bool publishDiscoveryStep(size_t,const std::function<uint16_t()>&);
    void publishState(); void publishChipTemperature(); void publishDiscovery();
};
'''
MAIN = r'''
int main(){
    MqttManager m;
    for(int i=0;i<10000;++i){m.publishState();m.publishChipTemperature();}
    assert(m.client_.calls==0 && m.statePublishPending_);
    assert(!m.publishPacket(nullptr,1,true,"x"));
    assert(!m.publishPacket("",1,true,"x"));
    assert(!m.publishPacket("topic",1,true,nullptr));
    assert(m.client_.calls==0);
    heap=20000; assert(!m.publishPacket("topic",1,true,"x")); heap=100000;
    largest=1024; assert(!m.publishPacket("topic",1,true,"x")); largest=50000;
    for(int i=0;i<4;++i) assert(m.publishPacket("topic",1,true,"x"));
    assert(!m.publishPacket("topic",1,true,"x") && m.client_.calls==4);
    m.releasePublishSlot(); assert(m.publishPacket("topic",1,true,""));
    for(int i=0;i<10;++i)m.releasePublishSlot(); assert(m.pendingPublishes_==0);
    m.client_.reject=true; assert(!m.publishPacket("topic",1,true,"x"));
    assert(m.pendingPublishes_==0); m.client_.reject=false;
    m.statePassActive_=true;
    for(int i=0;i<8;++i)m.publishPacket("state",1,true,"x");
    assert(m.stateCursor_==4 && m.statePassBlocked_);
    auto calls=m.client_.calls;
    for(int i=0;i<4;++i)m.releasePublishSlot();
    m.statePassBlocked_=false;m.statePassIndex_=0;
    for(int i=0;i<8;++i)m.publishPacket("state",1,true,"x");
    assert(m.stateCursor_==8 && m.client_.calls==calls+4);
    m.statePassActive_=false;
    unsigned constructed=0;
    for(size_t i=0;i<20;++i)if(m.publishDiscoveryStep(i,[&](){++constructed;return 0;}))break;
    assert(constructed==1 && m.discoveryCursor_==0);
    for(size_t i=0;i<20;++i)if(m.publishDiscoveryStep(i,[&](){++constructed;return 1;}))break;
    assert(constructed==2 && m.discoveryCursor_==1);
    m.publishDiscovery(); assert(m.discoveryRestartPending_ && m.discoveryPublishPending_);
}
'''

class MqttPublishTests(unittest.TestCase):
    @unittest.skipUnless(VCVARS.is_file(), 'MSVC compiler unavailable')
    def test_backpressure_and_deferred_publishing(self):
        source=(ROOT/'src/mqtt_manager.cpp').read_text()
        methods=source[source.index('void MqttManager::releasePublishSlot()'):source.index('void MqttManager::publishStateNow()')]
        with tempfile.TemporaryDirectory() as folder:
            work=pathlib.Path(folder)
            (work/'main.cpp').write_text(SHIM+methods+MAIN)
            (work/'build.cmd').write_text(f'@call "{VCVARS}" >nul\ncl /nologo /std:c++17 /EHsc main.cpp /Fe:mqtt-test.exe\nif errorlevel 1 exit /b 1\nmqtt-test.exe\n')
            result=subprocess.run(['cmd.exe','/c',str(work/'build.cmd')],cwd=work,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)

if __name__=='__main__': unittest.main()
