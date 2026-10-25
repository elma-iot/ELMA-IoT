#include "display_manager.h"
#if !APP_DISABLE_DISPLAY

#include <Wire.h>
#include "shared_i2c.h"
#include "storage_backend.h"
#include "device_log.h"
#include "update_screen.h"
#include "update_artwork.h"
#include "display_idle_policy.h"
#include "external_spi_displays.h"
#if APP_HAS_ONBOARD_PANEL
#include "rotary_hmi.h"
#endif

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
    panelBootAnimationPending_=settings.enabled;
    applySettings(settings);
}

void DisplayManager::applySettings(const OledSettings& settings) {
    SharedI2c::Guard bus; if(!bus)return;
    setTimeoutSleeping(false);
#if APP_SUNTON_PANEL == 3
    // Changing brightness/timers while the web owns the UI must not allocate LCD widgets.
    if(screenOff_ && panel_ && settings.enabled && settings_.enabled){
        settings_=settings;panel_->setRotation(rotationIndex());return;
    }
    restorePresentationPending_=false;lcdWakeRequested_=false;
    screenOff_=false;wakeTouchPending_=false;
    presentation_.store(settings.enabled?5:0,std::memory_order_release);
#endif
#if APP_HAS_ONBOARD_PANEL
    const bool keepPanel=panel_&&settings.enabled&&settings.displayType=="panel"&&settings.touchEnabled==settings_.touchEnabled;
#endif
#if APP_HAS_ONBOARD_PANEL
    // Backlight and idle settings do not invalidate LVGL, its widgets or DMA buffers.
    if(keepPanel && (dashboard_ || RoundHmi::available()) && settings.circularMenu==settings_.circularMenu && settings.interfaceMode==settings_.interfaceMode && settings.rotation==settings_.rotation){
        settings_=settings;dimmed_=false;lastActivityAt_=millis();
        panel_->brightness(settings_.brightness);return;
    }
#endif
    settings_ = settings;
    ssd1306_.reset();
    sh1106_.reset();
#if APP_HAS_ONBOARD_PANEL
    RoundHmi::end();
    dashboard_.reset();
    if(!keepPanel){
        if(panel_)panel_->brightness(0);
        panel_.reset();
    }
#endif
    dimmed_ = false;
    scrollOffset_ = 0;
    lastActivityAt_ = millis();
    if (!settings_.enabled || !isOledMode() || settings_.displayType=="spi") {
#if APP_SUNTON_PANEL == 3
        lv_mem_buf_free_all();presentation_.store(0,std::memory_order_release);
#endif
        return;
    }

#if APP_HAS_ONBOARD_PANEL
    if (settings_.displayType == "panel") {
        if(!panel_)panel_.reset(new PanelDisplay());
        if (!keepPanel && !panel_->begin(settings_.touchEnabled, rotationIndex(), settings_.brightness)) {
            panel_.reset();
            DebugLog.println("[display] VIEWE initialization failed");
        }
        if(panel_){panel_->setRotation(rotationIndex());panel_->brightness(settings_.brightness);if(settings_.interfaceMode!="lvgl")panel_->ensureTextBuffer();}
        // Keep the early boot splash small. Build LVGL only after the network
        // service has allocated its task and the real snapshot provider exists.
        if(panel_ && settings_.interfaceMode=="lvgl" && panelSnapshot_) {
            panel_->releaseTextBuffer();
#if APP_ROTARY_HMI
            if(!RoundHmi::begin(*panel_,rotationIndex(),settings_.circularMenu)){panel_->ensureTextBuffer();DebugLog.println("[display] Circular menu initialization failed");}
#else
            dashboard_.reset(new PanelDashboard(*panel_));
            if(!dashboard_->begin(rotationIndex(),panelBootAnimationPending_)) { dashboard_.reset(); panel_->ensureTextBuffer(); DebugLog.println("[display] LVGL allocation failed; using text interface"); }
            else panelBootAnimationPending_=false;
#endif
        }
#if APP_SUNTON_PANEL == 3
        presentation_.store(panel_?5:4,std::memory_order_release);
#endif
        lastSignature_ = "";
        lastCenterText_ = "";
        return;
    }
#endif
    if(!SharedI2c::begin(settings_.sdaPin, settings_.sclPin))return;
    if (settings_.driver == "sh1106") {
        // A full 128x64 frame at 100 kHz blocks the decoder for ~98 ms.
        // Fast-mode transfer ends by restoring 100 kHz for the shared BNO055.
#if defined(APP_SPK_BOARD)
        constexpr uint32_t oledClock=400000;
#else
        constexpr uint32_t oledClock=100000;
#endif
        sh1106_.reset(new Adafruit_SH1106G(settings_.width, settings_.height, &SharedI2c::wire(), settings_.resetPin, oledClock, 100000));
        if (!sh1106_->begin(settings_.i2cAddress, true)) {
            sh1106_.reset();
            return;
        }
        sh1106_->setRotation(rotationIndex());
        sh1106_->clearDisplay();
        sh1106_->display();
    } else {
#if defined(APP_SPK_BOARD)
        constexpr uint32_t oledClock=400000;
#else
        constexpr uint32_t oledClock=100000;
#endif
        ssd1306_.reset(new Adafruit_SSD1306(settings_.width, settings_.height, &SharedI2c::wire(), settings_.resetPin, oledClock, 100000));
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
    ExternalDisplays::temporaryText(message,durationMs);
#if APP_ROTARY_HMI
    if(RoundHmi::available())RoundHmi::temporaryText(message,durationMs);
#endif
    temporaryCenterText_ = message;
    temporaryCenterTextUntilMs_ = millis() + durationMs;
    lastSignature_ = "";
    markActivity();
}

void DisplayManager::markActivity() {
    lastActivityAt_ = millis();
    setTimeoutSleeping(false);
#if APP_SUNTON_PANEL == 3
    setScreenOff(false);
#endif
    setDimmed(false);
}

void DisplayManager::powerOff() {
    ExternalDisplays::powerOff();
    SharedI2c::Guard bus; if(!bus)return;
#if APP_HAS_ONBOARD_PANEL
    if (panel_) panel_->brightness(0);
#endif
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
#if APP_HAS_ONBOARD_PANEL
    if (panel_) return settings_.enabled;
#endif
    return settings_.enabled && isOledMode() && (ssd1306_ || sh1106_);
}

Adafruit_GFX* DisplayManager::gfx() {
#if APP_HAS_ONBOARD_PANEL
    if (panel_) return panel_.get();
#endif
    if (ssd1306_) {
        return ssd1306_.get();
    }
    if (sh1106_) {
        return sh1106_.get();
    }
    return nullptr;
}

void DisplayManager::clearDisplay() {
#if APP_HAS_ONBOARD_PANEL
    if (panel_) panel_->fillScreen(0);
#endif
    if (ssd1306_) {
        ssd1306_->clearDisplay();
    }
    if (sh1106_) {
        sh1106_->clearDisplay();
    }
}

void DisplayManager::flushDisplay() {
#if APP_HAS_ONBOARD_PANEL
    if (panel_) panel_->flush();
#endif
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
#if APP_SUNTON_PANEL == 3
    DebugLog.printf("[display] dim=%u backlight_pct=%u\n",unsigned(dimmed),unsigned(dimmed?settings_.brightness/2:settings_.brightness));
#endif
#if APP_HAS_ONBOARD_PANEL
    if (panel_) panel_->brightness(dimmed ?
#if APP_SUNTON_PANEL == 3
        settings_.brightness/2
#else
        0
#endif
        : settings_.brightness);
#endif
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

#if APP_SUNTON_PANEL == 3
void DisplayManager::setScreenOff(bool off) {
    if(screenOff_==off && !restorePresentationPending_)return;
    if(off){
        restorePresentationPending_=false;lcdWakeRequested_=false;wakeGesture_.reset();
        // Keep the panel's small I2C touch driver alive, not its LVGL tree/pixels.
        dashboard_.reset();
        if(panel_){panel_->releaseTextBuffer();panel_->sleep(true);panel_->brightness(0);}
        lv_mem_buf_free_all();screenOff_=true;
        presentation_.store(settings_.enabled?4:0,std::memory_order_release);
        DebugLog.printf("[presentation] web ready; LCD widgets released heap8=%u\n",heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    }else{
        screenOff_=false;restorePresentationPending_=true;
        presentationRestoreAt_=millis()+350;presentationWaitAt_=millis();
        presentation_.store(6,std::memory_order_release); // Gate heavy HTTP before allocation.
        DebugLog.println("[presentation] LCD requested; draining web responses");
    }
}
void DisplayManager::restorePanelPresentation(){
    if(!panel_||!settings_.enabled){restorePresentationPending_=false;presentation_.store(0);return;}
    if(int32_t(millis()-presentationRestoreAt_)<0)return;
    const size_t available=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    // LVGL can draw a single row; fragmented web buffers need not prevent a
    // return to the LCD. Text mode still needs its contiguous monochrome page.
    const size_t contiguous=settings_.interfaceMode=="lvgl"?1536:((kPanelWidth+7)/8*kPanelHeight);
    if(available<28672 || heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<contiguous){
        if(millis()-presentationWaitAt_>5000){setScreenOff(true);DebugLog.println("[presentation] LCD deferred: memory busy; audio continues");}
        return;
    }
    panel_->sleep(false);panel_->setRotation(rotationIndex());panel_->brightness(settings_.brightness);
    if(settings_.interfaceMode=="lvgl"){
        dashboard_.reset(new(std::nothrow) PanelDashboard(*panel_));
        if(!dashboard_||!dashboard_->begin(rotationIndex(),false)){
            dashboard_.reset();setScreenOff(true);return;
        }
    }else if(!panel_->ensureTextBuffer()){setScreenOff(true);return;}
    restorePresentationPending_=false;lastActivityAt_=millis();dimmed_=false;
    presentation_.store(5,std::memory_order_release);
    DebugLog.printf("[presentation] LCD ready; full web paused heap8=%u\n",heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
}
void DisplayManager::quietForDiagnostics(bool dark) {
    diagnosticQuietUntil_=millis()+30000;
    if(panel_&&dark)panel_->brightness(0);
    DebugLog.printf("[display-test] Paused LCD updates for 30 seconds; backlight=%s\n",dark?"off":"unchanged");
}
#endif
void DisplayManager::loop(const AppStateSnapshot& state) {
    if(firmwareUpdate_)return; // The dedicated update renderer owns the panel.
    ExternalDisplays::loop(state);
#if APP_SUNTON_PANEL == 3
    if(diagnosticQuietUntil_){
        if(int32_t(diagnosticQuietUntil_-millis())>0)return;
        diagnosticQuietUntil_=0;markActivity();if(panel_)panel_->brightness(settings_.brightness);
        DebugLog.println("[display-test] LCD updates resumed");
    }
#endif
    SharedI2c::Guard bus(0); if(!bus)return;
    if(settings_.displayType=="oled"){
        const String content=centerTextForState(state)+"|"+state.network.ip+"|"+String(state.battery.voltage,2)+"|"+String(state.network.mqttConnected)+"|"+state.system.lastError;
        if(content!=observedDisplayContent_){observedDisplayContent_=content;markActivity();}
        if(displayTimeoutDue(millis(),lastActivityAt_,settings_.timeoutEnabled,settings_.screenTimeoutSeconds))setTimeoutSleeping(true);
        if(timeoutSleeping_)return;
    }
#if APP_SUNTON_PANEL == 3
    if(webPresentationRequested_.exchange(false))setScreenOff(true);
    if(lcdPresentationRequested_.exchange(false))lcdWakeRequested_=true;
    if(restorePresentationPending_){restorePanelPresentation();return;}
    if(panel_ && (screenOff_||wakeTouchPending_||lcdWakeRequested_)){
        if(millis()-lastTouchAt_<30)return;
        lastTouchAt_=millis();int16_t x=0,y=0;const bool touched=panel_->readRawTouch(x,y);
        if(screenOff_ && wakeGesture_.update(touched,x,y,millis())){lcdWakeRequested_=true;wakeTouchPending_=false;}
        else if(wakeTouchPending_ && !touched)wakeTouchPending_=false;
        // Do not interrupt an OTA or foreground SD transfer to rebuild widgets.
        if(lcdWakeRequested_ && !wakeTouchPending_ && !state.ota.busy &&
           !storageWriteBlocked(StorageTarget::Sd) && !storageForegroundPending(StorageTarget::Sd)){
            lcdWakeRequested_=false;markActivity();
        }
        return; // A wake gesture must not activate the covered control.
    }
    if(panel_ && displayTimeoutDue(millis(),lastActivityAt_,settings_.timeoutEnabled,settings_.screenTimeoutSeconds)){
        setScreenOff(true);return;
    }
#endif
#if APP_HAS_ONBOARD_PANEL
    if(RoundHmi::available()) {
        RoundHmi::systemState(state,panelSnapshot_,panelCommand_);
        RoundHmi::networkStatus(state.network.wifiConnected,state.network.mqttConnected);RoundHmi::loop();if(RoundHmi::takeActivity())markActivity();
        setTimeoutSleeping(displayTimeoutDue(millis(),lastActivityAt_,settings_.timeoutEnabled,settings_.screenTimeoutSeconds));
        setDimmed(settings_.dimTimeoutSeconds>0 && millis()-lastActivityAt_>settings_.dimTimeoutSeconds*1000UL);return;
    }
    if(dashboard_) {
        const bool temporary=temporaryCenterTextUntilMs_ && static_cast<int32_t>(temporaryCenterTextUntilMs_-millis())>0;
        dashboard_->loop(state,panelSnapshot_,panelCommand_,temporary?temporaryCenterText_:String(""));
        if(dashboard_->touched())markActivity();
        setTimeoutSleeping(displayTimeoutDue(millis(),lastActivityAt_,settings_.timeoutEnabled,settings_.screenTimeoutSeconds));
        setDimmed(settings_.dimTimeoutSeconds>0 && millis()-lastActivityAt_>settings_.dimTimeoutSeconds*1000UL);
        return;
    }
    if (panel_ && millis() - lastTouchAt_ >= 30) {
        lastTouchAt_ = millis();
        int16_t x, y;
        if (panel_->readTouch(x, y)) {
            markActivity();
            DebugLog.printf("[touch] x=%d y=%d rotation=%u\n", x, y, settings_.rotation);
        }
    }
#endif
    if (!isEnabled()) {
        return;
    }
    const unsigned long now = millis();
    if (now - lastDrawAt_ < 300) {
        return;
    }
    lastDrawAt_ = now;

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

bool DisplayManager::available() const {return isEnabled()||ExternalDisplays::available();}
bool DisplayManager::beginFirmwareUpdate(){
    if(firmwareUpdate_)return true;
    firmwareUpdate_=true;
    setTimeoutSleeping(false);
    if(!ExternalDisplays::prepareUpdate())return false;
#if APP_HAS_ONBOARD_PANEL
    RoundHmi::prepareForUpdate();dashboard_.reset();lv_mem_buf_free_all();
    if(panel_){
        panel_->sleep(false);panel_->brightness(settings_.brightness);
        if(!panel_->beginUpdate())return false;
    }
#endif
    lastSignature_="";lastDrawAt_=0;
    return true;
}
void DisplayManager::firmwareUpdateProgress(uint8_t percent){
    if(!firmwareUpdate_)return;
    ExternalDisplays::progress(percent);
#if APP_HAS_ONBOARD_PANEL
    if(panel_){panel_->updateProgress(percent,millis());return;}
#endif
    if(!isEnabled() || millis()-lastDrawAt_<200)return;
    lastDrawAt_=millis();auto* display=gfx();if(!display)return;
    UpdateScreen::artwork(*display,percent,millis(),false);flushDisplay();
}
void DisplayManager::endFirmwareUpdate(){
    if(!firmwareUpdate_)return;
    firmwareUpdate_=false;
    ExternalDisplays::endUpdate();
#if APP_HAS_ONBOARD_PANEL
    // A cancelled/failed update returns to the saved interface. RGB scanout was
    // replaced, so recreate the driver before rebuilding the normal widgets.
    if(panel_){panel_->endUpdate();panel_->brightness(0);panel_.reset();}
#endif
    applySettings(settings_);
}
void DisplayManager::configureExternalDisplays(const SettingsBundle& settings){ExternalDisplays::configure(settings);}
void DisplayManager::setTimeoutSleeping(bool sleeping){
    if(timeoutSleeping_==sleeping)return;timeoutSleeping_=sleeping;
    if(ssd1306_)ssd1306_->ssd1306_command(sleeping?SSD1306_DISPLAYOFF:SSD1306_DISPLAYON);
    if(sh1106_)sh1106_->oled_command(sleeping?SH110X_DISPLAYOFF:SH110X_DISPLAYON);
#if APP_HAS_ONBOARD_PANEL
    if(panel_){panel_->sleep(sleeping);panel_->brightness(sleeping?0:settings_.brightness);}
#endif
    if(!sleeping){lastSignature_="";lastDrawAt_=0;}
}
bool DisplayManager::clearLogicText() {
    ExternalDisplays::temporaryText("",0);
    if(!isEnabled())return ExternalDisplays::available();
#if APP_ROTARY_HMI
    if(RoundHmi::available()){RoundHmi::temporaryText("",0);markActivity();return true;}
#endif
    temporaryCenterText_="";temporaryCenterTextUntilMs_=0;lastSignature_="";
    markActivity();
#if APP_HAS_ONBOARD_PANEL
    if(dashboard_)return true;
#endif
    clearDisplay();flushDisplay();return true;
}

#else
void DisplayManager::begin(const OledSettings&) {}
void DisplayManager::applySettings(const OledSettings&) {}
void DisplayManager::setBootMessage(const String&) {}
void DisplayManager::showTemporaryCenterText(const String&, unsigned long) {}
bool DisplayManager::available() const { return false; }
bool DisplayManager::clearLogicText() { return false; }
void DisplayManager::markActivity() {}
void DisplayManager::powerOff() {}
void DisplayManager::loop(const AppStateSnapshot&) {}
bool DisplayManager::beginFirmwareUpdate(){return true;}
void DisplayManager::firmwareUpdateProgress(uint8_t){}
void DisplayManager::endFirmwareUpdate(){}
void DisplayManager::configureExternalDisplays(const SettingsBundle&){}
void DisplayManager::setTimeoutSleeping(bool){}
#endif
