#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>

#ifndef APP_DISABLE_WEB_UI
#include <ESPAsyncWebServer.h>
#include <FS.h>
#endif

#include "app_state.h"
#include "ota_manager.h"
#include "settings_manager.h"
#include "storage_backend.h"
#include "interface_security.h"
#include "wifi_manager.h"

class WebServerManager {
  public:
    using SettingsGetter = std::function<SettingsBundle(void)>;
    using SettingsVisitor = std::function<void(const SettingsBundle&)>;
    using SettingsReader = std::function<void(const SettingsVisitor&)>;
    void setSettingsReader(SettingsReader reader) { settingsReader_=reader; }
    using SettingsSaver = std::function<bool(JsonVariantConst, String&)>;
    using PlayHandler = std::function<bool(const String&, const String&, const String&, String&)>;
    using AudioSourceHandler = std::function<bool(JsonVariantConst, String&)>;
    void setAudioSourceHandler(AudioSourceHandler handler) { audioSourceHandler_ = handler; }
    using LogicsHandler = std::function<bool(JsonVariantConst, JsonDocument&, String&)>;
    using LogicsGetter = std::function<void(JsonDocument&, bool)>;
    void setLogicsHandlers(LogicsGetter getter, LogicsHandler handler) { logicsGetter_=getter;logicsHandler_=handler; }
    using StopHandler = std::function<void(void)>;
    using VolumeHandler = std::function<void(uint8_t)>;
    using EqualizerHandler = std::function<void(const String&, int8_t, int8_t, int8_t)>;
    using SeekHandler = std::function<bool(uint32_t)>;
    using SimpleHandler = std::function<void(void)>;
    using OtaHandler = std::function<bool(bool)>;
    using MqttHandler = std::function<bool(const String&, String&)>;
    using MotorRunHandler = std::function<bool(uint8_t, bool, uint32_t, int8_t, String&)>;
    using MotorConfigSaver = std::function<bool(JsonVariantConst, String&)>;
    using StatusAppender = std::function<void(JsonObject)>;
    void setLedStatusAppender(StatusAppender appender) { ledStatusAppender_=appender; }

#if APP_SUNTON_PANEL == 3
    void setPresentationHandlers(std::function<uint8_t()> state,SimpleHandler web){presentationState_=state;presentationWeb_=web;}
#ifndef APP_DISABLE_WEB_UI
    void pauseHttpForClockTest();
#endif
#endif
    WebServerManager();
    void begin(
        AppState& appState,
        WiFiManager& wifiManager,
        SettingsManager& settingsManager,
        OtaManager& otaManager,
        SettingsGetter settingsGetter,
        SettingsSaver settingsSaver,
        PlayHandler playHandler,
        StopHandler stopHandler,
        VolumeHandler volumeHandler,
        EqualizerHandler equalizerHandler,
        SeekHandler seekHandler,
        OtaHandler otaHandler,
        MqttHandler mqttHandler,
        MotorRunHandler motorRunHandler,
        MotorConfigSaver motorConfigSaver,
        StatusAppender motorStatusAppender,
        SimpleHandler displayTriggerHandler,
        SimpleHandler serverShutdownHandler,
        SimpleHandler rebootHandler,
        SimpleHandler factoryResetHandler);
    void securityTick();
    void setWebUiLocked(bool locked);
    bool webUiLocked() const;
#if APP_HAS_ONBOARD_PANEL && !defined(APP_DISABLE_WEB_UI)
    void panelSnapshot(const String& page,JsonObject root);
    void releasePanelCaches();
    bool panelCommand(const String& action,JsonVariantConst args,String& error);
#endif

  private:
#ifndef APP_DISABLE_WEB_UI
    bool gateFirmwareTransfer(AsyncWebServerRequest* request);
#endif
#if APP_SUNTON_PANEL == 3
    std::function<uint8_t()> presentationState_;SimpleHandler presentationWeb_;
#ifndef APP_DISABLE_WEB_UI
    uint32_t clockTestResumeAt_=0;
    void registerPresentationRoutes();
    bool gatePresentation(AsyncWebServerRequest* request);
    void servePresentationPage(AsyncWebServerRequest* request);
#endif
#endif
#if APP_HAS_ONBOARD_PANEL && !defined(APP_DISABLE_WEB_UI)
    void panelSnapshotWithSettings(const String& page,JsonObject root,const SettingsBundle& settings);
    String panelStoragePath_="/", panelStorageListing_, panelStoragePage_;
    int panelStorageOffset_=0;
    bool panelStorageDirty_=true, panelStorageMounted_=false;
    void* panelBrowseJob_=nullptr;
#endif
    SettingsReader settingsReader_;
    StatusAppender ledStatusAppender_;
    LogicsGetter logicsGetter_;
    LogicsHandler logicsHandler_;
#ifndef APP_DISABLE_WEB_UI
    AsyncWebServer server_;
    bool listenerStartPending_=false;uint32_t listenerRetryAt_=0;
    AppState* appState_ = nullptr;
    WiFiManager* wifiManager_ = nullptr;
    SettingsManager* settingsManager_ = nullptr;
    OtaManager* otaManager_ = nullptr;
    SettingsGetter settingsGetter_;
    SettingsSaver settingsSaver_;
    PlayHandler playHandler_;
    AudioSourceHandler audioSourceHandler_;
    StopHandler stopHandler_;
    VolumeHandler volumeHandler_;
    EqualizerHandler equalizerHandler_;
    SeekHandler seekHandler_;
    OtaHandler otaHandler_;
    MqttHandler mqttHandler_;
    MotorRunHandler motorRunHandler_;
    MotorConfigSaver motorConfigSaver_;
    StatusAppender motorStatusAppender_;
    SimpleHandler displayTriggerHandler_;
    SimpleHandler serverShutdownHandler_;
    SimpleHandler rebootHandler_;
    SimpleHandler factoryResetHandler_;
#if APP_ROTARY_HMI || defined(APP_SPK_BOARD)
    InterfaceSecurity& security_=InterfaceSecurity::device();
#else
    mutable InterfaceSecurity security_;
#endif
    void registerSecurityRoutes();
    void releaseStorageUploadLease();
    AsyncWebServerRequest* storageUploadOwner_ = nullptr;
    bool storageUploadLeased_ = false, storageUploadCreated_ = false, storageUploadFinished_ = false;
    File storageUploadFile_;
    StorageTarget storageUploadTarget_ = StorageTarget::Flash;
    String storageUploadPath_;
    String storageUploadTempPath_,storageUploadExpectedCrc_;
    size_t storageUploadExpectedSize_=0;
    uint32_t storageUploadCrc_=0xffffffff;
    String storageUploadError_;
    size_t storageUploadBytesWritten_ = 0;
    size_t storageUploadLimitBytes_ = 0;
    uint8_t* storageTransferBuffer_ = nullptr;
    size_t storageTransferBufferCapacity_ = 0;

    bool ensureAuthorized(AsyncWebServerRequest* request);
    bool rejectIfWebUiLocked(AsyncWebServerRequest* request);
    bool ensureStorageTransferBuffer(size_t minimumSize);
    bool redirectCaptivePortalIfNeeded(AsyncWebServerRequest* request);
    void sendJson(AsyncWebServerRequest* request, const JsonDocument& doc, int statusCode = 200);
    void registerApiRoutes();
    void registerFirmwareRoutes();
    void registerWebRoutes();
    bool serveStorageWebAsset(AsyncWebServerRequest* request, const String& path, bool fallback = false);
  #endif
};
