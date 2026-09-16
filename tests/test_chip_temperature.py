"""Compile and exercise production temperature acquisition for each chip family."""
import pathlib, subprocess, tempfile, unittest, shutil
ROOT = pathlib.Path(__file__).resolve().parents[1]
VCVARS = pathlib.Path('C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat')
class TemperatureTests(unittest.TestCase):
 @unittest.skipUnless(VCVARS.is_file(), 'MSVC unavailable')
 def test_temperature_samples_expiry_and_wrap(self):
  for target in ('ESP32', 'ESP32C3', 'ESP32S3'):
   with self.subTest(target=target), tempfile.TemporaryDirectory() as folder:
    work=pathlib.Path(folder)
    shutil.copyfile(ROOT/'src/chip_temperature.h',work/'chip_temperature.h')
    (work/'main.cpp').write_text(r'''
#include <cmath>
#include <cassert>
#include <cstdint>
using std::isfinite;
uint8_t raw=128; float reading=NAN;
extern "C" uint8_t temprature_sens_read(){return raw;}
float temperatureRead(){return reading;}
#include "chip_temperature.h"
int main(){
 ChipTemperature sensor;
 sensor.sample(100);assert(!sensor.available(100));
 raw=163;reading=72.77778f;
 sensor.sample(200);assert(sensor.available(200));assert(std::fabs(sensor.valueC()-72.77778f)<0.001);
 raw=128;reading=NAN;
 sensor.sample(2200);assert(sensor.available(2200));assert(sensor.ageMs(2200)==2000);
 assert(std::fabs(sensor.valueC()-72.77778f)<0.001);
 sensor.sample(30200);assert(!sensor.available(30200));
 raw=160;reading=71.11111f;
 sensor.sample(31000);assert(sensor.available(31000));assert(sensor.ageMs(31000)==0);
 assert(std::fabs(sensor.valueC()-71.11111f)<0.001);
 sensor.sample(UINT32_MAX-999);raw=128;reading=INFINITY;
 sensor.sample(1000);assert(sensor.available(1000));assert(sensor.ageMs(1000)==2000);
 assert(!sensor.available(29000));
}
''')
    (work/'build.cmd').write_text(f'@call "{VCVARS}" >nul\ncl /nologo /std:c++17 /EHsc /DARDUINO_ARCH_ESP32 /DCONFIG_IDF_TARGET_{target} main.cpp /Fe:test.exe\nif errorlevel 1 exit /b 1\ntest.exe\n')
    result=subprocess.run(['cmd.exe','/c',str(work/'build.cmd')],cwd=work,capture_output=True,text=True)
    self.assertEqual(result.returncode,0,result.stdout+result.stderr)
if __name__=='__main__':unittest.main()
