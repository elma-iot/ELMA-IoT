#pragma once

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_SH110X.h>
#include <memory>

#include "app_state.h"
#include "settings_schema.h"

class DisplayManager {
  public:
    void begin(const OledSettings& settings);
    void applySettings(const OledSettings& settings);
    void setBootMessage(const String& message);
    void showTemporaryCenterText(const String& message, unsigned long durationMs = 1500UL);
    void markActivity();
    void powerOff();
    void loop(const AppStateSnapshot& state);

  private:
    OledSettings settings_;
    std::unique_ptr<Adafruit_SSD1306> ssd1306_;
    std::unique_ptr<Adafruit_SH1106G> sh1106_;
    String bootMessage_ = "Booting";
    unsigned long lastDrawAt_ = 0;
    unsigned long lastActivityAt_ = 0;
    unsigned long temporaryCenterTextUntilMs_ = 0;
    uint16_t scrollOffset_ = 0;
    String lastSignature_;
    String lastCenterText_;
    String temporaryCenterText_;
    bool dimmed_ = false;

    bool isEnabled() const;
    Adafruit_GFX* gfx();
    void clearDisplay();
    void flushDisplay();
    void setDimmed(bool dimmed);
    uint8_t rotationIndex() const;
    int16_t topDividerY(int16_t displayHeight) const;
    int16_t bottomDividerY(int16_t displayHeight) const;
    void drawWrappedLine(Adafruit_GFX& gfx, const String& text, int16_t y, uint8_t maxChars, bool scroll);
    void drawOtaProgress(Adafruit_GFX& gfx, const AppStateSnapshot& state);
    String centerTextForState(const AppStateSnapshot& state) const;
    bool isOledMode() const;
};
