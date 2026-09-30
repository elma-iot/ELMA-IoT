#include "panel_display.h"
#if APP_HAS_ONBOARD_PANEL
#include "device_log.h"

bool PanelDisplay::begin(bool touchEnabled, uint8_t rotation, uint8_t percent) {
    if (!getBuffer()) return false;
    esp_panel::board::Board defaults;
    auto config = defaults.getConfig();
    if (!touchEnabled) config.touch.reset();
    board_.reset(new esp_panel::board::Board(config));
    if (!board_->init() || !board_->begin()) { board_.reset(); return false; }
    setRotation(rotation);
    brightness(percent);
    fillScreen(0);
    setTextColor(1);
    setTextSize(2);
    setCursor(12, height()/2-8);
    print("ELMA-IoT");
    flush();
    DebugLog.printf("[display] VIEWE 240x320 ready; PSRAM=%u; touch=%s\n",
                    ESP.getPsramSize(), touchEnabled ? "CHSC6540 polling" : "disabled");
    return true;
}

void PanelDisplay::brightness(uint8_t percent) {
    if (board_ && board_->getBacklight()) board_->getBacklight()->setBrightness(min<uint8_t>(percent, 100));
}

void PanelDisplay::flush() {
    if (!board_) return;
    // One bounded transfer at a time: never change the DMA source while in use.
    // RGB565 black/white is byte-order invariant.
    const uint8_t *pixels = getBuffer();
    for (int y = 0; y < 320; ++y) {
        for (int x = 0; x < 240; ++x) {
            uint8_t color = pixels[y * 30 + x / 8] & (0x80 >> (x & 7)) ? 255 : 0;
            line_[x * 2] = line_[x * 2 + 1] = color;
        }
        if (!board_->getLCD()->drawBitmap(0, y, 240, 1, line_, -1)) break;
    }
}

bool PanelDisplay::drawColor(int x,int y,int width,int height,uint8_t* colors) {
    return board_ && board_->getLCD()->drawBitmap(x,y,width,height,colors,-1);
}

bool PanelDisplay::readRawTouch(int16_t &x, int16_t &y) {
    if (!board_ || !board_->getTouch()) return false;
    esp_panel::drivers::TouchPoint point;
    if (board_->getTouch()->readPoints(&point, 1, 0) <= 0) return false;
    const int16_t px = point.x, py = point.y;
    if (px < 0 || px >= 240 || py < 0 || py >= 320) return false;
    x=px;y=py;return true;
}

bool PanelDisplay::readTouch(int16_t &x,int16_t &y) {
    int16_t px,py;if(!readRawTouch(px,py))return false;
    switch (getRotation()) {
        case 1: x = py; y = 239 - px; break;
        case 2: x = 239 - px; y = 319 - py; break;
        case 3: x = 319 - py; y = px; break;
        default: x = px; y = py;
    }
    return true;
}
#endif
