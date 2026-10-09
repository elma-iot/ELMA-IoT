#pragma once

#include <Arduino.h>
#if !APP_DISABLE_DISPLAY
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_SH110X.h>
#include <memory>
#endif

#include "app_state.h"
#include "settings_schema.h"
#include "panel_display.h"
#include "panel_dashboard.h"

class DisplayManager {
  public:
    void begin(const OledSettings& settings);
#if APP_HAS_ONBOARD_PANEL
    void setPanelHandlers(PanelDashboard::Snapshot snapshot,PanelDashboard::Command command) { panelSnapshot_=snapshot;panelCommand_=command; }
#endif
    void applySettings(const OledSettings& settings);
    void setBootMessage(const String& message);
    void showTemporaryCenterText(const String& message, unsigned long durationMs = 1500UL);
    bool available() const;
    bool interactivePanelActive() const {
#if APP_HAS_ONBOARD_PANEL && !APP_DISABLE_DISPLAY
        return dashboard_ && !dimmed_;
#else
        return false;
#endif
    }
    bool clearLogicText();
    void markActivity();
    void powerOff();
    void loop(const AppStateSnapshot& state);

  private:
#if !APP_DISABLE_DISPLAY
    OledSettings settings_;
    std::unique_ptr<Adafruit_SSD1306> ssd1306_;
    std::unique_ptr<Adafruit_SH1106G> sh1106_;
#if APP_HAS_ONBOARD_PANEL
    std::unique_ptr<PanelDisplay> panel_;
    std::unique_ptr<PanelDashboard> dashboard_;
    PanelDashboard::Snapshot panelSnapshot_;
    PanelDashboard::Command panelCommand_;
    unsigned long lastTouchAt_ = 0;
#endif
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
#endif
};
