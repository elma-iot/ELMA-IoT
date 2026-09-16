"""Compile the real display manager against recording display drivers on Windows."""
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
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
using std::min;
using std::max;
inline unsigned long clockMs = 0;
inline unsigned long millis() { return clockMs; }
class String : public std::string {
public:
    using std::string::string;
    String(const std::string& s) : std::string(s) {}
    String(unsigned int n) : std::string(std::to_string(n)) {}
    String(int n) : std::string(std::to_string(n)) {}
    String(float n, int digits) {
        std::ostringstream out; out << std::fixed << std::setprecision(digits) << n;
        assign(out.str());
    }
    bool isEmpty() const { return empty(); }
    String substring(size_t start) const { return substr(start); }
    String substring(size_t start, size_t end) const { return substr(start, end-start); }
    String& operator+=(uint8_t n) { append(std::to_string(n)); return *this; }
    using std::string::operator+=;
};
inline int flushes = 0, contrastWrites = 0;
inline bool beginOk = true;
inline std::vector<int> colors;
inline std::vector<int> contrasts;
inline std::vector<String> printed;
class Adafruit_GFX {
public:
    int16_t width() { return 128; }
    int16_t height() { return 64; }
    void setRotation(int) {}
    void setTextWrap(bool wrap) { assert(!wrap); }
    void setTextSize(int) {}
    void setTextColor(int color) { colors.push_back(color); }
    void setCursor(int,int) {}
    void print(const String& text) { printed.push_back(text); }
    void drawLine(int,int,int,int,int color) { colors.push_back(color); }
    void drawRect(int,int,int,int,int color) { colors.push_back(color); }
    void fillRect(int,int,int,int,int color) { colors.push_back(color); }
    void clearDisplay() {}
    void display() { ++flushes; }
};
struct WireMock { void begin(int,int) {} };
inline WireMock Wire;
class Adafruit_SSD1306 : public Adafruit_GFX {
public:
    Adafruit_SSD1306(int,int,WireMock*,int) {}
    bool begin(int,int) { return beginOk; }
    void dim(bool value) { ++contrastWrites; contrasts.push_back(value ? 0 : 255); }
    void ssd1306_command(int) {}
};
class Adafruit_SH1106G : public Adafruit_GFX {
public:
    Adafruit_SH1106G(int,int,WireMock*,int) {}
    bool begin(int,bool) { return beginOk; }
    void setContrast(int value) { ++contrastWrites; contrasts.push_back(value); }
    void oled_command(int) {}
};
constexpr int SSD1306_WHITE=1, SSD1306_SWITCHCAPVCC=2, SSD1306_DISPLAYOFF=0xAE, SH110X_DISPLAYOFF=0xAE;
struct OledSettings {
    bool enabled=true;
    String displayType="oled", driver="ssd1306";
    int rotation=0, sdaPin=23, sclPin=19, resetPin=-1, width=128, height=64, i2cAddress=60;
    uint16_t dimTimeoutSeconds=0;
};
struct AppStateSnapshot {
    struct { bool busy=false; String phase; uint8_t progressPercent=0; } ota;
    struct { String lastError; } system;
    struct { String state="idle", title, url; } playback;
    struct { bool wifiConnected=true, apMode=false, mqttConnected=true; String ip="192.168.1.2", apSsid; } network;
    struct { float voltage=4.0f; } battery;
};
'''

MAIN = r'''
#include "display_manager.h"
void reset() { clockMs=0; flushes=0; contrastWrites=0; colors.clear(); contrasts.clear(); printed.clear(); }
int main() {
    for (const auto driver : {"ssd1306", "sh1106"}) {
        reset();
        DisplayManager display;
        OledSettings settings; settings.driver=driver;
        AppStateSnapshot state;
        display.begin(settings);
        display.setBootMessage("Idle");
        flushes=0;
        for(int i=1;i<=10;++i) { clockMs=i*300; display.loop(state); }
        assert(flushes==1); // No full-screen refreshes for identical static content.
        assert(contrastWrites==0); // No repeated brightness commands.
        state.ota.busy=true; state.ota.phase="Updating"; state.ota.progressPercent=10;
        clockMs+=300; display.loop(state);
        state.ota.progressPercent=20;
        clockMs+=300; display.loop(state);
        assert(flushes==3); // Progress-only changes still redraw.
        for(int color:colors) assert(color==SSD1306_WHITE);
        state.ota.busy=false;
        state.playback.state="playing"; state.playback.title="A long scrolling title for the display";
        printed.clear(); clockMs+=300; display.loop(state);
        assert(printed[1]=="A long scr");
        printed.clear(); state.battery.voltage=3.9f; clockMs+=300; display.loop(state);
        assert(printed[1]==" long scro"); // Battery updates must not restart scrolling.
        settings.dimTimeoutSeconds=1;
        display.applySettings(settings);
        state.network.wifiConnected=false; state.playback.state="idle";
        for(int i=0;i<10;++i) { clockMs+=300; display.loop(state); }
        assert(contrastWrites==1 && contrasts.back()==0);
        display.markActivity(); display.markActivity();
        assert(contrastWrites==2 && contrasts.back()==255);
        beginOk=false; display.applySettings(settings);
        int before=flushes; clockMs+=300; display.loop(state);
        assert(flushes==before); // Failed initialization never uses an invalid buffer.
        beginOk=true;
    }
}
'''


class DisplayManagerTests(unittest.TestCase):
    @unittest.skipUnless(VCVARS.is_file(), 'MSVC compiler unavailable')
    def test_rendering_brightness_and_refresh_regressions(self):
        with tempfile.TemporaryDirectory() as folder:
            work = pathlib.Path(folder)
            for name in ('display_manager.h', 'display_manager.cpp'):
                shutil.copyfile(ROOT / 'src' / name, work / name)
            (work / 'shim.h').write_text(SHIM)
            for name in ('Arduino.h', 'Wire.h', 'Adafruit_GFX.h', 'Adafruit_SSD1306.h',
                         'Adafruit_SH110X.h', 'app_state.h', 'settings_schema.h'):
                (work / name).write_text('#include "shim.h"\n')
            (work / 'main.cpp').write_text(MAIN)
            (work / 'build.cmd').write_text(
                f'@call "{VCVARS}" >nul\n'
                'cl /nologo /std:c++17 /EHsc /I. main.cpp display_manager.cpp /Fe:display-test.exe\n'
                'if errorlevel 1 exit /b 1\n'
                'display-test.exe\n'
            )
            result = subprocess.run(['cmd.exe', '/c', str(work / 'build.cmd')], cwd=work,
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
