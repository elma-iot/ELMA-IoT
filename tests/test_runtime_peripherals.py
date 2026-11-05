"""Compile and exercise the actual driver factory and NeoPixel runtime."""
import pathlib, subprocess, tempfile, unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
class RuntimePeripheralTests(unittest.TestCase):
 def test_runtime_factory(self):
  vc=pathlib.Path('C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat')
  if not vc.exists():vc=pathlib.Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
  if not vc.exists():self.skipTest('MSVC required')
  headers=next(ROOT.glob('.pio/libdeps/*/ArduinoJson/src'))
  with tempfile.TemporaryDirectory() as directory:
   p=pathlib.Path(directory)
   (p/'Arduino.h').write_text('#pragma once\n#include <algorithm>\n#include <cstdint>\n#include <string>\nusing String=std::string;\n#define PROGMEM\n#define FPSTR(x) (x)\ninline uint32_t millis(){return 0;}\ninline void delayMicroseconds(unsigned){}\n#define constrain(v,l,h) std::min(std::max((v),(l)),(h))\n')
   (p/'Adafruit_NeoPixel.h').write_text('#pragma once\n#include <vector>\n#define NEO_GRB 0\n#define NEO_KHZ800 0\nclass Adafruit_NeoPixel{public:std::vector<uint32_t> values;void updateType(int){}void setPin(int){}void updateLength(int n){values.resize(n);}uint8_t* getPixels(){return reinterpret_cast<uint8_t*>(values.data());}void begin(){}void setPixelColor(int i,uint32_t c){values[i]=c;}void show(){}};\n')
   for flags in ['','/DAPP_DISABLE_AUDIO=1 /DCONFIG_IDF_TARGET_ESP32C3=1','/DAPP_DISABLE_AUDIO=1 /DAPP_ESP8266_COMPACT=1','/DAPP_DISABLE_AUDIO=1 /DAPP_DISABLE_DISPLAY=1 /DAPP_DISABLE_SD=1']:
    with self.subTest(flags=flags):
     (p/'run.cmd').write_text(f'call "{vc}" >nul\ncl /nologo /std:c++17 /EHsc {flags} /I"{p}" /I"{headers}" /I"{ROOT / "src"}" /I"{ROOT / "include"}" "{ROOT / "tests/runtime_peripherals_host.cpp"}" /Fe:"{p / "test.exe"}"\n')
     subprocess.run(['cmd','/c',str(p/'run.cmd')],cwd=p,check=True)
     subprocess.run([str(p/'test.exe')],check=True)
if __name__=='__main__':unittest.main()
