#include "display_manager.h"

#include <Wire.h>

namespace {
uint8_t charsForWidth(int16_t width, uint8_t textSize) {
    const int16_t charWidth = 6 * textSize;
    return width <= charWidth ? 1 : static_cast<uint8_t>(width / charWidth);
}
}

bool DisplayManager::isOledMode() const {
    return settings_.displayType != "wape";
}

uint8_t DisplayManager::rotationIndex() const {
    switch (settings_.rotation) {
        case 90:
            return 1;
        case 180:
            return 2;
        case 270:
            return 3;
        default:
            return 0;
    }
}

int16_t DisplayManager::topDividerY(int16_t displayHeight) const {
    return min<int16_t>(11, displayHeight / 4);
}

int16_t DisplayManager::bottomDividerY(int16_t displayHeight) const {
    return max<int16_t>(displayHeight - 12, displayHeight - 12);
}

void DisplayManager::begin(const OledSettings& settings) {
    applySettings(settings);
}

void DisplayManager::applySettings(const OledSettings& settings) {
    settings_ = settings;
    ssd1306_.reset();
    sh1106_.reset();
    dimmed_ = false;
    scrollOffset_ = 0;
    lastActivityAt_ = millis();
    if (!settings_.enabled || !isOledMode()) {
        return;
    }

    Wire.begin(settings_.sdaPin, settings_.sclPin);
    if (settings_.driver == "sh1106") {
        sh1106_.reset(new Adafruit_SH1106G(settings_.width, settings_.height, &Wire, settings_.resetPin));
        if (!sh1106_->begin(settings_.i2cAddress, true)) {
            sh1106_.reset();
            return;
        }
        sh1106_->setRotation(rotationIndex());
        sh1106_->clearDisplay();
        sh1106_->display();
    } else {
        ssd1306_.reset(new Adafruit_SSD1306(settings_.width, settings_.height, &Wire, settings_.resetPin));
        if (!ssd1306_->begin(SSD1306_SWITCHCAPVCC, settings_.i2cAddress)) {
            ssd1306_.reset();
            return;
        }
        ssd1306_->setRotation(rotationIndex());
        ssd1306_->clearDisplay();
        ssd1306_->display();
    }
    lastSignature_ = "";
    lastCenterText_ = "";
}

void DisplayManager::setBootMessage(const String& message) {
    bootMessage_ = message;
    lastSignature_ = "";
}

void DisplayManager::showTemporaryCenterText(const String& message, unsigned long durationMs) {
    temporaryCenterText_ = message;
    temporaryCenterTextUntilMs_ = millis() + durationMs;
    lastSignature_ = "";
    markActivity();
}

void DisplayManager::markActivity() {
    lastActivityAt_ = millis();
    setDimmed(false);
}

void DisplayManager::powerOff() {
    if (!isEnabled()) {
        return;
    }

    clearDisplay();
    flushDisplay();
    if (ssd1306_) {
        ssd1306_->ssd1306_command(SSD1306_DISPLAYOFF);
    }
    if (sh1106_) {
        sh1106_->oled_command(SH110X_DISPLAYOFF);
    }
}

bool DisplayManager::isEnabled() const {
    return settings_.enabled && isOledMode() && (ssd1306_ || sh1106_);
}

Adafruit_GFX* DisplayManager::gfx() {
    if (ssd1306_) {
        return ssd1306_.get();
    }
    if (sh1106_) {
        return sh1106_.get();
    }
    return nullptr;
}

void DisplayManager::clearDisplay() {
    if (ssd1306_) {
        ssd1306_->clearDisplay();
    }
    if (sh1106_) {
        sh1106_->clearDisplay();
    }
}

void DisplayManager::flushDisplay() {
    if (ssd1306_) {
        ssd1306_->display();
    }
    if (sh1106_) {
        sh1106_->display();
    }
}

void DisplayManager::setDimmed(bool dimmed) {
    if (dimmed_ == dimmed) {
        return;
    }
    dimmed_ = dimmed;
    if (ssd1306_) {
        ssd1306_->dim(dimmed);
    }
    if (sh1106_) {
        // Match the SSD1306 whole-panel dim policy; restore SH1106's
        // library initialization contrast when activity resumes.
        sh1106_->setContrast(dimmed ? 0 : 0xFF);
    }
}

String DisplayManager::centerTextForState(const AppStateSnapshot& state) const {
    if (!temporaryCenterText_.isEmpty() && static_cast<long>(millis() - temporaryCenterTextUntilMs_) < 0) {
        return temporaryCenterText_;
    }
    if (state.ota.busy) {
        return state.ota.phase.isEmpty() ? String("OTA updating") : state.ota.phase;
    }
    if (!state.system.lastError.isEmpty()) {
        return state.system.lastError;
    }
    if (state.playback.state == "playing") {
        return state.playback.title.length() ? state.playback.title : state.playback.url;
    }
    if (state.network.apMode && !state.network.wifiConnected) {
        return "AP setup mode";
    }
    if (!state.network.wifiConnected) {
        return "Connecting Wi-Fi";
    }
    return bootMessage_.isEmpty() ? String("Idle") : bootMessage_;
}

