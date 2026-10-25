#pragma once

#include <Arduino.h>
#include <atomic>
#include "lcd_wake_gesture.h"
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
#if APP_ROTARY_HMI
#include "rotary_hmi.h"
#endif

class DisplayManager {
  public:
    void begin(const OledSettings& settings);
#if APP_HAS_ONBOARD_PANEL
    void setPanelHandlers(PanelDashboard::Snapshot snapshot,PanelDashboard::Command command) { panelSnapshot_=snapshot;panelCommand_=command;if(panel_&&settings_.interfaceMode=="lvgl")panel_->releaseTextBuffer(); }
#endif
    void applySettings(const OledSettings& settings);
    void setBootMessage(const String& message);
    void showTemporaryCenterText(const String& message, unsigned long durationMs = 1500UL);
    bool available() const;
    bool interactivePanelActive() const {
#if APP_HAS_ONBOARD_PANEL && !APP_DISABLE_DISPLAY
#if APP_ROTARY_HMI
        return RoundHmi::available() && !dimmed_;
#else
        return dashboard_ && !dimmed_;
#endif
#else
        return false;
#endif
    }
    bool clearLogicText();
    void markActivity();
    void powerOff();
    void loop(const AppStateSnapshot& state);
    bool beginFirmwareUpdate();
    void firmwareUpdateProgress(uint8_t percent);
    void endFirmwareUpdate();
    void configureExternalDisplays(const SettingsBundle& settings);
#if APP_SUNTON_PANEL == 3
    void quietForDiagnostics(bool dark);
    uint8_t presentationState()const{return presentation_.load(std::memory_order_acquire);}
    bool webPresentationReady()const{return (presentationState()&3)==0;}
    void requestWebPresentation(){webPresentationRequested_.store(true);}
    void requestLcdPresentation(){lcdPresentationRequested_.store(true);}
#endif

  private:
    bool panelBootAnimationPending_=true;
    bool firmwareUpdate_=false;
    bool timeoutSleeping_=false;
    String observedDisplayContent_;
    void setTimeoutSleeping(bool sleeping);
#if APP_SUNTON_PANEL == 3
    std::atomic<uint8_t> presentation_{0}; // low bits: 0 web, 1 LCD, 2 LCD pending; bit 2 enabled.
    std::atomic<bool> webPresentationRequested_{false},lcdPresentationRequested_{false};
    bool restorePresentationPending_=false,lcdWakeRequested_=false;
    uint32_t presentationRestoreAt_=0,presentationWaitAt_=0;
    void restorePanelPresentation();
    uint32_t diagnosticQuietUntil_=0;
    bool screenOff_=false,wakeTouchPending_=false;
    LcdWakeGesture wakeGesture_;
    void setScreenOff(bool off);
#endif
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
