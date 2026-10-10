#include "panel_display.h"
#if APP_HAS_ONBOARD_PANEL
#include "device_log.h"
#include "panel_boot.h"
#if APP_ROTARY_HMI
#include <Wire.h>
#endif

bool PanelDisplay::begin(bool touchEnabled, uint8_t rotation, uint8_t percent) {
    if (!getBuffer()) return false;
#if APP_SUNTON_PANEL
    board_.reset(new SuntonPanel());
    if(!board_->start(touchEnabled)){board_.reset();return false;}
#else
    esp_panel::board::Board defaults;
    auto config = defaults.getConfig();
#if APP_ROTARY_HMI
    if (config.touch) {
        auto* bus = std::get_if<esp_panel::drivers::BusI2C::Config>(&config.touch->bus_config);
        if (bus && bus->host) {
            if (auto* host=std::get_if<esp_panel::drivers::BusI2C::HostPartialConfig>(&bus->host.value())) host->clk_speed=100000;
            else if (auto* host=std::get_if<esp_panel::drivers::BusI2C::HostFullConfig>(&bus->host.value())) host->master.clk_speed=100000;
        }
    }
#endif
    if (!touchEnabled) config.touch.reset();
    board_.reset(new esp_panel::board::Board(config));
    if (!board_->init() || !board_->begin()) {
        board_.reset();
#if APP_ROTARY_HMI
        if (!touchEnabled) return false;
        // Touch is optional: a missing/sleeping controller must not prevent
        // the LCD and backlight from starting on the rotary board.
        DebugLog.println("[display] Touch-enabled startup failed; checking documented I2C pins SDA=16 SCL=15");
        if (Wire.begin(16, 15, 100000)) {
            for (uint8_t address=1; address<127; ++address) {
                Wire.beginTransmission(address);
                if (Wire.endTransmission()==0) {
                    DebugLog.printf("[display] I2C device address=0x%02x\n", address);
                    for (uint8_t reg : {uint8_t(0xa7), uint8_t(0xa8), uint8_t(0xa9)}) {
                        Wire.beginTransmission(address); Wire.write(reg);
                        if (Wire.endTransmission(false)==0 && Wire.requestFrom(address, uint8_t(1))==1)
                            DebugLog.printf("[display] I2C 0x%02x register 0x%02x=0x%02x\n", address, reg, Wire.read());
                    }
                }
            }
            Wire.end();
        }
        config.touch.reset();
        board_.reset(new esp_panel::board::Board(config));
        if (!board_->init() || !board_->begin()) { board_.reset(); return false; }
        touchEnabled=false;
        DebugLog.println("[display] LCD running without touch; encoder navigation remains available");
#else
        return false;
#endif
    }
#endif
    setRotation(rotation);
#if APP_ROTARY_HMI
    if (board_->getTouch()) {
        auto* bus=static_cast<esp_panel::drivers::BusI2C*>(board_->getTouch()->getBus());
        bus->getConfig().printHostConfig();
        bus->getConfig().printControlPanelConfig();
    }
#endif
    brightness(percent);
    drawPanelBootLogo(*this);
    DebugLog.printf("[display] Panel %dx%d ready; PSRAM=%u; touch=%s\n",
                    kPanelWidth, kPanelHeight, ESP.getPsramSize(), touchEnabled ? kPanelTouchName : "disabled");
    return true;
}

void PanelDisplay::brightness(uint8_t percent) {
#if APP_SUNTON_PANEL
    if(board_)board_->setBrightness(min<uint8_t>(percent,100)*255/100);
#else
    if (board_ && board_->getBacklight()) board_->getBacklight()->setBrightness(min<uint8_t>(percent, 100));
#endif
}

void PanelDisplay::flush() {
    if (!board_ || !getBuffer()) return;
    // One bounded transfer at a time: never change the DMA source while in use.
    // RGB565 black/white is byte-order invariant.
    const uint8_t *pixels = getBuffer();
    for (int y = 0; y < kPanelHeight; ++y) {
        for (int x = 0; x < kPanelWidth; ++x) {
            uint8_t color = pixels[y * ((kPanelWidth + 7) / 8) + x / 8] & (0x80 >> (x & 7)) ? 255 : 0;
            line_[x * 2] = line_[x * 2 + 1] = color;
        }
        if (!drawColor(0, y, kPanelWidth, 1, line_)) break;
    }
}

bool PanelDisplay::drawColor(int x,int y,int width,int height,uint8_t* colors) {
#if APP_SUNTON_PANEL
    if(!board_)return false;
    // LV_COLOR_16_SWAP=1 already stores wire-order RGB565, as in LovyanGFX's LVGL port.
    board_->pushImage(x,y,width,height,reinterpret_cast<lgfx::swap565_t*>(colors));
    board_->verifyTransfer(x,y,width,height,colors);
    const bool ok=true;
#else
    const bool ok = board_ && board_->getLCD()->drawBitmap(x,y,width,height,colors,-1);
#endif
    if (!transferReported_ || (!ok && !transferFailed_)) {
        DebugLog.printf("[display] LCD transfer %s (%dx%d at %d,%d)\n",
                        ok ? "completed" : "FAILED", width, height, x, y);
        transferReported_ = true;
    }
    transferFailed_ = !ok;
    return ok;
}

bool PanelDisplay::readRawTouch(int16_t &x, int16_t &y) {
#if APP_SUNTON_PANEL
    return board_ && board_->point(x,y);
#else
    if (!board_ || !board_->getTouch()) return false;
    esp_panel::drivers::TouchPoint point;
    if (board_->getTouch()->readPoints(&point, 1, 0) <= 0) return false;
    const int16_t px = point.x, py = point.y;
    if (px < 0 || px >= kPanelWidth || py < 0 || py >= kPanelHeight) return false;
    x=px;y=py;return true;
#endif
}

bool PanelDisplay::readTouch(int16_t &x,int16_t &y) {
    int16_t px,py;if(!readRawTouch(px,py))return false;
    switch (getRotation()) {
        case 1: x = py; y = kPanelWidth - 1 - px; break;
        case 2: x = kPanelWidth - 1 - px; y = kPanelHeight - 1 - py; break;
        case 3: x = kPanelHeight - 1 - py; y = px; break;
        default: x = px; y = py;
    }
    return true;
}
#endif
