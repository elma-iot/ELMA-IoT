"""Exercise actual portable array shutdown with a recording pixel driver."""
import pathlib, subprocess, tempfile, unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
class ShutdownTest(unittest.TestCase):
 def test_black_frame_and_boot_order(self):
  vc=pathlib.Path('C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat')
  if not vc.exists(): vc=pathlib.Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
  if not vc.exists(): self.skipTest('MSVC required')
  headers=next(ROOT.glob('.pio/libdeps/*/ArduinoJson/src'))
  with tempfile.TemporaryDirectory() as tmp:
   p=pathlib.Path(tmp)
   (p/'Arduino.h').write_text("#pragma once\n#include <algorithm>\n#include <cstdint>\n#include <string>\nusing String=std::string;\ninline uint32_t millis(){return 0;}\ninline void delayMicroseconds(unsigned){}\n#define constrain(v,l,h) std::min(std::max((v),(l)),(h))\n")
   (p/'Adafruit_NeoPixel.h').write_text("#pragma once\n#include <vector>\n#define NEO_GRB 0\n#define NEO_KHZ800 0\nclass Adafruit_NeoPixel{public:std::vector<uint32_t> values;std::vector<std::vector<uint32_t>> frames;void updateType(int){}void setPin(int){}void updateLength(int n){values.resize(n);}uint8_t* getPixels(){return reinterpret_cast<uint8_t*>(values.data());}void begin(){}void setPixelColor(int i,uint32_t c){values[i]=c;}void show(){frames.push_back(values);}};\n")
   (p/'test.cpp').write_text(r'''#define APP_DISABLE_AUDIO
#include "led_array.h"
#include <cassert>
int main(){
 auto& a=LedArrays::arrays[0];a.pin=5;a.count=32;a.on=true;a.initialClearPending=true;a.pixels.updateLength(32);a.segmentCount=1;a.segments[0].count=32;
 LedArrays::bootWifi(false,true,0);LedArrays::tick(0);
 assert(a.pixels.frames.size()==1);for(auto c:a.pixels.frames.back())assert(c==0);
 LedArrays::tick(25);for(auto c:a.pixels.frames.back())assert(c==0x003300);
 assert(LedArrays::shutdown());for(auto c:a.pixels.frames.back())assert(c==0);
 auto count=a.pixels.frames.size();LedArrays::tick(100);assert(a.pixels.frames.size()==count);assert(!LedArrays::bootActive());
 // Every output is blanked, including already-off/release-pending outputs.
 auto& b=LedArrays::arrays[1];b.pin=6;b.count=16;b.releasePending=true;b.pixels.updateLength(16);for(int i=0;i<16;i++)b.pixels.setPixelColor(i,0xffffff);
 assert(LedArrays::shutdown());assert(b.pixels.frames.size()==1);for(auto c:b.pixels.frames.back())assert(c==0);
}''')
   (p/'run.cmd').write_text(f'call "{vc}" >nul\ncl /nologo /std:c++17 /EHsc /I"{p}" /I"{headers}" /I"{ROOT / "src"}" "{p / "test.cpp"}" /Fe:"{p / "test.exe"}"\n')
   subprocess.run(['cmd','/c',str(p/'run.cmd')],cwd=p,check=True)
   subprocess.run([str(p/'test.exe')],check=True)
if __name__=='__main__':unittest.main()
