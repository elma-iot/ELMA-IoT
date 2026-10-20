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
    PanelDisplay() : GFXcanvas1(kPanelWidth, kPanelHeight,
#if APP_SUNTON_PANEL == 3
        false // LVGL/boot artwork draws directly; allocate text pixels on demand.
#else
        true
#endif
    ) {}
    bool begin(bool touchEnabled, uint8_t rotation, uint8_t brightness);
    void flush();
    bool readRawTouch(int16_t &x, int16_t &y);
    bool drawColor(int x,int y,int width,int height,uint8_t* colors);
    void brightness(uint8_t percent);
    void sleep(bool sleeping) {
#if APP_SUNTON_PANEL == 3
        if(board_){if(sleeping)board_->sleep();else board_->wakeup();}
#else
        (void)sleeping;
#endif
    }
    bool readTouch(int16_t &x, int16_t &y);
    void releaseTextBuffer(){if(buffer&&buffer_owned)free(buffer);buffer=nullptr;buffer_owned=true;}
    bool ensureTextBuffer(){if(!buffer){buffer=static_cast<uint8_t*>(calloc((kPanelWidth+7)/8*kPanelHeight,1));buffer_owned=true;}return buffer!=nullptr;}
    uint32_t spiClock() const {
#if APP_SUNTON_PANEL
        return board_?board_->spiClock():0;
#else
        return 0;
#endif
    }
    bool takeClockChange(){
#if APP_SUNTON_PANEL
        return board_&&board_->takeClockChange();
#else
        return false;
#endif
    }
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
