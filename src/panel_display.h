#pragma once
#if APP_HAS_ONBOARD_PANEL
#include <Adafruit_GFX.h>
#include <memory>
#include "panel_geometry.h"
#if APP_SUNTON_PANEL
#include "sunton_panel.h"
#else
#include <esp_display_panel.hpp>
#endif

// GFX keeps the existing ELMA text/OTA rendering and software rotation. The
// official board profile owns controller commands, mode pins, reset and PWM.
class PanelDisplay : public GFXcanvas1 {
public:
    PanelDisplay() : GFXcanvas1(kPanelWidth, kPanelHeight) {}
    bool begin(bool touchEnabled, uint8_t rotation, uint8_t brightness);
    void flush();
    bool readRawTouch(int16_t &x, int16_t &y);
    bool drawColor(int x,int y,int width,int height,uint8_t* colors);
    void brightness(uint8_t percent);
    bool readTouch(int16_t &x, int16_t &y);
private:
#if APP_SUNTON_PANEL
    std::unique_ptr<SuntonPanel> board_;
#else
    std::unique_ptr<esp_panel::board::Board> board_;
#endif
    uint8_t line_[kPanelWidth * 2] __attribute__((aligned(4)));
    bool transferReported_ = false;
    bool transferFailed_ = false;
};
#endif
