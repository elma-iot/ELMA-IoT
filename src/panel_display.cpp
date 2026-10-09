#include "panel_display.h"
#if APP_HAS_ONBOARD_PANEL
#include "device_log.h"

bool PanelDisplay::begin(bool touchEnabled, uint8_t rotation, uint8_t percent) {
    if (!getBuffer()) return false;
#if APP_SUNTON_PANEL
    board_.reset(new SuntonPanel());
    if(!board_->start(touchEnabled)){board_.reset();return false;}
#else
    esp_panel::board::Board defaults;
    auto config = defaults.getConfig();
    if (!touchEnabled) config.touch.reset();
    board_.reset(new esp_panel::board::Board(config));
    if (!board_->init() || !board_->begin()) { board_.reset(); return false; }
#endif
    setRotation(rotation);
    brightness(percent);
    fillScreen(0);
    setTextColor(1);
    setTextSize(2);
    setCursor(12, height()/2-8);
    print("ELMA-IoT");
    flush();
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