void DisplayManager::drawOtaProgress(Adafruit_GFX& display, const AppStateSnapshot& state) {
    const int16_t displayWidth = display.width();
    const int16_t displayHeight = display.height();
    const int16_t upperDivider = topDividerY(displayHeight);
    const int16_t lowerDivider = bottomDividerY(displayHeight);
    const int16_t centerTop = upperDivider + 6;
    const int16_t centerBottom = lowerDivider - 5;
    const uint8_t progress = state.ota.progressPercent;
    String label = state.ota.phase.isEmpty() ? String("Updating") : state.ota.phase;
    label += " ";
    label += progress;
    label += "%";

    display.setTextSize(1);
    const int16_t labelY = centerTop;
    drawWrappedLine(display, label, labelY, charsForWidth(displayWidth, 1), false);

    const int16_t barX = 8;
    const int16_t barHeight = 12;
    const int16_t barY = min<int16_t>(centerBottom - barHeight, labelY + 12);
    const int16_t barWidth = displayWidth - 16;
    display.drawRect(barX, barY, barWidth, barHeight, SSD1306_WHITE);
    const int16_t innerWidth = max<int16_t>(0, barWidth - 2);
    const int16_t fillWidth = (innerWidth * progress) / 100;
    if (fillWidth > 0) {
        display.fillRect(barX + 1, barY + 1, fillWidth, barHeight - 2, SSD1306_WHITE);
    }
}

void DisplayManager::drawWrappedLine(Adafruit_GFX& display, const String& text, int16_t y, uint8_t maxChars, bool scroll) {
    String toShow = text;
    if (toShow.length() <= maxChars || !scroll) {
        if (toShow.length() > maxChars) {
            toShow = toShow.substring(0, maxChars - 1) + "~";
        }
        display.setCursor(0, y);
        display.print(toShow);
        return;
    }

    const String padded = toShow + "   ";
    const uint16_t length = padded.length();
    const uint16_t start = scrollOffset_ % length;
    String window = padded.substring(start) + padded.substring(0, start);
    window = window.substring(0, maxChars);
    display.setCursor(0, y);
    display.print(window);
}

void DisplayManager::loop(const AppStateSnapshot& state) {
    if (!isEnabled()) {
        return;
    }
    const unsigned long now = millis();
    if (now - lastDrawAt_ < 300) {
        return;
    }
    lastDrawAt_ = now;

    if (state.playback.state == "playing" || state.network.wifiConnected || state.network.apMode) {
        lastActivityAt_ = now;
    }
    const bool shouldDim = settings_.dimTimeoutSeconds > 0 && (now - lastActivityAt_) > (settings_.dimTimeoutSeconds * 1000UL);
    setDimmed(shouldDim);

    const String top = state.network.wifiConnected ? state.network.ip : (state.network.apMode ? state.network.apSsid : String("Booting"));
    const String center = centerTextForState(state);
    const String bottom = String(state.network.wifiConnected ? "WiFi" : "AP") + " " +
                          String(state.battery.voltage, 2) + "V " +
                          (state.network.mqttConnected ? "MQTT" : "noMQTT");

    Adafruit_GFX* display = gfx();
    const bool scrolling = !state.ota.busy && center.length() > charsForWidth(display->width(), 2);
    const String signature = top + "|" + center + "|" + bottom + "|" +
                             String(state.ota.busy) + "|" + String(state.ota.progressPercent);
    if (signature == lastSignature_ && !scrolling) {
        return;
    }
    if (center != lastCenterText_) {
        scrollOffset_ = 0;
    } else if (scrolling) {
        scrollOffset_++;
    }
    lastSignature_ = signature;
    lastCenterText_ = center;

    const int16_t displayWidth = display->width();
    const int16_t displayHeight = display->height();
    const uint8_t topChars = charsForWidth(displayWidth, 1);
    const uint8_t centerChars = charsForWidth(displayWidth, 2);
    const uint8_t bottomChars = charsForWidth(displayWidth, 1);
    const int16_t upperDivider = topDividerY(displayHeight);
    const int16_t lowerDivider = bottomDividerY(displayHeight);
    const int16_t centerY = max<int16_t>(upperDivider + 12, (displayHeight / 2) - 8);
    clearDisplay();
    display->setTextWrap(false);
    display->setTextColor(SSD1306_WHITE);
    display->setTextSize(1);
    drawWrappedLine(*display, top, 2, topChars, false);
    display->drawLine(0, upperDivider, displayWidth - 1, upperDivider, SSD1306_WHITE);
    if (state.ota.busy) {
        drawOtaProgress(*display, state);
    } else {
        display->setTextSize(2);
        drawWrappedLine(*display, center, centerY, centerChars, true);
    }
    display->setTextSize(1);
    display->drawLine(0, lowerDivider, displayWidth - 1, lowerDivider, SSD1306_WHITE);
    drawWrappedLine(*display, bottom, displayHeight - 9, bottomChars, false);
    flushDisplay();
}

bool DisplayManager::available() const {return isEnabled();}
bool DisplayManager::clearLogicText() {
    if(!isEnabled())return false;
    temporaryCenterText_="";temporaryCenterTextUntilMs_=0;lastSignature_="";
    clearDisplay();flushDisplay();markActivity();return true;
}
