#include "buzzer_melody.h"
#include "logic_device.h"
#include "logic_audio_dispatch.h"
#include "generated_project_defaults.h"
#include "device_log.h"
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <soc/soc_caps.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_sleep.h>
#include <esp_system.h>

#include "default_config.h"
#include "motor_runtime_config.h"
#include "logic_storage.h"
#include "storage_backend.h"

namespace {
void waitForSerialConsole(unsigned long timeoutMs = 1500) {
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
    const unsigned long startedAt = millis();
    while (!Serial && (millis() - startedAt) < timeoutMs) {
        delay(10);
    }
#else
    (void)timeoutMs;
#endif
}

void scheduleReboot(uint32_t delayMs);

uint8_t activeStatusLedPin = DefaultConfig::STATUS_LED_PIN;
uint8_t activeStatusLedGreenPin = DefaultConfig::STATUS_LED_PIN;
uint8_t activeStatusLedBluePin = DefaultConfig::STATUS_LED_PIN;
bool activeStatusLedIsNeoPixel = DefaultConfig::STATUS_LED_IS_NEOPIXEL;
bool activeStatusLedIsRgb = false;
bool statusLedInitialized = false;

constexpr uint8_t kStatusLedBrightness = 24;
constexpr unsigned long kApStatusLedBlinkIntervalMs = 1000UL;

Adafruit_NeoPixel statusLedPixel(1, DefaultConfig::STATUS_LED_PIN, NEO_GRB + NEO_KHZ800);
bool statusLedColorKnown = false;
uint8_t lastStatusLedRed = 0;
uint8_t lastStatusLedGreen = 0;
uint8_t lastStatusLedBlue = 0;

void initializeStatusLed() {
    if (activeStatusLedIsNeoPixel) {
        statusLedPixel.setPin(activeStatusLedPin);
        statusLedPixel.begin();
        statusLedPixel.clear();
        statusLedPixel.show();
    } else if (activeStatusLedIsRgb) {
        pinMode(activeStatusLedPin, OUTPUT);
        pinMode(activeStatusLedGreenPin, OUTPUT);
        pinMode(activeStatusLedBluePin, OUTPUT);
        analogWrite(activeStatusLedPin, 0);analogWrite(activeStatusLedGreenPin, 0);analogWrite(activeStatusLedBluePin, 0);
    } else {
        pinMode(activeStatusLedPin, OUTPUT);
        digitalWrite(activeStatusLedPin, LOW);
    }
    statusLedColorKnown = true;
    lastStatusLedRed = 0;
    lastStatusLedGreen = 0;
    lastStatusLedBlue = 0;
    statusLedInitialized = true;
}

void writeStatusLedColor(uint8_t red, uint8_t green, uint8_t blue) {
    if (!statusLedInitialized) {
        return;
    }
    if (statusLedColorKnown && red == lastStatusLedRed && green == lastStatusLedGreen && blue == lastStatusLedBlue) {
        return;
    }
    if (activeStatusLedIsNeoPixel) {
        statusLedPixel.setPixelColor(0, statusLedPixel.Color(red, green, blue));
        statusLedPixel.show();
    } else if (activeStatusLedIsRgb) {
        analogWrite(activeStatusLedPin, red);analogWrite(activeStatusLedGreenPin, green);analogWrite(activeStatusLedBluePin, blue);
    } else {
        digitalWrite(activeStatusLedPin, (red != 0 || green != 0 || blue != 0) ? HIGH : LOW);
    }
    statusLedColorKnown = true;
    lastStatusLedRed = red;
    lastStatusLedGreen = green;
    lastStatusLedBlue = blue;
}

void writeStatusLed(bool on) {
    writeStatusLedColor(0, on ? kStatusLedBrightness : 0, 0);
}

bool statusLedTypeIsNeoPixel(const String& type) {
    return type.equalsIgnoreCase("neopixel");
}

void applyStatusLedConfig(uint8_t pin, uint8_t greenPin, uint8_t bluePin, const String& type) {
    const bool useNeoPixel = statusLedTypeIsNeoPixel(type);
    const bool useRgb = type.equalsIgnoreCase("rgb");
    if (statusLedInitialized && pin == activeStatusLedPin && greenPin == activeStatusLedGreenPin && bluePin == activeStatusLedBluePin && useNeoPixel == activeStatusLedIsNeoPixel && useRgb == activeStatusLedIsRgb) {
        return;
    }

    writeStatusLed(false);
    if (statusLedInitialized) {
        pinMode(activeStatusLedPin, INPUT);
        if(activeStatusLedIsRgb){pinMode(activeStatusLedGreenPin,INPUT);pinMode(activeStatusLedBluePin,INPUT);}
    }
    activeStatusLedPin = pin;
    activeStatusLedGreenPin = greenPin;
    activeStatusLedBluePin = bluePin;
    activeStatusLedIsNeoPixel = useNeoPixel;
    activeStatusLedIsRgb = useRgb;
    statusLedColorKnown = false;
    initializeStatusLed();
}

bool apStatusLedBluePhase(unsigned long now) {
    return ((now / kApStatusLedBlinkIntervalMs) % 2UL) != 0;
}

void updateStatusLedForNetwork(bool wifiConnected, bool apMode, unsigned long now = millis()) {
    if (wifiConnected) {
        writeStatusLedColor(0, 0, kStatusLedBrightness);
        return;
    }

    if (apMode) {
        writeStatusLedColor(0, 0, apStatusLedBluePhase(now) ? kStatusLedBrightness : 0);
        return;
    }

    writeStatusLed(((now / 300UL) % 2) != 0);
}

}

#ifdef SAFE_BOOT_DIAGNOSTIC
#include "version.h"

namespace {
const char* resetReasonToString(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_UNKNOWN:
            return "unknown";
        case ESP_RST_POWERON:
            return "poweron";
        case ESP_RST_EXT:
            return "external";
        case ESP_RST_SW:
            return "software";
        case ESP_RST_PANIC:
            return "panic";
        case ESP_RST_INT_WDT:
            return "interrupt watchdog";
        case ESP_RST_TASK_WDT:
            return "task watchdog";
        case ESP_RST_WDT:
            return "other watchdog";
        case ESP_RST_DEEPSLEEP:
            return "deepsleep";
        case ESP_RST_BROWNOUT:
            return "brownout";
        case ESP_RST_SDIO:
            return "sdio";
        default:
            return "unhandled";
    }
}

unsigned long lastHeartbeatAt = 0;
}

void setup() {
    Serial.begin(115200);
    DebugLog.begin();
    waitForSerialConsole();
    delay(300);
    initializeStatusLed();
    writeStatusLed(false);

    DebugLog.printf("\n[safe-boot] app=%s version=%s built=%s\n", APP_NAME, APP_VERSION, APP_BUILD_DATE);
    DebugLog.printf("[safe-boot] reset reason=%s (%d)\n", resetReasonToString(esp_reset_reason()), static_cast<int>(esp_reset_reason()));
    DebugLog.printf("[safe-boot] free heap=%u\n", ESP.getFreeHeap());
    DebugLog.println("[safe-boot] minimal firmware is running");
    Serial.flush();
    DebugLog.service(true);
}

void loop() {
    static bool heartbeatLedOn = false;
    DebugLog.service();
    const unsigned long now = millis();
    if (now - lastHeartbeatAt >= 1000UL) {
        lastHeartbeatAt = now;
        heartbeatLedOn = !heartbeatLedOn;
        writeStatusLed(heartbeatLedOn);
        DebugLog.printf("[safe-boot] uptime=%lu free_heap=%u\n", now, ESP.getFreeHeap());
        Serial.flush();
    }
    delay(10);
}

#else
#include "app_state.h"
#include "battery_monitor.h"
#include <cmath>
#include "display_manager.h"
#include "mqtt_manager.h"
#include "motor_control.h"
#include "ota_manager.h"
#include "playback_text.h"
#include "psram_allocator.h"
#include "settings_manager.h"
#include "sound_effects.h"
#include "system_metrics.h"
#include "version.h"
#include "web_server.h"
#include "wifi_manager.h"

#ifdef APP_DISABLE_AUDIO
class AudioPlayerStub {
  public:
    void begin(uint8_t, uint8_t, uint8_t, uint8_t initialVolumePercent, bool, AppState& appState) {
        appState_ = &appState;
        volume_ = initialVolumePercent;
        state_ = "idle";
        type_ = "idle";
        title_ = "Audio disabled";
        url_ = "";
        source_ = "disabled";
        publish();
    }

    void loop() {}

    bool play(const String&, const String&, const String&, const String&) {
        return false;
    }

    bool playStorageFile(StorageTarget, const String&, const String&, const String&, const String&) {
        return false;
    }
    bool playStorageOverlay(StorageTarget, const String&, uint8_t = 35, uint8_t = 100) { return false; }
    bool overlayActive() const { return false; }
    bool consumeOverlayFinished() { return false; }
    bool consumePlaybackCompletion(String&) { return false; }
    bool reconfigureOutputPins(uint8_t, uint8_t, uint8_t) { return false; }
    bool disableOutput() { return true; }

    void releaseResourcesForUpdate() { stop(); }
    void stop() {
    state_ = "idle";
    type_ = "idle";
    title_ = "Audio disabled";
    url_ = "";
    source_ = "disabled";
    publish();
    }

    void setVolumePercent(uint8_t volumePercent) {
        volume_ = volumePercent;
        publish();
    }

    void setEqualizer(const String&, int8_t, int8_t, int8_t) {}

    uint8_t volumePercent() const { return volume_; }
    String currentState() const { return state_; }
    uint32_t currentPositionSeconds() const { return 0; }
    uint32_t durationSeconds() const { return 0; }
    bool seekStorageFile(uint32_t) { return false; }

    private:
    void publish() {
        if (appState_ != nullptr) {
            appState_->setPlayback(state_, type_, title_, url_, source_, volume_);
        }
    }

    AppState* appState_ = nullptr;
        uint8_t volume_ = DefaultConfig::DEFAULT_VOLUME_PERCENT;
    String state_ = "idle";
    String type_ = "idle";
    String title_ = "Audio disabled";
    String url_;
    String source_ = "disabled";
};

using AudioPlayerType = AudioPlayerStub;
#else
#include "audio_player.h"
using AudioPlayerType = AudioPlayer;
#endif

namespace {
AppState* appState = nullptr;
SettingsManager* settingsManager = nullptr;
SettingsBundle* settings = nullptr;
WiFiManager* wifiManager = nullptr;
BatteryMonitor* batteryMonitor = nullptr;
DisplayManager* displayManager = nullptr;
AudioPlayerType* audioPlayer = nullptr;
OtaManager* otaManager = nullptr;
MqttManager* mqttManager = nullptr;
WebServerManager* webServer = nullptr;
SoundEffectsManager* soundEffects = nullptr;

struct DeferredActions {
    bool settingsApplyPending = false;
    bool settingsApplyWifiPowerOnly = false;
    SettingsBundle pendingSettings;
    bool mqttConnectionChangePending = false;
    bool mqttConnectRequested = false;
    bool playPending = false;
    bool playAddToHistory = true;
    bool playFromStorage = false;
    StorageTarget playStorageTarget = StorageTarget::Flash;
    bool stopPending = false;
    bool volumePending = false;
    bool volumeSavePending = false;
    uint8_t pendingVolume = 0;
    unsigned long volumeSaveAt = 0;
    bool equalizerPending = false;
    String pendingEqualizerPreset;
    int8_t pendingEqualizerLowDb = 0;
    int8_t pendingEqualizerPresenceDb = 0;
    int8_t pendingEqualizerHighDb = 0;
    String playUrl;
    String playStoragePath;
    String playLabel;
    String playType;
    String playSource;
    uint32_t playResumePositionSeconds = 0;
};

DeferredActions* deferredActions = nullptr;
QueueHandle_t logicAudioQueue = nullptr;
BuzzerMelody buzzerMelody;
LogicDevice logicDevice;
bool queueAudioSourceOwned(JsonVariantConst source, String& error,const char* owner) {
    if (otaManager != nullptr && otaManager->isFirmwareTransferActive()) {error="Firmware update is active";return false;}
    JsonDocument resolved;
    if(!source["melodyId"].isNull()){
        JsonDocument assets;deserializeJson(assets,settings->ui.recordedMelodies);String id=source["melodyId"] | "";
        if(!assets[id].is<JsonObject>()){error="Recorded melody was not compiled into this firmware";return false;}
        resolved.set(source);resolved.remove("melodyId");resolved["melody"].set(assets[id]);source=resolved.as<JsonVariantConst>();
    }
    if(!source["output"].isNull()){if(!BuzzerMelody::validate(source,*settings,error))return false;}
    else{
#ifdef APP_DISABLE_AUDIO
        error="DAC audio is not supported on this build";return false;
#else
        if (!AudioPlayer::validateSource(source,error)) return false;
#endif
    }
    JsonDocument queued;queued.set(source);queued.remove("__logic");queued["__logic"]["outputToken"]=LogicAudioDispatch::outputToken(!source["output"].isNull());
    if(owner&&*owner){queued["__logic"]["owner"]=owner;queued["__logic"]["token"]=LogicAudioDispatch::token(owner);}
    source=queued.as<JsonVariantConst>();
    if(logicAudioQueue&&uxQueueMessagesWaiting(logicAudioQueue)>=8){error="Audio source queue is busy";return false;}
    const size_t length=measureJson(source)+1;if(length>16384){error="Audio source exceeds 16 KiB";return false;}
    char* payload=static_cast<char*>(heap_caps_malloc(length,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!payload)payload=static_cast<char*>(heap_caps_malloc(length,MALLOC_CAP_8BIT));
    if(!payload){error="Insufficient audio source memory";return false;}
    serializeJson(source,payload,length);
    if (!logicAudioQueue || xQueueSend(logicAudioQueue,&payload,0)!=pdTRUE) {heap_caps_free(payload);error="Audio source queue is busy";return false;}
    return true;
}


bool queueAudioSource(JsonVariantConst source,String& error){return queueAudioSourceOwned(source,error,nullptr);}
bool queueLogicAudioStop(JsonObjectConst node,String& error,bool all=false){
 const char* owner=!all&&node["type"]=="peripheral.play"?(node["id"]|""):"";
 bool buzzer=std::string(node["peripheral"]["profile"]|"").find("buzzer")!=std::string::npos;
 if(*owner)LogicAudioDispatch::token(owner,true);else LogicAudioDispatch::outputToken(buzzer,true);
 JsonDocument command;command["__logic"]["stop"]=true;command["__logic"]["owner"]=owner;command["__logic"]["buzzer"]=buzzer;
 size_t size=measureJson(command)+1;char* payload=(char*)heap_caps_malloc(size,MALLOC_CAP_8BIT);
 if(!payload){error="Insufficient stop command memory";return false;}serializeJson(command,payload,size);
 if(!logicAudioQueue||xQueueSendToFront(logicAudioQueue,&payload,0)!=pdTRUE){heap_caps_free(payload);error="Audio stop queue is busy";return false;}
 return true;
}
String currentDacLogicOwner,currentBuzzerLogicOwner;


struct RuntimeAudioAutomation {
    bool startupEffectPending = false;
    bool startupEffectActive = false;
    bool bootUpdateCheckQueued = false;
    bool updateAvailableHandled = false;
    bool pendingAutoUpdateInstall = false;
    bool lowBatteryCueActive = false;
    bool ambientVolumeApplied = false;
    bool effectVolumeApplied = false;
    bool alarmActive = false;
    bool restartPending = false;
    bool restartFactoryReset = false;
    bool restartForcePending = false;
    bool webUiLockPending = false;
    bool pendingCommandAfterNotification = false;
    bool previewResumePending = false;
    bool resumeSavedPlaybackPending = false;
    uint8_t ambientPreviousVolume = 0;
    uint8_t previewResumeVolume = 0;
    uint32_t previewResumePositionSeconds = 0;
    uint8_t startupEffectAttempts = 0;
    unsigned long startupEffectEligibleAt = 0;
    unsigned long resumeSavedPlaybackEligibleAt = 0;
    unsigned long updateAvailableEffectEligibleAt = 0;
    unsigned long ambientEligibleAt = 0;
    unsigned long restartForceAt = 0;
    PlaybackCommand pendingCommand;
    String restartReason;
    String previewResumeUrl;
    String previewResumeLabel;
    String previewResumeType;
    String previewResumeSource;
    String previewActiveSource;
};

RuntimeAudioAutomation runtimeAudio;

uint16_t readNativeTouch(uint8_t pin) {
#if defined(SOC_TOUCH_SENSOR_SUPPORTED) && SOC_TOUCH_SENSOR_SUPPORTED
    return touchRead(pin);
#else
    (void)pin;
    return 0;
#endif
}

struct PhysicalButtonState {
    int8_t configuredIndex = -1;
    uint8_t controlSlot = 0;
    uint8_t pin = 0;
    const char* label = "";
    String profileValue = "none";
    bool limitSwitch = false;
    bool normallyClosed = false;
    bool limitSwitchUsesPullup = true;
    bool limitSwitchActiveHigh = false;
    bool nativeTouch = false;
    bool touchSupported = false;
    bool mainControlEnabled = false;
    bool factoryResetTouch = false;
    uint8_t sensitivityPercent = 55;
    uint16_t touchRawValue = 0;
    uint16_t touchBaselineValue = 0;
    uint8_t touchPressCandidateCount = 0;
    uint8_t touchReleaseCandidateCount = 0;
    bool lastSampledPressed = false;
    bool stablePressed = false;
    bool holdResetTriggered = false;
    unsigned long lastTransitionAt = 0;
    unsigned long pressedSinceAt = 0;
};

struct PlaybackHistoryEntry {
    String url;
    String label;
    String type;
    String source;
};

bool rebootRequested = false;
bool factoryResetRequested = false;
unsigned long rebootAt = 0;
unsigned long factoryResetLedBlinkUntil = 0;
bool touchStatusLedOverrideActive = false;
unsigned long lastHeapUpdateAt = 0;
bool recoveryRebootScheduled = false;
bool wokeFromDeepSleep = false;
uint8_t activeI2sBclkPin = DefaultConfig::I2S_BCLK_PIN;
uint8_t activeI2sWsPin = DefaultConfig::I2S_WS_PIN;
uint8_t activeI2sDoutPin = DefaultConfig::I2S_DOUT_PIN;
bool activeAudioOutputEnabled = true;
uint8_t activeWapeTriggerPin = 0;
bool wapeTriggerInitialized = false;
bool wapePulseActive = false;
unsigned long wapePulseReleaseAt = 0;
unsigned long lowBatteryWakeStartedAt = 0;
unsigned long lastOtaMqttPublishAt = 0;
bool previousWifiConnected = false;
bool previousMqttConnected = false;
bool previousCharging = false;
String previousPlaybackState = "idle";
String previousPlaybackSource = "none";
String previousPlaybackType = "idle";
String lastPublishedOtaSignature;
bool transitionStateInitialized = false;
bool otaPendingVerification = false;
bool otaHealthConfirmed = false;
unsigned long otaBootStartedAt = 0;
bool powerCycleCounterClearArmed = false;
bool runtimeSafeModeActive = false;
unsigned long powerCycleCounterClearAt = 0;
unsigned long lastInputPollAt = 0;
String otaPendingVersion;
String lastRolledBackVersion;
String lastRollbackReason;
uint32_t lastProcessedMotorStateVersion = 0;
uint32_t lastPublishedMotorStateVersion = 0;
uint32_t motorStateRepublishAt = 0;
uint8_t motorStateRepublishRemaining = 0;
uint32_t activeCpuFrequencyMhz = 0;
uint8_t cpuGovernorHighSamples = 0;
uint8_t cpuGovernorLowSamples = 0;
unsigned long lastCpuGovernorSampleAt = 0;
unsigned long lastCpuFrequencyChangeAt = 0;
unsigned long lastCpuFrequencyFailureAt = 0;
AppStateSnapshot runtimeStateSnapshot;
bool runtimeStateSnapshotInitialized = false;
unsigned long lastRuntimeStateServiceAt = 0;

struct CloneProvisioningState {
    String commandLine;
    String payload;
    size_t expectedLength = 0;
    uint32_t expectedCrc32 = 0;
    bool receiving = false;
};

CloneProvisioningState cloneProvisioning;

constexpr float kBatteryPercentEmptyVoltage = 3.2f;
constexpr float kBatteryPercentFullVoltage = 4.2f;
constexpr float kBatteryPolicyMinimumPlausibleVoltage = 0.5f;
constexpr unsigned long kTouchFactoryResetHoldMs = 10000UL;
constexpr unsigned long kFactoryResetLedBlinkIntervalMs = 500UL;
constexpr unsigned long kFactoryResetLedSuccessWindowMs = 1500UL;
constexpr unsigned long kLowBatteryWakeWindowMs = 30000UL;
constexpr unsigned long kLowBatteryStartupGraceMs = 30000UL;
constexpr unsigned long kVolumePersistDelayMs = 750UL;
constexpr unsigned long kButtonDebounceMs = 30UL;
constexpr unsigned long kTouchDebounceMs = 20UL;
constexpr unsigned long kInputPollIntervalMs = 10UL;
constexpr uint8_t kDefaultTouchSensitivityPercent = 55;
constexpr size_t kMaxPeripheralInputProfiles = 10;
constexpr uint8_t kTouchPressCandidateSamples = 3;
constexpr uint8_t kTouchReleaseCandidateSamples = 3;
constexpr size_t kPlaybackHistoryLimit = 12;
constexpr unsigned long kOtaHealthConfirmDelayMs = 8000UL;
constexpr unsigned long kWapePulseDurationMs = 500UL;
constexpr unsigned long kStartupEffectDelayMs = 1200UL;
constexpr unsigned long kStartupEffectRetryDelayMs = 1500UL;
constexpr uint8_t kStartupEffectMaxAttempts = 5;
constexpr unsigned long kAmbientResumeDelayMs = 30000UL;
constexpr unsigned long kRestartEffectForceDelayMs = 7000UL;
constexpr unsigned long kEffectPreviewRetryDelayMs = 150UL;
constexpr unsigned long kPowerCycleCounterClearDelayMs = 10000UL;
constexpr uint8_t kPowerCycleFactoryResetThreshold = 7;
constexpr uint32_t kCpuFrequencyIdleMhz = 80;
constexpr uint32_t kCpuFrequencyMediumMhz = 160;
constexpr uint32_t kCpuFrequencyBurstMhz = 240;
constexpr unsigned long kCpuGovernorSampleIntervalMs = 500UL;
constexpr unsigned long kCpuFrequencyMinimumHoldMs = 2000UL;
constexpr unsigned long kCpuFrequencyRetryDelayMs = 5000UL;
constexpr uint8_t kCpuEmergencyLoadPercent = 92;
constexpr uint8_t kCpuIdleUpshiftLoadPercent = 70;
constexpr uint8_t kCpuPlaybackUpshiftLoadPercent = 75;
constexpr uint8_t kCpuPlaybackDownshiftLoadPercent = 30;
constexpr uint8_t kCpuBurstDownshiftLoadPercent = 45;
constexpr uint8_t kCpuUpshiftSamples = 2;
constexpr uint8_t kCpuPlaybackDownshiftSamples = 10;
constexpr uint8_t kCpuBurstDownshiftSamples = 8;
constexpr unsigned long kRuntimeStateServiceIntervalMs = 20UL;
constexpr uint32_t kActiveLoopDelayMs = 1;
constexpr uint32_t kIdleLoopDelayMs = 5;
constexpr size_t kCloneConfigurationMaxBytes = 32768;
constexpr char kCloneConfigurationCommand[] = "ELMA_CLONE_CONFIG ";

constexpr char kOtaRollbackNamespace[] = "ota_state";
constexpr char kOtaPendingVersionKey[] = "pend_ver";
constexpr char kOtaPendingReasonKey[] = "pend_reason";
constexpr char kOtaLastBadVersionKey[] = "bad_ver";
constexpr char kOtaLastBadReasonKey[] = "bad_reason";
constexpr char kPowerCycleNamespace[] = "boot_guard";
constexpr char kPowerCycleCountKey[] = "pc_count";
constexpr char kLowBatteryBootGuardFixKey[] = "lb_sleep_fix";
constexpr char kAbnormalResetCountKey[] = "crash_count";
constexpr uint8_t kRuntimeSafeModeThreshold = 3;
constexpr char kAudioPlaybackGuardNamespace[] = "audio_guard";
constexpr char kAudioPlaybackPendingKey[] = "pending";
constexpr char kAudioPlaybackUrlKey[] = "url";
constexpr unsigned long kAudioPlaybackStableMs = 20000UL;

bool audioPlaybackGuardActive = false;
unsigned long audioPlaybackGuardStartedAt = 0;
String audioPlaybackGuardUrl;

bool playRequest(const String& url, const String& label, const String& type, const String& source, String& error, bool addToHistory);
const char* resetReasonToString(esp_reset_reason_t reason);
void flushPendingSettingsNow();
void syncPendingLastPlaybackSettings();
void restoreAmbientVolumeIfNeeded();
void applyCpuFrequencyPolicy();
bool queuePreviewResume(const AppStateSnapshot& snapshot, const String& previewSource);
void clearPreviewResumeState();

bool isResumablePlaybackSelection(const String& url, const String& type, const String& source) {
    String normalizedUrl = url;
    normalizedUrl.trim();
    if (normalizedUrl.isEmpty()) {
        return false;
    }

    String normalizedType = type;
    normalizedType.trim();
    normalizedType.toLowerCase();
    if (normalizedType != "stream" && normalizedType != "media") {
        return false;
    }

    String normalizedSource = source;
    normalizedSource.trim();
    normalizedSource.toLowerCase();
    return !normalizedSource.startsWith("effect-");
}

bool parseSavedPlaybackReference(const String& raw, StorageTarget& target, String& path) {
    String value = raw;
    value.trim();
    value.replace('\\', '/');

    const int separatorIndex = value.indexOf(':');
    if (separatorIndex <= 0) {
        return false;
    }

    String targetId = value.substring(0, separatorIndex);
    targetId.trim();
    targetId.toLowerCase();
    if (targetId == "sd") {
        target = StorageTarget::Sd;
    } else if (targetId == "flash") {
        target = StorageTarget::Flash;
    } else {
        return false;
    }

    path = value.substring(separatorIndex + 1);
    path.trim();
    if (path.isEmpty()) {
        return false;
    }
    if (!path.startsWith("/")) {
        path = "/" + path;
    }
    while (path.indexOf("//") >= 0) {
        path.replace("//", "/");
    }
    if (path.indexOf("..") >= 0) {
        return false;
    }

    return true;
}

String canonicalPlaybackReference(const String& url) {
    StorageTarget storageTarget = StorageTarget::Flash;
    String storagePath;
    if (parseSavedPlaybackReference(url, storageTarget, storagePath)) {
        return String(storageTargetId(storageTarget)) + ":" + storagePath;
    }
    return PlaybackText::normalizeUrl(url);
}

bool resetReasonCanBeCausedByPlayback(esp_reset_reason_t reason) {
    return reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT ||
        reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT;
}

void clearAudioPlaybackGuard() {
    Preferences preferences;
    if (preferences.begin(kAudioPlaybackGuardNamespace, false)) {
        preferences.remove(kAudioPlaybackPendingKey);
        preferences.remove(kAudioPlaybackUrlKey);
        preferences.end();
    }
    audioPlaybackGuardActive = false;
    audioPlaybackGuardStartedAt = 0;
    audioPlaybackGuardUrl = "";
}

void armAudioPlaybackGuard(const String& url) {
    const String normalizedUrl = canonicalPlaybackReference(url);
    Preferences preferences;
    if (preferences.begin(kAudioPlaybackGuardNamespace, false)) {
        preferences.putBool(kAudioPlaybackPendingKey, true);
        preferences.putString(kAudioPlaybackUrlKey, normalizedUrl);
        preferences.end();
    }
    audioPlaybackGuardActive = true;
    audioPlaybackGuardStartedAt = millis();
    audioPlaybackGuardUrl = normalizedUrl;
    DebugLog.printf("[audio-guard] armed for %s\n", normalizedUrl.c_str());
}

void recoverInterruptedAudioPlayback(esp_reset_reason_t resetReason) {
    Preferences preferences;
    if (!preferences.begin(kAudioPlaybackGuardNamespace, true)) return;
    const bool pending = preferences.getBool(kAudioPlaybackPendingKey, false);
    const String guardedUrl = preferences.getString(kAudioPlaybackUrlKey, "");
    preferences.end();
    if (!pending) return;

    if (resetReasonCanBeCausedByPlayback(resetReason) && settings != nullptr &&
        settingsManager != nullptr && settings->audio.lastPlayback.resumeAfterBoot &&
        canonicalPlaybackReference(settings->audio.lastPlayback.url) == guardedUrl) {
        settings->audio.lastPlayback.resumeAfterBoot = false;
        syncPendingLastPlaybackSettings();
        settingsManager->save(*settings);
        runtimeAudio.resumeSavedPlaybackPending = false;
        const String message = "Automatic playback was stopped because this source caused an abnormal restart.";
        if (appState != nullptr) appState->setLastError(message);
        DebugLog.printf("[audio-guard] quarantined unstable saved source after %s reset: %s\n",
                        resetReasonToString(resetReason), guardedUrl.c_str());
    } else {
        DebugLog.printf("[audio-guard] cleared stale playback marker after %s reset\n", resetReasonToString(resetReason));
    }
    clearAudioPlaybackGuard();
}

void serviceAudioPlaybackGuard(const AppStateSnapshot& snapshot) {
    if (!audioPlaybackGuardActive) return;
    const String currentUrl = canonicalPlaybackReference(snapshot.playback.url);
    const bool active = snapshot.playback.state == "playing" || snapshot.playback.state == "buffering";
    if (!active || currentUrl != audioPlaybackGuardUrl) {
        clearAudioPlaybackGuard();
        return;
    }
    if (millis() - audioPlaybackGuardStartedAt >= kAudioPlaybackStableMs) {
        DebugLog.printf("[audio-guard] source stable; automatic resume allowed: %s\n", audioPlaybackGuardUrl.c_str());
        clearAudioPlaybackGuard();
    }
}

void syncPendingLastPlaybackSettings() {
    if (deferredActions != nullptr && deferredActions->settingsApplyPending) {
        deferredActions->pendingSettings.audio.lastPlayback = settings->audio.lastPlayback;
    }
}

void setSavedPlaybackResumeAfterBoot(bool resumeAfterBoot) {
    if (settings == nullptr || settingsManager == nullptr || !settings->audio.rememberLastPlayed) {
        return;
    }

    AudioSettings::LastPlaybackSettings& lastPlayback = settings->audio.lastPlayback;
    if (lastPlayback.resumeAfterBoot == resumeAfterBoot ||
        !isResumablePlaybackSelection(lastPlayback.url, lastPlayback.type, lastPlayback.source)) {
        return;
    }

    lastPlayback.resumeAfterBoot = resumeAfterBoot;
    syncPendingLastPlaybackSettings();
    settingsManager->save(*settings);
}

void persistResumablePlaybackSelection(const String& url, const String& label, const String& type, const String& source, bool resumeAfterBoot = true) {
    if (settings == nullptr || settingsManager == nullptr || !settings->audio.rememberLastPlayed || !isResumablePlaybackSelection(url, type, source)) {
        return;
    }

    const String normalizedUrl = canonicalPlaybackReference(url);
    StorageTarget storageTarget = StorageTarget::Flash;
    String storagePath;
    const bool storageReference = parseSavedPlaybackReference(normalizedUrl, storageTarget, storagePath);
    const String normalizedLabel = storageReference
        ? PlaybackText::normalizeTitle(label, storagePath)
        : PlaybackText::normalizeTitle(label, normalizedUrl);
    String normalizedType = type;
    normalizedType.trim();
    normalizedType.toLowerCase();
    String normalizedSource = source;
    normalizedSource.trim();

    AudioSettings::LastPlaybackSettings& lastPlayback = settings->audio.lastPlayback;
    if (lastPlayback.url == normalizedUrl && lastPlayback.label == normalizedLabel && lastPlayback.type == normalizedType &&
        lastPlayback.source == normalizedSource && lastPlayback.resumeAfterBoot == resumeAfterBoot) {
        return;
    }

    lastPlayback.url = normalizedUrl;
    lastPlayback.label = normalizedLabel;
    lastPlayback.type = normalizedType;
    lastPlayback.source = normalizedSource;
    lastPlayback.resumeAfterBoot = resumeAfterBoot;
    syncPendingLastPlaybackSettings();
    settingsManager->save(*settings);
}

bool queueSavedPlaybackResume() {
    if (settings == nullptr) {
        return false;
    }

    const AudioSettings::LastPlaybackSettings& lastPlayback = settings->audio.lastPlayback;
    if (!settings->audio.rememberLastPlayed || !lastPlayback.resumeAfterBoot || !isResumablePlaybackSelection(lastPlayback.url, lastPlayback.type, lastPlayback.source)) {
        return false;
    }

    String error;
    const bool queued = playRequest(lastPlayback.url, lastPlayback.label, lastPlayback.type, lastPlayback.source, error, false);
    if (!queued && !error.isEmpty()) {
        DebugLog.printf("[audio] saved playback resume skipped: %s\n", error.c_str());
    }
    return queued;
}

bool parseStorageFileReference(const String& raw, StorageTarget& target, String& path) {
    String value = raw;
    value.trim();
    value.replace('\\', '/');

    const int separatorIndex = value.indexOf(':');
    if (separatorIndex <= 0) {
        return false;
    }

    String targetId = value.substring(0, separatorIndex);
    targetId.trim();
    targetId.toLowerCase();
    if (targetId == "sd") {
        target = StorageTarget::Sd;
    } else if (targetId == "flash") {
        target = StorageTarget::Flash;
    } else {
        return false;
    }

    path = value.substring(separatorIndex + 1);
    path.trim();
    if (path.isEmpty()) {
        return false;
    }
    if (!path.startsWith("/")) {
        path = "/" + path;
    }
    while (path.indexOf("//") >= 0) {
        path.replace("//", "/");
    }
    if (path.indexOf("..") >= 0) {
        return false;
    }

    return true;
}

void applyCpuFrequencyPolicy() {
    const unsigned long now = millis();
    const uint32_t measuredFrequencyMhz = ESP.getCpuFreqMHz();
    if (measuredFrequencyMhz != 0 && measuredFrequencyMhz != activeCpuFrequencyMhz) {
        activeCpuFrequencyMhz = measuredFrequencyMhz;
    }

    const SystemMetricsSnapshot metrics = getSystemMetricsSnapshot();
    if (!metrics.cpuLoadAvailable || metrics.cpuLoadCoreCount == 0) {
        return;
    }

    // A saturated task on one core must not be hidden by averaging it with an
    // idle core, so decisions use the busiest core. The monitor still reports
    // the aggregate and individual core loads.
    uint8_t peakLoadPercent = metrics.cpuLoadPercent;
    for (uint8_t coreIndex = 0; coreIndex < metrics.cpuLoadCoreCount; ++coreIndex) {
        peakLoadPercent = max<uint8_t>(peakLoadPercent, metrics.cpuLoadCorePercent[coreIndex]);
    }

    uint32_t targetFrequencyMhz = activeCpuFrequencyMhz;
    const char* policyReason = nullptr;
    const bool emergencyLoad = peakLoadPercent >= kCpuEmergencyLoadPercent;
    const bool minimumHoldElapsed = now - lastCpuFrequencyChangeAt >= kCpuFrequencyMinimumHoldMs;

    if (activeCpuFrequencyMhz <= kCpuFrequencyIdleMhz) {
        cpuGovernorLowSamples = 0;
        cpuGovernorHighSamples = peakLoadPercent >= kCpuIdleUpshiftLoadPercent
            ? min<uint8_t>(static_cast<uint8_t>(cpuGovernorHighSamples + 1), kCpuUpshiftSamples)
            : 0;
        if (emergencyLoad || (minimumHoldElapsed && cpuGovernorHighSamples >= kCpuUpshiftSamples)) {
            targetFrequencyMhz = kCpuFrequencyMediumMhz;
            policyReason = emergencyLoad ? "load-emergency" : "load-high";
        }
    } else if (activeCpuFrequencyMhz <= kCpuFrequencyMediumMhz) {
        cpuGovernorHighSamples = peakLoadPercent >= kCpuPlaybackUpshiftLoadPercent
            ? min<uint8_t>(static_cast<uint8_t>(cpuGovernorHighSamples + 1), kCpuUpshiftSamples)
            : 0;
        cpuGovernorLowSamples = peakLoadPercent <= kCpuPlaybackDownshiftLoadPercent
            ? min<uint8_t>(static_cast<uint8_t>(cpuGovernorLowSamples + 1), kCpuPlaybackDownshiftSamples)
            : 0;
        if (emergencyLoad || (minimumHoldElapsed && cpuGovernorHighSamples >= kCpuUpshiftSamples)) {
            targetFrequencyMhz = kCpuFrequencyBurstMhz;
            policyReason = emergencyLoad ? "load-emergency" : "load-high";
        } else if (minimumHoldElapsed && cpuGovernorLowSamples >= kCpuPlaybackDownshiftSamples) {
            targetFrequencyMhz = kCpuFrequencyIdleMhz;
            policyReason = "load-low";
        }
    } else {
        cpuGovernorHighSamples = 0;
        cpuGovernorLowSamples = peakLoadPercent <= kCpuBurstDownshiftLoadPercent
            ? min<uint8_t>(static_cast<uint8_t>(cpuGovernorLowSamples + 1), kCpuBurstDownshiftSamples)
            : 0;
        if (minimumHoldElapsed && cpuGovernorLowSamples >= kCpuBurstDownshiftSamples) {
            targetFrequencyMhz = kCpuFrequencyMediumMhz;
            policyReason = "load-low";
        }
    }

    // Flash erase/write waits can look like low CPU load. Keep the clock
    // stable throughout a firmware transfer instead of downshifting between
    // upload chunks while the Wi-Fi and flash tasks are active.
    if (otaManager != nullptr && otaManager->isFirmwareTransferActive()) {
        targetFrequencyMhz = kCpuFrequencyBurstMhz;
        policyReason = "firmware-transfer";
        cpuGovernorHighSamples = 0;
        cpuGovernorLowSamples = 0;
    }

    if (targetFrequencyMhz == activeCpuFrequencyMhz || policyReason == nullptr) {
        return;
    }

    if (lastCpuFrequencyFailureAt != 0 && now - lastCpuFrequencyFailureAt < kCpuFrequencyRetryDelayMs) {
        return;
    }

    if (setCpuFrequencyMhz(targetFrequencyMhz)) {
        activeCpuFrequencyMhz = ESP.getCpuFreqMHz();
        cpuGovernorHighSamples = 0;
        cpuGovernorLowSamples = 0;
        lastCpuFrequencyChangeAt = now;
        lastCpuFrequencyFailureAt = 0;
        DebugLog.printf("[power] cpu frequency set to %lu MHz reason=%s load=%u%% cores=%u/%u%%\n",
                      static_cast<unsigned long>(activeCpuFrequencyMhz),
                      policyReason,
                      static_cast<unsigned>(peakLoadPercent),
                      static_cast<unsigned>(metrics.cpuLoadCorePercent[0]),
                      static_cast<unsigned>(metrics.cpuLoadCoreCount > 1 ? metrics.cpuLoadCorePercent[1] : 0));
    } else {
        lastCpuFrequencyFailureAt = now;
        DebugLog.printf("[power] cpu frequency change to %lu MHz failed reason=%s\n",
                      static_cast<unsigned long>(targetFrequencyMhz),
                      policyReason);
    }
}

PhysicalButtonState button1 { -1, 0, DefaultConfig::BUTTON1_PIN, "Button 1" };
PhysicalButtonState button2 { -1, 1, DefaultConfig::BUTTON2_PIN, "Button 2" };
MotorController motorController;
PlaybackHistoryEntry playbackHistory[kPlaybackHistoryLimit];
size_t playbackHistoryCount = 0;
int playbackHistoryIndex = -1;

bool isNativeTouchProfileValue(const String& profileValue) {
    String normalized = profileValue;
    normalized.trim();
    normalized.toLowerCase();
    return normalized == "esp32-native-touch-pad";
}

bool isLimitSwitchProfileValue(const String& profileValue) {
    String normalized = profileValue;
    normalized.trim();
    normalized.toLowerCase();
    return normalized == "limit-switch";
}

bool isTouchButtonProfileValue(const String& profileValue) {
    String normalized = profileValue;
    normalized.trim();
    normalized.toLowerCase();
    return normalized == "esp32-native-touch-pad" || normalized == "ttp223-touch-button";
}

bool isTouchCapablePin(uint8_t pin) {
#if defined(CONFIG_IDF_TARGET_ESP32)
    switch (pin) {
        case 0:
        case 2:
        case 4:
        case 12:
        case 13:
        case 14:
        case 15:
        case 27:
        case 32:
        case 33:
            return true;
        default:
            return false;
    }
#elif defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3)
    return pin >= 1 && pin <= 14;
#else
    (void)pin;
    return false;
#endif
}

String peripheralInputProfileFromUi(size_t index) {
    if (settings == nullptr) {
        return String("none");
    }

    const String rawSelections = settings->ui.peripheralProfileSelections;
    if (rawSelections.isEmpty()) {
        return String("none");
    }

    JsonDocument document;
    if (deserializeJson(document, rawSelections) != DeserializationError::Ok) {
        return String("none");
    }

    JsonArrayConst inputs = document["inputs"].as<JsonArrayConst>();
    if (inputs.isNull() || index >= inputs.size() || !inputs[index].is<const char*>()) {
        return String("none");
    }

    String profile = inputs[index].as<const char*>();
    profile.trim();
    profile.toLowerCase();
    return profile.isEmpty() ? String("none") : profile;
}

String peripheralInputHelperValue(size_t index, const char* key) {
    if (settings == nullptr || key == nullptr || settings->ui.peripheralHelperBindings.isEmpty()) {
        return "";
    }

    JsonDocument document;
    if (deserializeJson(document, settings->ui.peripheralHelperBindings) != DeserializationError::Ok) {
        return "";
    }

    const String slotKey = String("input:") + index;
    JsonObjectConst slot = document[slotKey].as<JsonObjectConst>();
    if (slot.isNull() || !slot[key].is<const char*>()) {
        return "";
    }

    String value = slot[key].as<const char*>();
    value.trim();
    return value;
}

uint8_t configuredInputPin(size_t index, const String& profileValue, uint8_t fallbackPin) {
    String rawValue;
    if (isNativeTouchProfileValue(profileValue)) {
        rawValue = peripheralInputHelperValue(index, "TOUCH");
    } else if (isLimitSwitchProfileValue(profileValue)) {
        rawValue = peripheralInputHelperValue(index, "COM");
        if (rawValue.isEmpty()) {
            rawValue = peripheralInputHelperValue(index, "SIG");
        }
    } else {
        rawValue = peripheralInputHelperValue(index, "SIG");
    }
    const int numericPin = rawValue.isEmpty() ? -1 : rawValue.toInt();
    if (numericPin < 0 || numericPin > 255) {
        return fallbackPin;
    }
    return static_cast<uint8_t>(numericPin);
}

int configuredAssignedInputPin(size_t index, const String& profileValue) {
    String rawValue;
    if (isNativeTouchProfileValue(profileValue)) {
        rawValue = peripheralInputHelperValue(index, "TOUCH");
    } else if (isLimitSwitchProfileValue(profileValue)) {
        rawValue = peripheralInputHelperValue(index, "COM");
        if (rawValue.isEmpty()) {
            rawValue = peripheralInputHelperValue(index, "SIG");
        }
    } else {
        rawValue = peripheralInputHelperValue(index, "SIG");
    }

    const int numericPin = rawValue.isEmpty() ? -1 : rawValue.toInt();
    return (numericPin < 0 || numericPin > 255) ? -1 : numericPin;
}

uint8_t configuredTouchSensitivity(size_t index) {
    const String rawValue = peripheralInputHelperValue(index, "SENSITIVITY");
    const int numericValue = rawValue.isEmpty() ? static_cast<int>(kDefaultTouchSensitivityPercent) : rawValue.toInt();
    if (numericValue < 5) {
        return 5;
    }
    if (numericValue > 100) {
        return 100;
    }
    return static_cast<uint8_t>(numericValue);
}

bool configuredLimitSwitchNormallyClosed(size_t index) {
    String contactValue = peripheralInputHelperValue(index, "CONTACT");
    contactValue.trim();
    contactValue.toUpperCase();
    return contactValue == "NC";
}

bool configuredLimitSwitchSwitchedToVcc(size_t index) {
    String sourceValue = peripheralInputHelperValue(index, "SOURCE");
    sourceValue.trim();
    sourceValue.toUpperCase();
    return sourceValue == "VCC";
}

void configureLimitSwitchInputPin(size_t index) {
    const String profileValue = peripheralInputProfileFromUi(index);
    if (!isLimitSwitchProfileValue(profileValue)) {
        return;
    }

    const int pin = configuredAssignedInputPin(index, profileValue);
    if (pin < 0) {
        return;
    }

    const bool switchedToVcc = configuredLimitSwitchSwitchedToVcc(index);
    pinMode(static_cast<uint8_t>(pin), switchedToVcc ? INPUT_PULLDOWN : INPUT_PULLUP);
}

bool readConfiguredLimitSwitchPressed(size_t index) {
    const String profileValue = peripheralInputProfileFromUi(index);
    if (!isLimitSwitchProfileValue(profileValue)) {
        return false;
    }

    const int pin = configuredAssignedInputPin(index, profileValue);
    if (pin < 0 || sdStorageUsesPin(static_cast<uint8_t>(pin))) {
        return false;
    }

    const bool normallyClosed = configuredLimitSwitchNormallyClosed(index);
    const bool switchedToVcc = configuredLimitSwitchSwitchedToVcc(index);
    const bool activeHigh = switchedToVcc != normallyClosed;
    configureLimitSwitchInputPin(index);
    return (digitalRead(static_cast<uint8_t>(pin)) == HIGH) == activeHigh;
}

bool configuredTouchMainControlEnabled(size_t index) {
    String flagValue = peripheralInputHelperValue(index, "MAIN_CONTROL");
    flagValue.trim();
    flagValue.toLowerCase();
    return flagValue == "1" || flagValue == "true" || flagValue == "on" || flagValue == "yes";
}

const char* motorRuntimeChannelKey(uint8_t channelIndex) {
    return MotorRuntimeConfig::channelKey(channelIndex);
}

const char* motorRuntimeDirectionKey(bool forward) {
    return MotorRuntimeConfig::directionKey(forward);
}

String normalizeMotorLearnedState(String value) {
    return MotorRuntimeConfig::normalizeLearnedState(value);
}

String normalizeTouchMotorAction(String value) {
    return MotorRuntimeConfig::normalizeTouchAction(value);
}

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
size_t motorRuntimeConfigJsonCapacity(const String& rawConfig);
bool deserializeMotorRuntimeConfigDocument(const String& rawConfig, DynamicJsonDocument& document);

String configuredTouchMotorAction(uint8_t controlSlot) {
    if (settings == nullptr) {
        return "none";
    }

    DynamicJsonDocument document(motorRuntimeConfigJsonCapacity(settings->ui.motorRuntimeConfig));
    if (!deserializeMotorRuntimeConfigDocument(settings->ui.motorRuntimeConfig, document)) {
        return "none";
    }
    JsonObjectConst touchButtons = document["touchButtons"].as<JsonObjectConst>();
    if (touchButtons.isNull()) {
        return "none";
    }

    const char* buttonKey = controlSlot == 0 ? "button1" : "button2";
    JsonObjectConst buttonConfig = touchButtons[buttonKey].as<JsonObjectConst>();
    return normalizeTouchMotorAction(String(static_cast<const char*>(buttonConfig["action"] | "none")));
}

String movementRoleFromRuntimeConfig(JsonObjectConst channelConfig, bool forward) {
    return MotorRuntimeConfig::movementRole(channelConfig, forward);
}

String learnedStateForMovementRole(const String& role) {
    if (role == "opening") {
        return "open";
    }
    if (role == "closing") {
        return "closed";
    }
    return "unknown";
}

String inferredMotorLearnedState(JsonObjectConst channelStatus, JsonObjectConst channelConfig) {
    if (channelStatus.isNull() || (channelStatus["active"] | false)) {
        return "unknown";
    }

    String stopReason = String(static_cast<const char*>(channelStatus["stopReason"] | ""));
    stopReason.trim();
    stopReason.toLowerCase();
    if (stopReason != "time_limit_reached" && stopReason != "end_switch_activated") {
        return "unknown";
    }

    String lastDirection = String(static_cast<const char*>(channelStatus["lastDirection"] | "forward"));
    lastDirection.trim();
    lastDirection.toLowerCase();
    const bool forward = lastDirection != "backward";
    return learnedStateForMovementRole(movementRoleFromRuntimeConfig(channelConfig, forward));
}

String motorPositionState(JsonObjectConst channelStatus, JsonObjectConst channelConfig, const String& learnedState) {
    if (channelStatus.isNull()) {
        return "idle";
    }

    if (channelStatus["active"] | false) {
        String direction = String(static_cast<const char*>(channelStatus["direction"] | "forward"));
        direction.trim();
        direction.toLowerCase();
        const bool forward = direction != "backward";
        const String role = movementRoleFromRuntimeConfig(channelConfig, forward);
        if (role == "opening" || role == "closing") {
            return role;
        }
        return forward ? String("forward") : String("backward");
    }

    return learnedState == "open" || learnedState == "closed" ? learnedState : String("idle");
}

void appendAugmentedMotorStatus(JsonObject root) {
    if (!motorController.available()) {
        root["available"] = false;
        return;
    }

    motorController.appendStatus(root);
    if (settings == nullptr) {
        return;
    }

    DynamicJsonDocument runtimeConfigDoc(motorRuntimeConfigJsonCapacity(settings->ui.motorRuntimeConfig));
    if (!deserializeMotorRuntimeConfigDocument(settings->ui.motorRuntimeConfig, runtimeConfigDoc)) {
        return;
    }
    JsonArray channels = root["channels"].as<JsonArray>();
    for (uint8_t channelIndex = 0; channelIndex < 2 && channelIndex < channels.size(); ++channelIndex) {
        JsonObject channel = channels[channelIndex].as<JsonObject>();
        if (channel.isNull()) {
            continue;
        }

        JsonObjectConst channelConfig = runtimeConfigDoc[motorRuntimeChannelKey(channelIndex)].as<JsonObjectConst>();
        const String persistedLearnedState = normalizeMotorLearnedState(String(static_cast<const char*>(channelConfig["learnedState"] | "unknown")));
        const String inferredLearnedState = inferredMotorLearnedState(channel, channelConfig);
        String learnedState = inferredLearnedState != "unknown" ? inferredLearnedState : persistedLearnedState;

        const String positionState = motorPositionState(channel, channelConfig, learnedState);
        channel["learnedState"] = learnedState;
        channel["positionState"] = positionState;

        if (!(channel["active"] | false) && learnedState != "unknown") {
            String stopReason = String(static_cast<const char*>(channel["stopReason"] | ""));
            stopReason.trim();
            stopReason.toLowerCase();
            const String label = learnedState == "open" ? String("open") : String("closed");
            if (stopReason == "time_limit_reached") {
                channel["statusText"] = label + ", time limit reached";
            } else if (stopReason == "end_switch_activated") {
                channel["statusText"] = label + ", end switch activated";
            } else {
                channel["statusText"] = label;
            }
        }
    }
}

void applyButtonProfileConfig(PhysicalButtonState& button, size_t index, uint8_t fallbackPin) {
    button.configuredIndex = static_cast<int8_t>(index);
    button.profileValue = peripheralInputProfileFromUi(index);
    button.pin = configuredInputPin(index, button.profileValue, fallbackPin);
    button.limitSwitch = isLimitSwitchProfileValue(button.profileValue);
    button.normallyClosed = button.limitSwitch && configuredLimitSwitchNormallyClosed(index);
    const bool switchedToVcc = button.limitSwitch && configuredLimitSwitchSwitchedToVcc(index);
    button.limitSwitchUsesPullup = button.limitSwitch ? !switchedToVcc : true;
    button.limitSwitchActiveHigh = button.limitSwitch ? (switchedToVcc != button.normallyClosed) : false;
    button.nativeTouch = isNativeTouchProfileValue(button.profileValue);
    button.touchSupported = button.nativeTouch && isTouchCapablePin(button.pin);
    button.mainControlEnabled = isTouchButtonProfileValue(button.profileValue) && configuredTouchMainControlEnabled(index);
    button.sensitivityPercent = configuredTouchSensitivity(index);
    button.touchRawValue = 0;
    button.touchBaselineValue = 0;
    button.touchPressCandidateCount = 0;
    button.touchReleaseCandidateCount = 0;
    button.lastSampledPressed = false;
    button.stablePressed = false;
    button.holdResetTriggered = false;
    button.pressedSinceAt = 0;
}

InputChannelSnapshot inputSnapshotFor(const PhysicalButtonState& button) {
    InputChannelSnapshot snapshot;
    snapshot.configuredIndex = button.configuredIndex;
    snapshot.pin = button.pin;
    snapshot.profile = button.profileValue;
    snapshot.nativeTouch = button.nativeTouch;
    snapshot.touchSupported = button.touchSupported;
    snapshot.active = button.stablePressed;
    snapshot.sensitivityPercent = button.sensitivityPercent;
    snapshot.rawValue = button.touchRawValue;
    snapshot.baselineValue = button.touchBaselineValue;
    return snapshot;
}

void publishInputSnapshots() {
    if (appState == nullptr) {
        return;
    }
    appState->setInputs(inputSnapshotFor(button1), inputSnapshotFor(button2));
}

bool isBatterySamplingAllowed() {
    if (audioPlayer == nullptr) {
        return true;
    }

    const String audioState = audioPlayer->currentState();
    return audioState != "playing" && audioState != "buffering";
}

bool isPhysicalButtonEnabled(const PhysicalButtonState& button) {
    return button.profileValue != "none" && !sdStorageUsesPin(button.pin);
}

void flashFactoryResetIndicator() {
    writeStatusLedColor(kStatusLedBrightness, 0, 0);
    delay(500);
    writeStatusLed(false);
}

bool isAnyTouchInputPressed() {
    return (isTouchButtonProfileValue(button1.profileValue) && button1.stablePressed) ||
        (isTouchButtonProfileValue(button2.profileValue) && button2.stablePressed);
}

bool isFactoryResetLedBlinkActive(unsigned long now) {
    return factoryResetLedBlinkUntil != 0 && static_cast<long>(now - factoryResetLedBlinkUntil) < 0;
}

void startFactoryResetLedBlink(unsigned long now, unsigned long durationMs = kFactoryResetLedSuccessWindowMs) {
    factoryResetLedBlinkUntil = now + durationMs;
}

void serviceStatusLedOverrides(unsigned long now) {
    if (isFactoryResetLedBlinkActive(now)) {
        touchStatusLedOverrideActive = false;
        const bool flashOn = ((now / kFactoryResetLedBlinkIntervalMs) % 2UL) == 0;
        writeStatusLedColor(flashOn ? kStatusLedBrightness : 0, 0, 0);
        return;
    }
    if (factoryResetLedBlinkUntil != 0) {
        factoryResetLedBlinkUntil = 0;
        touchStatusLedOverrideActive = false;
        updateStatusLedForNetwork(wifiManager != nullptr && wifiManager->isConnected(), wifiManager != nullptr && wifiManager->isApMode(), now);
        return;
    }
    if (isAnyTouchInputPressed()) {
        touchStatusLedOverrideActive = true;
        writeStatusLedColor(0, kStatusLedBrightness, 0);
        return;
    }
    if (touchStatusLedOverrideActive) {
        touchStatusLedOverrideActive = false;
        updateStatusLedForNetwork(wifiManager != nullptr && wifiManager->isConnected(), wifiManager != nullptr && wifiManager->isApMode(), now);
        return;
    }
    // Refresh continuously so the AP/disconnected blink phases are real. The
    // NeoPixel writer suppresses identical colors, avoiding unnecessary bus
    // activity during audio playback.
    updateStatusLedForNetwork(wifiManager != nullptr && wifiManager->isConnected(), wifiManager != nullptr && wifiManager->isApMode(), now);
}

void triggerTouchHoldFactoryReset(PhysicalButtonState& button) {
    if (button.holdResetTriggered) {
        return;
    }

    button.holdResetTriggered = true;
    DebugLog.printf("[input] %s held on GPIO%u for %lu ms, triggering factory reset\n",
                  button.label,
                  static_cast<unsigned>(button.pin),
                  static_cast<unsigned long>(kTouchFactoryResetHoldMs));
    if (displayManager != nullptr) {
        displayManager->showTemporaryCenterText("Factory Reset", 1500UL);
    }
    if (appState != nullptr) {
        appState->setLastError("Touch hold reset: restoring default settings.");
    }
    factoryResetRequested = true;
    scheduleReboot(kFactoryResetLedSuccessWindowMs + 250UL);
}

void serviceTouchHoldFactoryReset(PhysicalButtonState& button, unsigned long now) {
    if (settings == nullptr || !settings->device.touchHoldFactoryResetEnabled ||
        !button.factoryResetTouch ||
        !isTouchButtonProfileValue(button.profileValue) || !button.stablePressed || button.pressedSinceAt == 0 ||
        rebootRequested || factoryResetRequested || button.holdResetTriggered) {
        return;
    }

    if ((now - button.pressedSinceAt) >= kTouchFactoryResetHoldMs) {
        triggerTouchHoldFactoryReset(button);
    }
}

const char* resetReasonToString(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_UNKNOWN:
            return "unknown";
        case ESP_RST_POWERON:
            return "poweron";
        case ESP_RST_EXT:
            return "external";
        case ESP_RST_SW:
            return "software";
        case ESP_RST_PANIC:
            return "panic";
        case ESP_RST_INT_WDT:
            return "interrupt watchdog";
        case ESP_RST_TASK_WDT:
            return "task watchdog";
        case ESP_RST_WDT:
            return "other watchdog";
        case ESP_RST_DEEPSLEEP:
            return "deepsleep";
        case ESP_RST_BROWNOUT:
            return "brownout";
        case ESP_RST_SDIO:
            return "sdio";
        default:
            return "unhandled";
    }
}

String normalizedAppVersion() {
    return String("v") + APP_VERSION;
}

String normalizedFirmwareVersionFromLabel(String value) {
    value.trim();
    const int binaryExtension = value.indexOf(".bin");
    if (binaryExtension >= 0) {
        value = value.substring(0, binaryExtension);
    }

    int versionMarker = value.lastIndexOf("-v");
    if (versionMarker < 0) {
        versionMarker = value.lastIndexOf("-V");
    }
    if (versionMarker >= 0) {
        value = value.substring(versionMarker + 1);
    }

    value.trim();
    while (value.endsWith(")")) {
        value.remove(value.length() - 1);
        value.trim();
    }
    if (!value.startsWith("v") && !value.startsWith("V") && value.length() > 0 && isDigit(value[0])) {
        value = "v" + value;
    } else if (value.startsWith("V")) {
        value.setCharAt(0, 'v');
    }
    return value;
}

String readPreferenceStringIfPresent(Preferences& prefs, const char* key) {
    return prefs.isKey(key) ? prefs.getString(key, "") : String("");
}

void removePreferenceIfPresent(Preferences& prefs, const char* key) {
    if (prefs.isKey(key)) {
        prefs.remove(key);
    }
}

bool isPowerCycleResetReason(esp_reset_reason_t reason) {
    return reason == ESP_RST_POWERON || reason == ESP_RST_BROWNOUT;
}

void clearPowerCycleCounter() {
    Preferences prefs;
    if (!prefs.begin(kPowerCycleNamespace, false)) {
        return;
    }
    removePreferenceIfPresent(prefs, kPowerCycleCountKey);
    prefs.end();
}

uint8_t updatePowerCycleCounter(esp_reset_reason_t resetReason) {
    Preferences prefs;
    if (!prefs.begin(kPowerCycleNamespace, false)) {
        return 0;
    }

    uint8_t count = 0;
    if (isPowerCycleResetReason(resetReason)) {
        count = prefs.getUChar(kPowerCycleCountKey, 0);
        if (count < 255) {
            count += 1;
        }
        prefs.putUChar(kPowerCycleCountKey, count);
    } else {
        removePreferenceIfPresent(prefs, kPowerCycleCountKey);
    }

    prefs.end();
    return count;
}

uint8_t updateAbnormalResetCounter(esp_reset_reason_t resetReason) {
    Preferences preferences;
    if (!preferences.begin(kPowerCycleNamespace, false)) return 0;
    uint8_t count=preferences.getUChar(kAbnormalResetCountKey,0);
    if(resetReasonCanBeCausedByPlayback(resetReason)&&count<255){++count;preferences.putUChar(kAbnormalResetCountKey,count);}
    preferences.end();return count;
}

void clearAbnormalResetCounter() {
    Preferences preferences;
    if(preferences.begin(kPowerCycleNamespace,false)){removePreferenceIfPresent(preferences,kAbnormalResetCountKey);preferences.end();}
}

void armPowerCycleCounterClear() {
    powerCycleCounterClearArmed = true;
    powerCycleCounterClearAt = millis() + kPowerCycleCounterClearDelayMs;
}

void maybeClearPowerCycleCounterAfterStableBoot() {
    if (!powerCycleCounterClearArmed || static_cast<long>(millis() - powerCycleCounterClearAt) < 0) {
        return;
    }
    clearPowerCycleCounter();
    clearAbnormalResetCounter();
    powerCycleCounterClearArmed = false;
    DebugLog.println("[boot-guard] reset counters cleared after stable uptime");
}

void storeRollbackPendingInfo(const String& version, const String& reason) {
    Preferences prefs;
    if (!prefs.begin(kOtaRollbackNamespace, false)) {
        return;
    }
    prefs.putString(kOtaPendingVersionKey, version);
    prefs.putString(kOtaPendingReasonKey, reason);
    prefs.end();
}

void clearRollbackPendingInfo() {
    Preferences prefs;
    if (!prefs.begin(kOtaRollbackNamespace, false)) {
        return;
    }
    removePreferenceIfPresent(prefs, kOtaPendingVersionKey);
    removePreferenceIfPresent(prefs, kOtaPendingReasonKey);
    prefs.end();
}

void persistRollbackOutcome(const String& version, const String& reason) {
    Preferences prefs;
    if (!prefs.begin(kOtaRollbackNamespace, false)) {
        return;
    }
    prefs.putString(kOtaLastBadVersionKey, version);
    prefs.putString(kOtaLastBadReasonKey, reason);
    prefs.end();
}

void repairLowBatteryBootCounterOnce() {
    Preferences prefs;
    if (!prefs.begin(kPowerCycleNamespace, false)) {
        return;
    }
    if (!prefs.getBool(kLowBatteryBootGuardFixKey, false)) {
        removePreferenceIfPresent(prefs, kPowerCycleCountKey);
        prefs.putBool(kLowBatteryBootGuardFixKey, true);
        DebugLog.println("[boot-guard] cleared legacy counter for low-battery sleep fix");
    }
    prefs.end();
}

void clearRollbackOutcome() {
    Preferences prefs;
    if (prefs.begin(kOtaRollbackNamespace, false)) {
        removePreferenceIfPresent(prefs, kOtaLastBadVersionKey);
        removePreferenceIfPresent(prefs, kOtaLastBadReasonKey);
        prefs.end();
    }
    lastRolledBackVersion = "";
    lastRollbackReason = "";
}

void loadRollbackState() {
    Preferences prefs;
    if (!prefs.begin(kOtaRollbackNamespace, false)) {
        return;
    }

    otaPendingVersion = normalizedFirmwareVersionFromLabel(readPreferenceStringIfPresent(prefs, kOtaPendingVersionKey));
    String pendingReason = readPreferenceStringIfPresent(prefs, kOtaPendingReasonKey);
    lastRolledBackVersion = readPreferenceStringIfPresent(prefs, kOtaLastBadVersionKey);
    lastRollbackReason = readPreferenceStringIfPresent(prefs, kOtaLastBadReasonKey);

    const String currentVersion = normalizedAppVersion();
    const esp_partition_t* runningPartition = esp_ota_get_running_partition();
    esp_ota_img_states_t otaState = ESP_OTA_IMG_UNDEFINED;
    if (runningPartition != nullptr && esp_ota_get_state_partition(runningPartition, &otaState) == ESP_OK && otaState == ESP_OTA_IMG_PENDING_VERIFY) {
        otaPendingVerification = true;
        otaBootStartedAt = millis();
        otaHealthConfirmed = false;
        if (otaPendingVersion.isEmpty()) {
            otaPendingVersion = currentVersion;
        }
        if (pendingReason.isEmpty()) {
            pendingReason = "App booted but did not finish health confirmation.";
        }
        prefs.putString(kOtaPendingVersionKey, otaPendingVersion);
        prefs.putString(kOtaPendingReasonKey, pendingReason);
    } else {
        otaPendingVerification = false;
        otaHealthConfirmed = true;
        if (!otaPendingVersion.isEmpty() && !otaPendingVersion.equalsIgnoreCase(currentVersion)) {
            lastRolledBackVersion = otaPendingVersion;
            if (pendingReason.isEmpty()) {
                pendingReason = "New firmware rebooted before health confirmation; bootloader rolled back.";
            }
            lastRollbackReason = pendingReason;
            prefs.putString(kOtaLastBadVersionKey, lastRolledBackVersion);
            prefs.putString(kOtaLastBadReasonKey, lastRollbackReason);
        } else if (!otaPendingVersion.isEmpty()) {
            // A non-pending partition running the attempted version is a
            // successful update, not a rollback. Older builds stored the full
            // local-upload label here, which caused a false mismatch.
            removePreferenceIfPresent(prefs, kOtaLastBadVersionKey);
            removePreferenceIfPresent(prefs, kOtaLastBadReasonKey);
            lastRolledBackVersion = "";
            lastRollbackReason = "";
        }
        removePreferenceIfPresent(prefs, kOtaPendingVersionKey);
        removePreferenceIfPresent(prefs, kOtaPendingReasonKey);
        otaPendingVersion = "";
    }

    prefs.end();
}

void refreshRollbackStateInOtaManager() {
    if (otaManager == nullptr) {
        return;
    }
    otaManager->setRollbackState(otaPendingVerification && !otaHealthConfirmed, otaPendingVersion, lastRolledBackVersion, lastRollbackReason);
}

void initializeWapeTriggerPin() {
    if (activeWapeTriggerPin == 0) {
        wapeTriggerInitialized = false;
        return;
    }
    pinMode(activeWapeTriggerPin, OUTPUT);
    digitalWrite(activeWapeTriggerPin, HIGH);
    wapeTriggerInitialized = true;
}

void applyWapeTriggerPin(uint8_t pin) {
    if (wapeTriggerInitialized && activeWapeTriggerPin != 0 && activeWapeTriggerPin != pin) {
        digitalWrite(activeWapeTriggerPin, HIGH);
        pinMode(activeWapeTriggerPin, INPUT);
    }

    activeWapeTriggerPin = pin;
    wapePulseActive = false;

    if (pin == 0) {
        wapeTriggerInitialized = false;
        return;
    }

    initializeWapeTriggerPin();
}

bool isWapeDisplayActive() {
    return settings != nullptr && settings->oled.displayType == "wape" && activeWapeTriggerPin != 0;
}

void requestWapeTriggerPulse() {
    if (!isWapeDisplayActive()) {
        return;
    }
    if (!wapeTriggerInitialized) {
        initializeWapeTriggerPin();
    }
    digitalWrite(activeWapeTriggerPin, LOW);
    wapePulseActive = true;
    wapePulseReleaseAt = millis() + kWapePulseDurationMs;
}

void triggerWapeDisplay() {
    requestWapeTriggerPulse();
}

void serviceWapeTriggerPulse() {
    if (!wapePulseActive) {
        return;
    }
    if (static_cast<long>(millis() - wapePulseReleaseAt) < 0) {
        return;
    }
    if (wapeTriggerInitialized && activeWapeTriggerPin != 0) {
        digitalWrite(activeWapeTriggerPin, HIGH);
    }
    wapePulseActive = false;
}

[[noreturn]] void rollbackAndReboot(const String& reason) {
    const String version = otaPendingVersion.isEmpty() ? normalizedAppVersion() : otaPendingVersion;
    storeRollbackPendingInfo(version, reason);
    persistRollbackOutcome(version, reason);
    motorController.prepareForRestart();
    DebugLog.printf("[rollback] %s\n", reason.c_str());
    Serial.flush();
    DebugLog.service(true);
    const esp_err_t result = esp_ota_mark_app_invalid_rollback_and_reboot();
    DebugLog.printf("[rollback] esp_ota_mark_app_invalid_rollback_and_reboot failed: %d\n", static_cast<int>(result));
    Serial.flush();
    delay(1000);
    DebugLog.service(true);
    ESP.restart();
    for (;;) {
        delay(1000);
    }
}

void confirmOtaHealthIfReady() {
    if (!otaPendingVerification || otaHealthConfirmed) {
        return;
    }
    if ((millis() - otaBootStartedAt) < kOtaHealthConfirmDelayMs) {
        return;
    }
    if (rebootRequested || factoryResetRequested || recoveryRebootScheduled) {
        return;
    }

    const esp_err_t result = esp_ota_mark_app_valid_cancel_rollback();
    if (result == ESP_OK) {
        otaHealthConfirmed = true;
        otaPendingVerification = false;
        otaPendingVersion = "";
        clearRollbackPendingInfo();
        clearRollbackOutcome();
        refreshRollbackStateInOtaManager();
        DebugLog.println("[rollback] OTA firmware marked healthy");
        Serial.flush();
    } else {
        DebugLog.printf("[rollback] failed to confirm OTA app: %d\n", static_cast<int>(result));
        Serial.flush();
    }
}

void scheduleReboot(uint32_t delayMs) {
    rebootRequested = true;
    rebootAt = millis() + delayMs;
}

String effectFileForSource(const String& source) {
    if (settings == nullptr) {
        return "";
    }
    if (source == "effect-startup") return settings->effects.startupFile;
    if (source == "effect-alarm") return settings->effects.alarmFile;
    if (source == "effect-notification") return settings->effects.notificationFile;
    if (source == "effect-ambient") return settings->effects.ambientSoundFile;
    if (source == "effect-low-battery") return settings->effects.lowBatteryFile;
    if (source == "effect-shutdown") return settings->effects.shutDownFile;
    if (source == "effect-update-available") return settings->effects.updateAvailableFile;
    if (source == "effect-update-success") return settings->effects.updateSuccessFile;
    return "";
}

uint8_t effectVolumeForSource(const String& source) {
    if (settings == nullptr) {
        return DefaultConfig::DEFAULT_VOLUME_PERCENT;
    }
    if (source == "effect-startup") return settings->effects.startupVolumePercent;
    if (source == "effect-alarm") return settings->effects.alarmVolumePercent;
    if (source == "effect-notification") return settings->effects.notificationVolumePercent;
    if (source == "effect-ambient") return settings->effects.ambientVolumePercent;
    if (source == "effect-low-battery") return settings->effects.lowBatteryVolumePercent;
    if (source == "effect-shutdown") return settings->effects.shutDownVolumePercent;
    if (source == "effect-update-available") return settings->effects.updateAvailableVolumePercent;
    if (source == "effect-update-success") return settings->effects.updateSuccessVolumePercent;
    return settings->device.savedVolumePercent;
}

void restoreEffectVolumeIfNeeded() {
    if (!runtimeAudio.effectVolumeApplied || audioPlayer == nullptr) {
        return;
    }
    audioPlayer->setVolumePercent(settings != nullptr ? settings->device.savedVolumePercent : DefaultConfig::DEFAULT_VOLUME_PERCENT);
    runtimeAudio.effectVolumeApplied = false;
}

bool playConfiguredEffect(const String& effectRef, const String& label, const String& source, String* errorOut = nullptr) {
    auto setEffectError = [&](const String& message) {
        if (errorOut != nullptr) {
            *errorOut = message;
        }
    };

    if (effectRef.isEmpty()) {
        setEffectError(label + " effect is not configured.");
        return false;
    }
    if (deferredActions == nullptr) {
        setEffectError(label + " effect could not start because deferred audio actions are unavailable.");
        return false;
    }

    StorageTarget effectTarget = StorageTarget::Flash;
    String effectPath;
    if (parseStorageFileReference(effectRef, effectTarget, effectPath)) {
        if (effectTarget == StorageTarget::Sd && !storageMounted(effectTarget)) {
            remountActiveStorageBackend(effectTarget);
        }
        if (!storageMounted(effectTarget)) {
            setEffectError(label + " effect storage is not mounted.");
            return false;
        }
        if (effectTarget != StorageTarget::Sd && !storageExists(effectTarget, effectPath)) {
            setEffectError(label + " effect file is missing: " + effectRef);
            return false;
        }
    }

    const bool resumableExclusiveEffect = source == "effect-notification" || source == "effect-update-available" ||
        source == "effect-low-battery";
    if (resumableExclusiveEffect && appState != nullptr) {
        queuePreviewResume(appState->snapshot(), source);
    }

    if (source.startsWith("effect-") && source != "effect-ambient" && audioPlayer != nullptr) {
        restoreAmbientVolumeIfNeeded();
        audioPlayer->setVolumePercent(effectVolumeForSource(source));
        runtimeAudio.effectVolumeApplied = true;
    }
    String requestError;
    if (playRequest(effectRef, label, "effect", source, requestError, false)) {
        setEffectError("");
        return true;
    }
    restoreEffectVolumeIfNeeded();
    setEffectError(requestError.isEmpty() ? label + " effect could not start." : requestError);
    return false;
}

bool tryOverlayEffectReference(const String& effectRef, uint8_t duckPercent = 35, uint8_t overlayPercent = 100, bool allowAmbientSource = false) {
    if (effectRef.isEmpty() || audioPlayer == nullptr || appState == nullptr) {
        return false;
    }
    StorageTarget target = StorageTarget::Flash;
    String path;
    if (!parseStorageFileReference(effectRef, target, path)) {
        return false;
    }
    const AppStateSnapshot snapshot = appState->snapshot();
    if (snapshot.playback.state != "playing" ||
        (snapshot.playback.source.startsWith("effect-") && (!allowAmbientSource || snapshot.playback.source != "effect-ambient"))) {
        return false;
    }
    return audioPlayer->playStorageOverlay(target, path, duckPercent, overlayPercent);
}

bool tryOverlayConfiguredEffect(const String& effectRef, uint8_t duckPercent = 35, uint8_t overlayPercent = 100) {
    return tryOverlayEffectReference(effectRef, duckPercent, overlayPercent, false);
}

bool playConfiguredEffectSource(const String& source, const String& label, String* errorOut = nullptr) {
    return playConfiguredEffect(effectFileForSource(source), label, source, errorOut);
}

uint8_t ambientPlaybackVolumePercent() {
    if (settings == nullptr) {
        return 20;
    }
    return effectVolumeForSource("effect-ambient");
}

void restoreAmbientVolumeIfNeeded() {
    if (!runtimeAudio.ambientVolumeApplied || audioPlayer == nullptr) {
        return;
    }
    audioPlayer->setVolumePercent(settings != nullptr ? settings->device.savedVolumePercent : runtimeAudio.ambientPreviousVolume);
    runtimeAudio.ambientVolumeApplied = false;
}

void startAmbientIfEligible(const AppStateSnapshot& snapshot) {
    if (settings == nullptr || audioPlayer == nullptr || runtimeAudio.alarmActive || runtimeAudio.restartPending ||
        runtimeAudio.startupEffectPending || runtimeAudio.startupEffectActive || runtimeAudio.resumeSavedPlaybackPending ||
        (deferredActions != nullptr && deferredActions->playPending) ||
        settings->effects.ambientSoundFile.isEmpty() || snapshot.playback.state != "idle") {
        return;
    }
    if (runtimeAudio.ambientEligibleAt == 0 || static_cast<long>(millis() - runtimeAudio.ambientEligibleAt) < 0) {
        return;
    }
    runtimeAudio.ambientPreviousVolume = settings->device.savedVolumePercent;
    audioPlayer->setVolumePercent(ambientPlaybackVolumePercent());
    runtimeAudio.ambientVolumeApplied = true;
    if (!playConfiguredEffectSource("effect-ambient", "Ambient Sound")) {
        restoreAmbientVolumeIfNeeded();
        runtimeAudio.ambientEligibleAt = millis() + kAmbientResumeDelayMs;
    }
}

void scheduleAmbientResume(unsigned long delayMs = 0UL) {
    runtimeAudio.ambientEligibleAt = millis() + delayMs;
}

void applyWebUiLockNow() {
    if (webServer != nullptr) {
        webServer->setWebUiLocked(true);
    }
    if (appState != nullptr) {
        appState->setLastError("Web UI locked. Unlock it via MQTT command <baseTopic>/cmd/web_ui with payload unlock.");
    }
    if (mqttManager != nullptr) {
        mqttManager->publishState();
    }
}

void requestWebUiLockSequence() {
    runtimeAudio.webUiLockPending = true;
    runtimeAudio.pendingCommandAfterNotification = false;
    runtimeAudio.alarmActive = false;
    clearPreviewResumeState();
    restoreAmbientVolumeIfNeeded();

    if (audioPlayer != nullptr) {
        audioPlayer->stop();
    }

    if (!playConfiguredEffectSource("effect-shutdown", "Shutting Down")) {
        runtimeAudio.webUiLockPending = false;
        applyWebUiLockNow();
    }
}

void requestRestartSequence(const String& reason, bool factoryResetAfterRestart) {
    runtimeAudio.restartPending = true;
    runtimeAudio.restartFactoryReset = factoryResetAfterRestart;
    runtimeAudio.restartReason = reason;
    runtimeAudio.restartForcePending = true;
    runtimeAudio.restartForceAt = millis() + kRestartEffectForceDelayMs;
    runtimeAudio.pendingCommandAfterNotification = false;
    runtimeAudio.alarmActive = false;
    clearPreviewResumeState();
    restoreAmbientVolumeIfNeeded();

    if (audioPlayer != nullptr) {
        audioPlayer->stop();
    }

    const bool playedUpdateSuccess = reason == "ota" && playConfiguredEffectSource("effect-update-success", "Update Success");
    if (!playedUpdateSuccess && !playConfiguredEffectSource("effect-shutdown", "Restarting")) {
        if (factoryResetAfterRestart) {
            factoryResetRequested = true;
        }
        scheduleReboot(250);
    }
}

void handleOtaRestartRequest(const String& reason) {
    if (reason == "ota" && otaManager != nullptr) {
        String attemptedVersion = otaManager->pendingInstallVersion();
        attemptedVersion.trim();
        if (attemptedVersion.isEmpty()) {
            attemptedVersion = "unknown firmware";
        }
        storeRollbackPendingInfo(
            normalizedFirmwareVersionFromLabel(attemptedVersion),
            "Firmware did not complete post-update health confirmation; the bootloader restored the previous image.");
    }
    requestRestartSequence(reason, false);
}

uint8_t estimateBatteryPercent(float voltage) {
    const float normalized = (voltage - kBatteryPercentEmptyVoltage) / (kBatteryPercentFullVoltage - kBatteryPercentEmptyVoltage);
    const float clamped = normalized < 0.0f ? 0.0f : (normalized > 1.0f ? 1.0f : normalized);
    return static_cast<uint8_t>((clamped * 100.0f) + 0.5f);
}

String normalizedButtonAction(String action, const char* fallback) {
    action.trim();
    action.toLowerCase();
    action.replace('-', '_');
    action.replace(' ', '_');

    if (action == "none" || action == "previous" || action == "next" || action == "play_pause" ||
        action == "replay_current" || action == "stop" || action == "volume_up" || action == "volume_down" ||
        action == "ha_previous" || action == "ha_next" || action == "toggle_open" || action == "toggle_close" || action == "toggle_open_close") {
        return action;
    }

    return String(fallback);
}

String buttonActionFor(const PhysicalButtonState& button) {
    if (isTouchButtonProfileValue(button.profileValue)) {
        const String touchMotorAction = configuredTouchMotorAction(button.controlSlot);
        if (touchMotorAction != "none") {
            return touchMotorAction;
        }
    }

    if (settings == nullptr) {
        return button.controlSlot == 0 ? String(DefaultConfig::BUTTON1_DEFAULT_ACTION) : String(DefaultConfig::BUTTON2_DEFAULT_ACTION);
    }

    return button.controlSlot == 0
        ? normalizedButtonAction(settings->device.button1Action, DefaultConfig::BUTTON1_DEFAULT_ACTION)
        : normalizedButtonAction(settings->device.button2Action, DefaultConfig::BUTTON2_DEFAULT_ACTION);
}

int resolveMainControlInputIndex() {
    for (size_t index = 0; index < kMaxPeripheralInputProfiles; ++index) {
        const String profile = peripheralInputProfileFromUi(index);
        if (isTouchButtonProfileValue(profile) && configuredTouchMainControlEnabled(index)) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

int resolveFirstTouchInputIndex() {
    for (size_t index = 0; index < kMaxPeripheralInputProfiles; ++index) {
        if (isTouchButtonProfileValue(peripheralInputProfileFromUi(index))) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

int resolveSecondaryInputIndex(int excludedIndex, int fallbackIndex) {
    for (size_t index = 0; index < kMaxPeripheralInputProfiles; ++index) {
        if (static_cast<int>(index) == excludedIndex) {
            continue;
        }
        if (peripheralInputProfileFromUi(index) != "none") {
            return static_cast<int>(index);
        }
    }
    return fallbackIndex;
}

String buttonActionDisplayLabel(const String& action) {
    if (action == "none") {
        return "Disabled";
    }
    if (action == "previous") {
        return "Previous";
    }
    if (action == "next") {
        return "Next";
    }
    if (action == "play_pause") {
        return "Play/Pause";
    }
    if (action == "replay_current") {
        return "Replay";
    }
    if (action == "stop") {
        return "Stop";
    }
    if (action == "volume_up") {
        return "Volume +";
    }
    if (action == "volume_down") {
        return "Volume -";
    }
    if (action == "ha_previous") {
        return "HA Prev";
    }
    if (action == "ha_next") {
        return "HA Next";
    }
    if (action == "toggle_open") {
        return "Open";
    }
    if (action == "toggle_close") {
        return "Close";
    }
    if (action == "toggle_open_close") {
        return "Open/Close";
    }
    return action;
}

bool motorChannelConfigured(uint8_t channelIndex) {
    JsonDocument motorDoc;
    appendAugmentedMotorStatus(motorDoc.to<JsonObject>());
    JsonArrayConst channels = motorDoc["channels"].as<JsonArrayConst>();
    return channelIndex < channels.size() && !channels[channelIndex].isNull() && (channels[channelIndex]["configured"] | false);
}

int8_t resolveTouchMotorChannelIndex(const PhysicalButtonState& button) {
    if (button.controlSlot < 2 && motorChannelConfigured(button.controlSlot)) {
        return static_cast<int8_t>(button.controlSlot);
    }
    for (uint8_t channelIndex = 0; channelIndex < 2; ++channelIndex) {
        if (motorChannelConfigured(channelIndex)) {
            return static_cast<int8_t>(channelIndex);
        }
    }
    return -1;
}

struct TouchMotorRunConfig {
    bool forward = true;
    uint32_t durationMs = 5000;
    int8_t limitInputIndex = -1;
};

uint32_t clampPersistedMotorDuration(uint32_t durationMs);

TouchMotorRunConfig touchMotorRunConfigForDirection(uint8_t channelIndex, bool openDirection) {
    TouchMotorRunConfig config;
    if (settings == nullptr) {
        config.forward = openDirection;
        return config;
    }

    JsonDocument document;
    const String runtimeConfigJson = settings->ui.motorRuntimeConfig.isEmpty() ? String("{}") : settings->ui.motorRuntimeConfig;
    deserializeJson(document, runtimeConfigJson);
    JsonObjectConst channel = document[motorRuntimeChannelKey(channelIndex)].as<JsonObjectConst>();

    const auto applyDirectionConfig = [&](const char* directionKey, bool forwardDirection) {
        JsonObjectConst direction = channel[directionKey].as<JsonObjectConst>();
        config.forward = forwardDirection;
        config.durationMs = clampPersistedMotorDuration(direction["durationMs"] | config.durationMs);
        config.limitInputIndex = direction["limitInputIndex"].isNull()
            ? static_cast<int8_t>(-1)
            : static_cast<int8_t>(direction["limitInputIndex"].as<int>());
    };

    const String targetRole = openDirection ? String("opening") : String("closing");
    const String forwardRole = movementRoleFromRuntimeConfig(channel, true);
    const String backwardRole = movementRoleFromRuntimeConfig(channel, false);
    if (forwardRole == targetRole && backwardRole != targetRole) {
        applyDirectionConfig("forward", true);
    } else if (backwardRole == targetRole && forwardRole != targetRole) {
        applyDirectionConfig("backward", false);
    } else {
        applyDirectionConfig(openDirection ? "forward" : "backward", openDirection);
    }

    return config;
}

String learnedMotorStateForChannel(uint8_t channelIndex) {
    if (settings == nullptr) {
        return "unknown";
    }

    JsonDocument document;
    const String runtimeConfigJson = settings->ui.motorRuntimeConfig.isEmpty() ? String("{}") : settings->ui.motorRuntimeConfig;
    deserializeJson(document, runtimeConfigJson);
    JsonObjectConst channel = document[motorRuntimeChannelKey(channelIndex)].as<JsonObjectConst>();
    return normalizeMotorLearnedState(String(static_cast<const char*>(channel["learnedState"] | "unknown")));
}

bool runTouchMotorAction(const PhysicalButtonState& button, const String& action) {
    const int8_t channelIndex = resolveTouchMotorChannelIndex(button);
    if (channelIndex < 0) {
        if (appState != nullptr) {
            appState->setLastError("No configured motor channel is available for touch control.");
        }
        return false;
    }

    bool openDirection = true;
    if (action == "toggle_close") {
        openDirection = false;
    } else if (action == "toggle_open_close") {
        openDirection = learnedMotorStateForChannel(static_cast<uint8_t>(channelIndex)) != "open";
    }

    const TouchMotorRunConfig config = touchMotorRunConfigForDirection(static_cast<uint8_t>(channelIndex), openDirection);
    String error;
    if (!motorController.runChannel(static_cast<uint8_t>(channelIndex), config.forward, config.durationMs, config.limitInputIndex, error)) {
        if (appState != nullptr) {
            appState->setLastError(error.isEmpty() ? String("Motor command failed.") : error);
        }
        return false;
    }

    if (appState != nullptr) {
        appState->setLastError("");
    }
    if (mqttManager != nullptr) {
        mqttManager->publishState();
    }
    return true;
}

String buttonOverlayText(const String& action) {
    if ((action == "volume_up" || action == "volume_down") && settings != nullptr) {
        String label = "Volume ";
        label += settings->device.savedVolumePercent;
        label += "%";
        return label;
    }

    return buttonActionDisplayLabel(action);
}

void showS3ButtonActionOnDisplay(const String& action) {
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    if (displayManager != nullptr) {
        displayManager->showTemporaryCenterText(buttonOverlayText(action));
    }
#else
    (void)action;
#endif
}

void initializeButtons() {
    const unsigned long now = millis();
    const uint8_t defaultButton1Pin = DefaultConfig::BUTTON1_PIN;
    const uint8_t defaultButton2Pin = DefaultConfig::BUTTON2_PIN;
    button1.controlSlot = 0;
    button2.controlSlot = 1;
    const int mainControlIndex = resolveMainControlInputIndex();
    const int factoryResetTouchIndex = resolveFirstTouchInputIndex();
    const int primaryIndex = mainControlIndex >= 0 ? mainControlIndex : 0;
    const int secondaryIndex = factoryResetTouchIndex >= 0 && factoryResetTouchIndex != primaryIndex
        ? factoryResetTouchIndex
        : resolveSecondaryInputIndex(primaryIndex, 1);
    applyButtonProfileConfig(button1, static_cast<size_t>(primaryIndex), defaultButton1Pin);
    applyButtonProfileConfig(button2, static_cast<size_t>(secondaryIndex), defaultButton2Pin);
    button1.factoryResetTouch = button1.configuredIndex == factoryResetTouchIndex;
    button2.factoryResetTouch = button2.configuredIndex == factoryResetTouchIndex;

    if (isPhysicalButtonEnabled(button1)) {
        if (button1.touchSupported) {
            button1.touchRawValue = readNativeTouch(button1.pin);
            button1.touchBaselineValue = button1.touchRawValue;
            button1.lastSampledPressed = false;
        } else if (button1.limitSwitch) {
            pinMode(button1.pin, button1.limitSwitchUsesPullup ? INPUT_PULLUP : INPUT_PULLDOWN);
            button1.lastSampledPressed = false;
        } else {
            pinMode(button1.pin, INPUT_PULLDOWN);
            button1.lastSampledPressed = digitalRead(button1.pin) == HIGH;
        }
    } else {
        button1.lastSampledPressed = false;
    }
    button1.stablePressed = button1.lastSampledPressed;
    button1.holdResetTriggered = false;
    button1.pressedSinceAt = button1.stablePressed ? now : 0;
    button1.lastTransitionAt = now;

    if (isPhysicalButtonEnabled(button2)) {
        if (button2.touchSupported) {
            button2.touchRawValue = readNativeTouch(button2.pin);
            button2.touchBaselineValue = button2.touchRawValue;
            button2.lastSampledPressed = false;
        } else if (button2.limitSwitch) {
            pinMode(button2.pin, button2.limitSwitchUsesPullup ? INPUT_PULLUP : INPUT_PULLDOWN);
            button2.lastSampledPressed = false;
        } else {
            pinMode(button2.pin, INPUT_PULLDOWN);
            button2.lastSampledPressed = digitalRead(button2.pin) == HIGH;
        }
    } else {
        button2.lastSampledPressed = false;
    }
    button2.stablePressed = button2.lastSampledPressed;
    button2.holdResetTriggered = false;
    button2.pressedSinceAt = button2.stablePressed ? now : 0;
    button2.lastTransitionAt = now;

    for (size_t index = 0; index < kMaxPeripheralInputProfiles; ++index) {
        configureLimitSwitchInputPin(index);
    }

    publishInputSnapshots();
}

void rememberPlaybackSelection(const String& url, const String& label, const String& type, const String& source) {
    const String normalizedUrl = canonicalPlaybackReference(url);
    StorageTarget storageTarget = StorageTarget::Flash;
    String storagePath;
    const bool storageReference = parseStorageFileReference(normalizedUrl, storageTarget, storagePath);
    const String normalizedLabel = storageReference
        ? PlaybackText::normalizeTitle(label, storagePath)
        : PlaybackText::normalizeTitle(label, normalizedUrl);

    if (normalizedUrl.isEmpty()) {
        return;
    }

    if (playbackHistoryIndex >= 0 && playbackHistoryIndex < static_cast<int>(playbackHistoryCount)) {
        PlaybackHistoryEntry& current = playbackHistory[playbackHistoryIndex];
        if (current.url == normalizedUrl && current.type == type) {
            current.label = normalizedLabel;
            current.source = source;
            return;
        }
    }

    if (playbackHistoryIndex >= 0 && playbackHistoryIndex < static_cast<int>(playbackHistoryCount - 1)) {
        playbackHistoryCount = static_cast<size_t>(playbackHistoryIndex + 1);
    }

    if (playbackHistoryCount > 0) {
        PlaybackHistoryEntry& last = playbackHistory[playbackHistoryCount - 1];
        if (last.url == normalizedUrl && last.type == type) {
            last.label = normalizedLabel;
            last.source = source;
            playbackHistoryIndex = static_cast<int>(playbackHistoryCount - 1);
            return;
        }
    }

    if (playbackHistoryCount == kPlaybackHistoryLimit) {
        for (size_t index = 1; index < playbackHistoryCount; ++index) {
            playbackHistory[index - 1] = playbackHistory[index];
        }
        playbackHistoryCount -= 1;
    }

    playbackHistory[playbackHistoryCount] = {normalizedUrl, normalizedLabel, type, source};
    playbackHistoryCount += 1;
    playbackHistoryIndex = static_cast<int>(playbackHistoryCount - 1);
}

bool replayPlaybackEntry(const PlaybackHistoryEntry& entry, bool addToHistory) {
    if (entry.url.isEmpty()) {
        return false;
    }

    String error;
    return playRequest(entry.url, entry.label, entry.type, entry.source, error, addToHistory);
}

void clearPreviewResumeState() {
    runtimeAudio.previewResumePending = false;
    runtimeAudio.previewResumeVolume = 0;
    runtimeAudio.previewResumePositionSeconds = 0;
    runtimeAudio.previewResumeUrl = "";
    runtimeAudio.previewResumeLabel = "";
    runtimeAudio.previewResumeType = "";
    runtimeAudio.previewResumeSource = "";
    runtimeAudio.previewActiveSource = "";
}

bool queuePreviewResume(const AppStateSnapshot& snapshot, const String& previewSource) {
    const bool resumableAmbient = snapshot.playback.source == "effect-ambient";
    if ((snapshot.playback.state != "playing" && snapshot.playback.state != "buffering") ||
        snapshot.playback.url.isEmpty() ||
        (snapshot.playback.source.startsWith("effect-") && !resumableAmbient)) {
        clearPreviewResumeState();
        return false;
    }

    runtimeAudio.previewResumePending = true;
    runtimeAudio.previewResumeVolume = snapshot.playback.volumePercent;
    runtimeAudio.previewResumePositionSeconds = snapshot.playback.positionSeconds;
    runtimeAudio.previewResumeUrl = snapshot.playback.url;
    runtimeAudio.previewResumeLabel = snapshot.playback.title;
    runtimeAudio.previewResumeType = snapshot.playback.type;
    runtimeAudio.previewResumeSource = snapshot.playback.source;
    runtimeAudio.previewActiveSource = previewSource;
    return true;
}

bool resumeQueuedPreviewPlayback() {
    if (!runtimeAudio.previewResumePending || runtimeAudio.previewResumeUrl.isEmpty()) {
        clearPreviewResumeState();
        return false;
    }

    if (deferredActions != nullptr && deferredActions->playPending) {
        return false;
    }

    const String resumeUrl = runtimeAudio.previewResumeUrl;
    const String resumeLabel = runtimeAudio.previewResumeLabel;
    const String resumeType = runtimeAudio.previewResumeType;
    const String resumeSource = runtimeAudio.previewResumeSource;
    const uint32_t resumePositionSeconds = runtimeAudio.previewResumePositionSeconds;

    // Keep ambient auto-start from winning the same-loop race before the
    // queued stream/media resume is processed in the next deferred-actions pass.
    scheduleAmbientResume(kAmbientResumeDelayMs);

    String error;
    const bool queued = playRequest(resumeUrl, resumeLabel, resumeType, resumeSource, error, false);
    if (queued && deferredActions != nullptr) {
        deferredActions->playResumePositionSeconds = resumePositionSeconds;
    }
    if (!queued && !error.isEmpty()) {
        DebugLog.printf("[audio] preview resume skipped: %s\n", error.c_str());
    }
    return queued;
}

bool replayCurrentPlaybackSelection(bool addToHistory) {
    if (playbackHistoryIndex >= 0 && playbackHistoryIndex < static_cast<int>(playbackHistoryCount)) {
        return replayPlaybackEntry(playbackHistory[playbackHistoryIndex], addToHistory);
    }

    if (appState == nullptr) {
        return false;
    }

    const AppStateSnapshot snapshot = appState->snapshot();
    if (snapshot.playback.url.isEmpty()) {
        return false;
    }

    String error;
    return playRequest(snapshot.playback.url, snapshot.playback.title, snapshot.playback.type, snapshot.playback.source, error, addToHistory);
}

bool stepPlaybackHistory(int direction) {
    if (playbackHistoryCount == 0 || direction == 0) {
        return false;
    }

    int nextIndex = playbackHistoryIndex;
    if (nextIndex < 0 || nextIndex >= static_cast<int>(playbackHistoryCount)) {
        nextIndex = direction > 0 ? 0 : static_cast<int>(playbackHistoryCount - 1);
    } else {
        nextIndex = (nextIndex + direction + static_cast<int>(playbackHistoryCount)) % static_cast<int>(playbackHistoryCount);
    }

    playbackHistoryIndex = nextIndex;
    return replayPlaybackEntry(playbackHistory[nextIndex], false);
}

void applyVolumePercent(uint8_t volumePercent) {
    if (settings == nullptr || audioPlayer == nullptr || soundEffects == nullptr || deferredActions == nullptr) {
        return;
    }

    settings->device.savedVolumePercent = volumePercent;
    audioPlayer->setVolumePercent(volumePercent);
    soundEffects->setVolumePercent(volumePercent);
    deferredActions->pendingVolume = volumePercent;
    deferredActions->volumePending = false;
    deferredActions->volumeSavePending = true;
    deferredActions->volumeSaveAt = millis() + kVolumePersistDelayMs;
    if (mqttManager != nullptr) {
        mqttManager->publishState();
    }
}

void changeVolumeBy(int delta) {
    if (settings == nullptr) {
        return;
    }

    int nextVolume = static_cast<int>(settings->device.savedVolumePercent) + delta;
    if (nextVolume < 0) {
        nextVolume = 0;
    }
    if (nextVolume > 100) {
        nextVolume = 100;
    }

    applyVolumePercent(static_cast<uint8_t>(nextVolume));
}

bool executeButtonAction(const PhysicalButtonState& button, const String& action) {
    if (action == "none") {
        return false;
    }

    if (displayManager != nullptr) {
        displayManager->markActivity();
    }

    if (action == "previous") {
        return stepPlaybackHistory(-1);
    }
    if (action == "next") {
        return stepPlaybackHistory(1);
    }
    if (action == "play_pause") {
        const AppStateSnapshot snapshot = appState->snapshot();
        if (snapshot.playback.state == "playing" || snapshot.playback.state == "buffering") {
            audioPlayer->stop();
            if (mqttManager != nullptr) {
                mqttManager->publishState();
            }
            return true;
        }
        return replayCurrentPlaybackSelection(false);
    }
    if (action == "replay_current") {
        return replayCurrentPlaybackSelection(false);
    }
    if (action == "stop") {
        audioPlayer->stop();
        if (mqttManager != nullptr) {
            mqttManager->publishState();
        }
        return true;
    }
    if (action == "volume_up") {
        changeVolumeBy(DefaultConfig::BUTTON_VOLUME_STEP_PERCENT);
        return true;
    }
    if (action == "volume_down") {
        changeVolumeBy(-static_cast<int>(DefaultConfig::BUTTON_VOLUME_STEP_PERCENT));
        return true;
    }
    if (action == "ha_previous" || action == "ha_next") {
        if (mqttManager == nullptr) {
            return false;
        }
        return mqttManager->publishButtonActionEvent(button.label, button.pin, action == "ha_previous" ? "previous" : "next");
    }

    if (action == "toggle_open" || action == "toggle_close" || action == "toggle_open_close") {
        return runTouchMotorAction(button, action);
    }

    return false;
}

bool sampleButtonPressed(PhysicalButtonState& button) {
    if (!button.touchSupported) {
        button.touchRawValue = 0;
        button.touchBaselineValue = 0;
        button.touchPressCandidateCount = 0;
        button.touchReleaseCandidateCount = 0;
        const bool pinHigh = digitalRead(button.pin) == HIGH;
        if (!button.limitSwitch) {
            return pinHigh;
        }
        return pinHigh == button.limitSwitchActiveHigh;
    }

    const uint16_t rawValue = readNativeTouch(button.pin);
    button.touchRawValue = rawValue;
    if (button.touchBaselineValue == 0 && rawValue > 0) {
        button.touchBaselineValue = rawValue;
    }

    const uint16_t baseline = button.touchBaselineValue;
    const uint16_t requiredDeltaBasisPoints = static_cast<uint16_t>(200U - ((static_cast<uint32_t>(button.sensitivityPercent) * 150U) / 100U));
    const uint16_t requiredDelta = max<uint16_t>(32U, static_cast<uint16_t>((static_cast<uint32_t>(baseline) * requiredDeltaBasisPoints) / 10000U));
    const uint16_t absoluteDelta = rawValue > baseline ? static_cast<uint16_t>(rawValue - baseline) : static_cast<uint16_t>(baseline - rawValue);
    const bool candidatePressed = baseline > 0 && rawValue > 0 && absoluteDelta >= requiredDelta;

    if (!button.stablePressed && rawValue > 0) {
        const uint16_t settlingDelta = max<uint16_t>(6U, static_cast<uint16_t>(requiredDelta / 4U));
        if (button.touchBaselineValue == 0) {
            button.touchBaselineValue = rawValue;
        } else if (!candidatePressed && absoluteDelta <= settlingDelta) {
            button.touchBaselineValue = static_cast<uint16_t>((static_cast<uint32_t>(button.touchBaselineValue) * 31UL + rawValue + 16UL) / 32UL);
        }
    }

    if (candidatePressed) {
        if (button.touchPressCandidateCount < 255) {
            button.touchPressCandidateCount += 1;
        }
        button.touchReleaseCandidateCount = 0;
    } else {
        button.touchPressCandidateCount = 0;
        if (button.touchReleaseCandidateCount < 255) {
            button.touchReleaseCandidateCount += 1;
        }
    }

    if (button.stablePressed) {
        return button.touchReleaseCandidateCount < kTouchReleaseCandidateSamples;
    }
    return button.touchPressCandidateCount >= kTouchPressCandidateSamples;
}

void pollPhysicalButton(PhysicalButtonState& button) {
    if (!isPhysicalButtonEnabled(button)) {
        button.touchRawValue = 0;
        button.touchBaselineValue = 0;
        button.touchPressCandidateCount = 0;
        button.touchReleaseCandidateCount = 0;
        button.lastSampledPressed = false;
        button.stablePressed = false;
        button.holdResetTriggered = false;
        button.pressedSinceAt = 0;
        return;
    }

    const bool pressed = sampleButtonPressed(button);
    const unsigned long now = millis();

    if (pressed != button.lastSampledPressed) {
        button.lastSampledPressed = pressed;
        button.lastTransitionAt = now;
    }

    const unsigned long debounceMs = button.touchSupported ? kTouchDebounceMs : kButtonDebounceMs;
    if ((now - button.lastTransitionAt) < debounceMs || pressed == button.stablePressed) {
        serviceTouchHoldFactoryReset(button, now);
        return;
    }

    button.stablePressed = pressed;
    if (!button.stablePressed) {
        button.pressedSinceAt = 0;
        button.holdResetTriggered = false;
        return;
    }

    button.pressedSinceAt = now;
    button.holdResetTriggered = false;

    const String action = buttonActionFor(button);
    const bool handled = executeButtonAction(button, action);
    if (handled || action == "none") {
        showS3ButtonActionOnDisplay(action);
    }
    DebugLog.printf("[input] %s on GPIO%u mode=%s action=%s handled=%s\n",
                  button.label,
                  static_cast<unsigned>(button.pin),
                  button.touchSupported ? "touch" : (button.limitSwitch ? (button.normallyClosed ? "limit-nc" : "limit-no") : "gpio"),
                  action.c_str(),
                  handled ? "yes" : "no");
}

void pollPhysicalButtons() {
    pollPhysicalButton(button1);
    pollPhysicalButton(button2);
    publishInputSnapshots();
}

void enterLowBatteryDeepSleep(uint8_t batteryPercent, float voltage, const char* reason) {
    if (settings == nullptr) {
        return;
    }

    logicDevice.shuttingDown();
    const uint16_t wakeIntervalMinutes = settings->device.lowBatteryWakeIntervalMinutes;
    DebugLog.printf("[power] entering deep sleep reason=%s battery=%u%% voltage=%.3f wake_interval_min=%u\n",
                  reason,
                  static_cast<unsigned>(batteryPercent),
                  voltage,
                  static_cast<unsigned>(wakeIntervalMinutes));
    // A scheduled sleep is not a failed boot. Clear the rapid power-cycle
    // counter so waking or updating cannot accidentally trigger a reset.
    clearPowerCycleCounter();
    powerCycleCounterClearArmed = false;
    Serial.flush();

    if (audioPlayer != nullptr) {
        audioPlayer->stop();
    }
    if (displayManager != nullptr) {
        displayManager->powerOff();
    }

    delay(100);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    if (wakeIntervalMinutes > 0) {
        const uint64_t wakeIntervalUs = static_cast<uint64_t>(wakeIntervalMinutes) * 60ULL * 1000000ULL;
        esp_sleep_enable_timer_wakeup(wakeIntervalUs);
    }

    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    delay(100);
    DebugLog.service(true);
    esp_deep_sleep_start();
}

bool batteryReadingIsValidForSleepPolicy(const BatterySnapshot& battery) {
    return battery.rawAdc > 0 && isfinite(battery.voltage) &&
        battery.voltage >= kBatteryPolicyMinimumPlausibleVoltage;
}

void handleLowBatterySleepPolicy(const AppStateSnapshot& snapshot) {
    if (settings == nullptr || snapshot.ota.busy || !settings->device.lowBatterySleepEnabled) {
        if (settings == nullptr || !settings->device.lowBatterySleepEnabled) {
            wokeFromDeepSleep = false;
            lowBatteryWakeStartedAt = 0;
        }
        return;
    }

    // Keep the device reachable after boot/OTA so the desktop flasher can apply
    // corrected settings and the power-cycle guard can clear after stable uptime.
    if (millis() < kLowBatteryStartupGraceMs) {
        return;
    }

    // Zero/implausible ADC readings mean the configured divider is absent,
    // disconnected or assigned to the wrong pin. Treating that as a flat battery
    // makes mains-powered devices disappear into an endless deep-sleep cycle.
    if (!batteryReadingIsValidForSleepPolicy(snapshot.battery)) {
        wokeFromDeepSleep = false;
        lowBatteryWakeStartedAt = 0;
        return;
    }

    const uint8_t batteryPercent = estimateBatteryPercent(snapshot.battery.voltage);
    const uint8_t thresholdPercent = settings->device.lowBatterySleepThresholdPercent;
    if (batteryPercent > thresholdPercent) {
        wokeFromDeepSleep = false;
        lowBatteryWakeStartedAt = 0;
        return;
    }

    if (wokeFromDeepSleep) {
        if (lowBatteryWakeStartedAt == 0) {
            lowBatteryWakeStartedAt = millis();
            DebugLog.printf("[power] low-battery wake window started battery=%u%% threshold=%u%%\n",
                          static_cast<unsigned>(batteryPercent),
                          static_cast<unsigned>(thresholdPercent));
        }
        if (millis() - lowBatteryWakeStartedAt < kLowBatteryWakeWindowMs) {
            return;
        }
        enterLowBatteryDeepSleep(batteryPercent, snapshot.battery.voltage, "wake window elapsed while battery still low");
        return;
    }

    enterLowBatteryDeepSleep(batteryPercent, snapshot.battery.voltage, "battery threshold reached");
}

void publishOtaStateIfNeeded(const AppStateSnapshot& snapshot) {
    if (mqttManager == nullptr || !mqttManager->isConnected()) {
        return;
    }

    const String otaSignature = String(snapshot.ota.busy ? '1' : '0') + "|" + snapshot.ota.latestVersion + "|" +
        snapshot.ota.lastResult + "|" + snapshot.ota.lastError + "|" + snapshot.ota.phase + "|" + snapshot.ota.progressPercent;
    if (otaSignature == lastPublishedOtaSignature) {
        return;
    }

    const unsigned long now = millis();
    if ((now - lastOtaMqttPublishAt) < 500UL && snapshot.ota.busy) {
        return;
    }

    lastPublishedOtaSignature = otaSignature;
    lastOtaMqttPublishAt = now;
    mqttManager->publishState();
}

void pumpOtaDisplayProgress() {
    if (appState == nullptr || displayManager == nullptr) {
        return;
    }
    const AppStateSnapshot snapshot = appState->snapshot();
    displayManager->loop(snapshot);
    publishOtaStateIfNeeded(snapshot);
}

bool initializeRuntimeObjects() {
    if (appState == nullptr) {
        appState = allocatePreferPsram<AppState>();
    }
    if (settingsManager == nullptr) {
        settingsManager = allocatePreferPsram<SettingsManager>();
    }
    if (settings == nullptr) {
        settings = allocatePreferPsram<SettingsBundle>();
    }
    if (wifiManager == nullptr) {
        wifiManager = allocatePreferPsram<WiFiManager>();
    }
    if (batteryMonitor == nullptr) {
        batteryMonitor = allocatePreferPsram<BatteryMonitor>();
    }
    if (displayManager == nullptr) {
        displayManager = allocatePreferPsram<DisplayManager>();
    }
    if (audioPlayer == nullptr) {
        audioPlayer = allocatePreferPsram<AudioPlayerType>();
    }
    if (otaManager == nullptr) {
        otaManager = allocatePreferPsram<OtaManager>();
    }
    if (mqttManager == nullptr) {
        mqttManager = allocatePreferPsram<MqttManager>();
    }
    if (webServer == nullptr) {
        webServer = allocatePreferPsram<WebServerManager>();
    }
    if (soundEffects == nullptr) {
        soundEffects = allocatePreferPsram<SoundEffectsManager>();
    }
    if (deferredActions == nullptr) {
        deferredActions = allocatePreferPsram<DeferredActions>();
    }

    return appState != nullptr && settingsManager != nullptr && settings != nullptr && wifiManager != nullptr &&
           batteryMonitor != nullptr && displayManager != nullptr && audioPlayer != nullptr && otaManager != nullptr &&
           mqttManager != nullptr && webServer != nullptr && soundEffects != nullptr && deferredActions != nullptr;
}

void applyRuntimeSettings() {
    applyStorageSettings(*settings);
    initializeButtons();
    motorController.applySettings(*settings);
    appState->setDevice(settings->device.deviceName, settings->device.friendlyName, settings->usingSavedSettings);
    applyStatusLedConfig(settings->device.statusLedPin, settings->device.statusLedGreenPin, settings->device.statusLedBluePin, settings->device.statusLedType);
    applyWapeTriggerPin(settings->oled.displayType == "wape" ? settings->oled.wapeTriggerPin : 0);
    wifiManager->applySettings(*settings);
    batteryMonitor->applySettings(settings->battery, settings->battery.adcPin);
    displayManager->applySettings(settings->oled);
    if (!settings->audio.enabled) {
        if (activeAudioOutputEnabled) {
            audioPlayer->disableOutput();
            activeAudioOutputEnabled = false;
        }
    } else if (!activeAudioOutputEnabled || settings->audio.bclkPin != activeI2sBclkPin || settings->audio.wsPin != activeI2sWsPin || settings->audio.doutPin != activeI2sDoutPin) {
        if (audioPlayer->reconfigureOutputPins(settings->audio.bclkPin, settings->audio.wsPin, settings->audio.doutPin)) {
            activeI2sBclkPin = settings->audio.bclkPin;
            activeI2sWsPin = settings->audio.wsPin;
            activeI2sDoutPin = settings->audio.doutPin;
            activeAudioOutputEnabled = true;
        }
    }
#if APP_AUDIO_DIAGNOSTIC_TEST
    audioPlayer->setDirectLibraryVolume(DefaultConfig::AUDIO_DIAGNOSTIC_LIBRARY_VOLUME);
#else
    const AppStateSnapshot snapshot = appState->snapshot();
    if (snapshot.playback.source == "effect-ambient") {
        runtimeAudio.ambientPreviousVolume = settings->device.savedVolumePercent;
        audioPlayer->setVolumePercent(ambientPlaybackVolumePercent());
        runtimeAudio.ambientVolumeApplied = true;
        runtimeAudio.effectVolumeApplied = false;
    } else if (snapshot.playback.source.startsWith("effect-")) {
        audioPlayer->setVolumePercent(effectVolumeForSource(snapshot.playback.source));
        runtimeAudio.effectVolumeApplied = true;
        runtimeAudio.ambientVolumeApplied = false;
    } else {
        audioPlayer->setVolumePercent(settings->device.savedVolumePercent);
        runtimeAudio.ambientVolumeApplied = false;
        runtimeAudio.effectVolumeApplied = false;
    }
#endif
    audioPlayer->setEqualizer(settings->audio.equalizerPreset,
                              settings->audio.equalizerLowDb,
                              settings->audio.equalizerPresenceDb,
                              settings->audio.equalizerHighDb);
    soundEffects->applySettings(*settings);
    mqttManager->applySettings(*settings);
    otaManager->applySettings(*settings);
    runtimeAudio.ambientEligibleAt = millis() + kAmbientResumeDelayMs;
    runtimeAudio.updateAvailableHandled = false;
    sampleSystemMetrics();

    // Repaint the current status immediately on the newly selected LED pin so
    // the pin change is visible right after Save Device Settings.
    updateStatusLedForNetwork(wifiManager->isConnected(), wifiManager->isApMode());
}

void preserveLearnedMotorStates(SettingsBundle& target, const SettingsBundle& source) {
    DynamicJsonDocument sourceDoc(motorRuntimeConfigJsonCapacity(source.ui.motorRuntimeConfig));
    if (!deserializeMotorRuntimeConfigDocument(source.ui.motorRuntimeConfig, sourceDoc)) {
        return;
    }

    DynamicJsonDocument targetDoc(motorRuntimeConfigJsonCapacity(target.ui.motorRuntimeConfig));
    if (!deserializeMotorRuntimeConfigDocument(target.ui.motorRuntimeConfig, targetDoc)) {
        return;
    }

    bool changed = false;
    for (uint8_t channelIndex = 0; channelIndex < 2; ++channelIndex) {
        const char* channelKey = motorRuntimeChannelKey(channelIndex);
        JsonObjectConst sourceChannel = sourceDoc[channelKey].as<JsonObjectConst>();
        JsonObject targetChannel = targetDoc[channelKey].as<JsonObject>();
        if (targetChannel.isNull()) {
            targetChannel = targetDoc[channelKey].to<JsonObject>();
        }

        for (const char* directionKey : {"forward", "backward"}) {
            JsonObjectConst sourceDirection = sourceChannel[directionKey].as<JsonObjectConst>();
            JsonObjectConst targetDirection = targetChannel[directionKey].as<JsonObjectConst>();
            if (!sourceDirection.isNull() && targetDirection.isNull()) {
                targetChannel[directionKey].set(sourceDirection);
                changed = true;
            }
        }

        const String sourceState = normalizeMotorLearnedState(String(static_cast<const char*>(sourceChannel["learnedState"] | "unknown")));
        if (sourceState == "unknown") {
            continue;
        }

        const String targetState = normalizeMotorLearnedState(String(static_cast<const char*>(targetChannel["learnedState"] | "unknown")));
        if (targetState != "unknown") {
            continue;
        }

        if (targetChannel["forward"].isNull() && targetChannel["backward"].isNull()) {
            continue;
        }

        targetChannel["learnedState"] = sourceState;
        changed = true;
    }

    JsonObjectConst sourceTouchButtons = sourceDoc["touchButtons"].as<JsonObjectConst>();
    JsonObjectConst targetTouchButtons = targetDoc["touchButtons"].as<JsonObjectConst>();
    if (!sourceTouchButtons.isNull() && targetTouchButtons.isNull()) {
        targetDoc["touchButtons"].set(sourceTouchButtons);
        changed = true;
    }

    if (!changed) {
        return;
    }

    String serialized;
    serializeJson(targetDoc.as<JsonObjectConst>(), serialized);
    target.ui.motorRuntimeConfig = serialized;
}

bool extractMotorRuntimeConfigFromJson(JsonVariantConst root, String& rawConfig) {
    if (!root.is<JsonObjectConst>()) {
        return false;
    }

    JsonObjectConst ui = root["ui"].as<JsonObjectConst>();
    if (ui.isNull()) {
        return false;
    }

    JsonVariantConst value = ui["motorRuntimeConfig"];
    if (value.isNull()) {
        return false;
    }

    rawConfig = "";
    if (value.is<JsonObjectConst>() || value.is<JsonArrayConst>()) {
        serializeJson(value, rawConfig);
    } else {
        const char* rawValue = value.as<const char*>();
        if (rawValue != nullptr) {
            rawConfig = rawValue;
        } else {
            String serializedValue;
            serializeJson(value, serializedValue);
            if (serializedValue.length() >= 2 && serializedValue.charAt(0) == '"' && serializedValue.charAt(serializedValue.length() - 1) == '"') {
                DynamicJsonDocument decodedValue(serializedValue.length() * 2U + 64U);
                if (!deserializeJson(decodedValue, serializedValue) && decodedValue.is<const char*>()) {
                    rawConfig = decodedValue.as<const char*>();
                }
            }
            if (rawConfig.isEmpty()) {
                rawConfig = serializedValue;
            }
        }
    }

    if (rawConfig.isEmpty()) {
        return false;
    }
    rawConfig.trim();
    return !rawConfig.isEmpty();
}

bool saveSettingsFromJson(JsonVariantConst root, String& error) {
    // The Wi-Fi power button sends exactly these two fields. Do not reinitialize
    // motors, displays, storage or audio for a radio-power-only adjustment.
    const JsonObjectConst object = root.as<JsonObjectConst>();
    const JsonObjectConst wifi = root["wifi"].as<JsonObjectConst>();
    const bool powerOnly = object.size() == 1 && wifi.size() > 0 &&
        wifi.size() == static_cast<size_t>(wifi["staTxPowerDbm"].is<float>()) +
                       static_cast<size_t>(wifi["apTxPowerDbm"].is<float>());
    String postedMotorRuntimeConfig;
    const bool hasPostedMotorRuntimeConfig = extractMotorRuntimeConfigFromJson(root, postedMotorRuntimeConfig);
    postedMotorRuntimeConfig.trim();

    SettingsBundle updated = *settings;
    if (!settingsManager->updateFromJson(updated, root, error)) {
        return false;
    }
    if (hasPostedMotorRuntimeConfig) {
        updated.ui.motorRuntimeConfig = postedMotorRuntimeConfig.isEmpty() ? String("{}") : postedMotorRuntimeConfig;
    } else {
        preserveLearnedMotorStates(updated, *settings);
    }
    updated.usingSavedSettings = true;
    settingsManager->save(updated);

    SettingsBundle persisted = settingsManager->load();
    if (hasPostedMotorRuntimeConfig && persisted.ui.motorRuntimeConfig != updated.ui.motorRuntimeConfig) {
        persisted.ui.motorRuntimeConfig = updated.ui.motorRuntimeConfig;
        persisted.usingSavedSettings = true;
        settingsManager->save(persisted);
        persisted = settingsManager->load();
    }

    *settings = persisted;
    appState->setDevice(settings->device.deviceName, settings->device.friendlyName, true);
    deferredActions->pendingSettings = persisted;
    deferredActions->settingsApplyWifiPowerOnly = powerOnly &&
        (!deferredActions->settingsApplyPending || deferredActions->settingsApplyWifiPowerOnly);
    deferredActions->settingsApplyPending = true;
    return true;
}

uint32_t cloneConfigurationCrc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t index = 0; index < length; ++index) {
        crc ^= data[index];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1U) ^ (0xEDB88320UL & (0U - (crc & 1U)));
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}

void resetCloneProvisioningReceiver() {
    cloneProvisioning.commandLine = "";
    cloneProvisioning.payload = "";
    cloneProvisioning.expectedLength = 0;
    cloneProvisioning.expectedCrc32 = 0;
    cloneProvisioning.receiving = false;
}

void applyClonedConfiguration() {
    const uint32_t actualCrc = cloneConfigurationCrc32(
        reinterpret_cast<const uint8_t*>(cloneProvisioning.payload.c_str()),
        cloneProvisioning.payload.length());
    if (actualCrc != cloneProvisioning.expectedCrc32) {
        DebugLog.printf("[clone] error=configuration checksum mismatch expected=%08lX actual=%08lX\n",
                      static_cast<unsigned long>(cloneProvisioning.expectedCrc32),
                      static_cast<unsigned long>(actualCrc));
        resetCloneProvisioningReceiver();
        return;
    }

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    DynamicJsonDocument document(cloneProvisioning.payload.length() * 2U + 2048U);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    const DeserializationError parseError = deserializeJson(document, cloneProvisioning.payload);
    if (parseError || document.overflowed() || !document.is<JsonObject>()) {
        DebugLog.printf("[clone] error=invalid configuration json detail=%s\n", parseError.c_str());
        resetCloneProvisioningReceiver();
        return;
    }

    SettingsBundle cloned = settingsManager->defaults();
    String error;
    if (!settingsManager->updateFromJson(cloned, document.as<JsonVariantConst>(), error)) {
        DebugLog.printf("[clone] error=configuration rejected detail=%s\n", error.c_str());
        resetCloneProvisioningReceiver();
        return;
    }

    // This is an explicit setup operation from the owner-facing Android app.
    // Preserve its complete snapshot, including the chosen device and MQTT
    // identities, so the device web UI reflects the wizard after reboot.
    cloned.usingSavedSettings = true;
    settingsManager->save(cloned);
    *settings = settingsManager->load();

    DebugLog.printf("[clone] configuration applied identity=%s mqttClientId=%s mqttBaseTopic=%s\n",
                  settings->device.deviceName.c_str(),
                  settings->mqtt.clientId.c_str(),
                  settings->mqtt.baseTopic.c_str());
    DebugLog.println("[clone] restarting target");
    Serial.flush();
    resetCloneProvisioningReceiver();
    scheduleReboot(500);
}

void beginCloneProvisioningPayload(const String& command) {
    const String arguments = command.substring(strlen(kCloneConfigurationCommand));
    const int separator = arguments.indexOf(' ');
    if (separator <= 0) {
        DebugLog.println("[clone] error=invalid provisioning header");
        return;
    }

    const size_t length = static_cast<size_t>(strtoul(arguments.substring(0, separator).c_str(), nullptr, 10));
    const uint32_t crc = static_cast<uint32_t>(strtoul(arguments.substring(separator + 1).c_str(), nullptr, 16));
    if (length == 0 || length > kCloneConfigurationMaxBytes) {
        DebugLog.printf("[clone] error=configuration length must be 1..%u bytes\n",
                      static_cast<unsigned>(kCloneConfigurationMaxBytes));
        return;
    }

    cloneProvisioning.payload = "";
    if (!cloneProvisioning.payload.reserve(length + 1U)) {
        DebugLog.println("[clone] error=unable to allocate configuration buffer");
        return;
    }
    cloneProvisioning.expectedLength = length;
    cloneProvisioning.expectedCrc32 = crc;
    cloneProvisioning.receiving = true;
    DebugLog.printf("[clone] receiving configuration bytes=%u\n", static_cast<unsigned>(length));
}

void serviceCloneProvisioningSerial() {
    while (Serial.available() > 0) {
        const char value = static_cast<char>(Serial.read());
        if (cloneProvisioning.receiving) {
            cloneProvisioning.payload += value;
            if (cloneProvisioning.payload.length() == cloneProvisioning.expectedLength) {
                applyClonedConfiguration();
            }
            continue;
        }

        if (value == '\r') {
            continue;
        }
        if (value != '\n') {
            if (cloneProvisioning.commandLine.length() < 160U) {
                cloneProvisioning.commandLine += value;
            } else {
                cloneProvisioning.commandLine = "";
            }
            continue;
        }

        const String command = cloneProvisioning.commandLine;
        cloneProvisioning.commandLine = "";
        if (command.startsWith(kCloneConfigurationCommand)) {
            beginCloneProvisioningPayload(command);
        } else if (command == "ELMA_CLONE_PING") {
            DebugLog.printf("[clone] provisioning ready identity=%s\n", settings->device.deviceName.c_str());
            Serial.flush();
        } else if (command == "ELMA_CONFIG_SNAPSHOT") {
            JsonDocument settingsDocument;
            settingsManager->toJson(*settings, settingsDocument.to<JsonObject>());
            String settingsJson;serializeJson(settingsDocument,settingsJson);
            JsonDocument logicDocument;logicDevice.snapshot(logicDocument,true);
            String logicJson;serializeJson(logicDocument,logicJson);
            DebugLog.printf("[elma-config] schema=1 firmware=%s settingsBytes=%u logicsBytes=%u\n", APP_VERSION,
                static_cast<unsigned>(settingsJson.length()),static_cast<unsigned>(logicJson.length()));
            Serial.print("[elma-config-settings] ");Serial.println(settingsJson);
            Serial.print("[elma-config-logics] ");Serial.println(logicJson);
            DebugLog.println("[elma-config] end");
            Serial.flush();
        } else if (command == "ELMA_NETWORK_STATUS") {
            // Requested after USB re-enumeration, when boot-time log messages
            // may already have passed before Android opened the runtime port.
            if (wifiManager != nullptr && wifiManager->isConnected()) {
                DebugLog.printf("[elma-network] mode=STA ssid='%s' ip=%s\n",
                    WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
            } else if (wifiManager != nullptr && wifiManager->isApMode()) {
                DebugLog.printf("[elma-network] mode=AP ssid='%s' ip=%s\n",
                    wifiManager->apSsid().c_str(), WiFi.softAPIP().toString().c_str());
            }
            Serial.flush();
        } else if (command == "ELMA_DIAGNOSTICS") {
            DebugLog.printf("[health] uptime=%lu heap=%u min_heap=%u largest=%u wifi=%d rssi=%d mqtt=%d\n",
                millis(), ESP.getFreeHeap(), ESP.getMinFreeHeap(),
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                static_cast<int>(WiFi.status()), WiFi.RSSI(), mqttManager != nullptr && mqttManager->isConnected());
        }
    }
}

bool saveMotorRuntimeConfigFromJson(JsonVariantConst root, String& error) {
    if (settings == nullptr || settingsManager == nullptr) {
        error = "Motor settings are unavailable.";
        return false;
    }

    String rawConfig;
    if (root.is<JsonObjectConst>() || root.is<JsonArrayConst>()) {
        serializeJson(root, rawConfig);
    } else {
        const char* rawValue = root.as<const char*>();
        if (rawValue != nullptr) {
            rawConfig = rawValue;
        }
    }

    rawConfig.trim();
    if (rawConfig.isEmpty() || rawConfig == "{}") {
        error = "Motor settings payload is invalid.";
        return false;
    }

    SettingsBundle updated = *settings;
    updated.ui.motorRuntimeConfig = rawConfig;
    preserveLearnedMotorStates(updated, *settings);
    updated.usingSavedSettings = true;

    settingsManager->save(updated);
    *settings = settingsManager->load();
    if (settings->ui.motorRuntimeConfig != updated.ui.motorRuntimeConfig) {
        settings->ui.motorRuntimeConfig = updated.ui.motorRuntimeConfig;
        settings->usingSavedSettings = true;
        settingsManager->save(*settings);
        *settings = settingsManager->load();
    }

    appState->setDevice(settings->device.deviceName, settings->device.friendlyName, true);
    applyRuntimeSettings();
    mqttManager->publishState();
    error = "";
    return true;
}

bool playRequest(const String& url, const String& label, const String& type, const String& source, String& error, bool addToHistory) {
    if (type=="tts-offline") {JsonDocument descriptor;descriptor["text"]=url;descriptor["language"]="en";return queueAudioSource(descriptor.as<JsonVariantConst>(),error);}
    if (otaManager != nullptr && otaManager->isFirmwareTransferActive()) {
        error = "Wait for the firmware update to finish before starting playback.";
        return false;
    }
    StorageTarget storageTarget = StorageTarget::Flash;
    String storagePath;
    const bool storageReference = parseStorageFileReference(url, storageTarget, storagePath);
    String normalizedUrl;
    if (!storageReference) {
        normalizedUrl = PlaybackText::normalizeUrl(url);
        if (normalizedUrl.isEmpty()) {
            error = "URL is required";
            return false;
        }
    }
#if APP_AUDIO_DIAGNOSTIC_TEST
    if (source != "diagnostic-test") {
        error = "Diagnostic audio test build only plays the configured MAX98357A test stream.";
        return false;
    }
#endif
#ifdef APP_DISABLE_AUDIO
    error = "Audio disabled in diagnostic build";
    return false;
#endif
    deferredActions->playFromStorage = storageReference;
    deferredActions->playStorageTarget = storageTarget;
    deferredActions->playStoragePath = storagePath;
    deferredActions->playUrl = storageReference ? String(url) : normalizedUrl;
    deferredActions->playLabel = storageReference
        ? PlaybackText::normalizeTitle(label, storagePath)
        : PlaybackText::normalizeTitle(label, normalizedUrl);
    deferredActions->playType = type;
    deferredActions->playAddToHistory = addToHistory;
    if (!source.isEmpty()) {
        deferredActions->playSource = source;
    } else {
        deferredActions->playSource = type == "tts" ? "home-assistant" : "manual";
    }
    deferredActions->playPending = true;
    deferredActions->stopPending = false;
    return true;
}

String playbackStartFailureMessage(const DeferredActions& actions) {
    if (actions.playLabel.length() > 0) {
        return String("Failed to start ") + actions.playLabel + ". Check that the selected audio file exists and is playable.";
    }
    if (actions.playFromStorage && actions.playStoragePath.length() > 0) {
        return String("Failed to start ") + actions.playStoragePath + ". Check that the selected audio file exists and is playable.";
    }
    if (actions.playUrl.length() > 0) {
        return String("Failed to start ") + actions.playUrl + ". Check that the selected audio file exists and is playable.";
    }
    return "Failed to start playback. Check that the selected audio file exists and is playable.";
}

void stopAlarmPlayback() {
    runtimeAudio.alarmActive = false;
    if (audioPlayer != nullptr && appState != nullptr) {
        const AppStateSnapshot snapshot = appState->snapshot();
        if (snapshot.playback.source == "effect-alarm") {
            audioPlayer->stop();
        }
    }
    scheduleAmbientResume();
}

void startAlarmPlayback() {
    runtimeAudio.alarmActive = true;
    clearPreviewResumeState();
    restoreAmbientVolumeIfNeeded();
    if (audioPlayer != nullptr) {
        audioPlayer->stop();
    }
    if (!playConfiguredEffectSource("effect-alarm", "Alarm")) {
        runtimeAudio.alarmActive = false;
    }
}

void executePlaybackCommand(const PlaybackCommand& command);

bool payloadEnablesSwitch(const String& value, bool& enabled) {
    if (value.isEmpty()) {
        return false;
    }

    String normalized = value;
    normalized.trim();
    normalized.toLowerCase();

    if (normalized == "on" || normalized == "true" || normalized == "1" || normalized == "enable" || normalized == "enabled") {
        enabled = true;
        return true;
    }

    if (normalized == "off" || normalized == "false" || normalized == "0" || normalized == "disable" || normalized == "disabled") {
        enabled = false;
        return true;
    }

    return false;
}

uint32_t clampPersistedMotorDuration(uint32_t durationMs) {
    if (durationMs < 100U) {
        return 5000U;
    }
    return durationMs > 600000U ? 600000U : durationMs;
}

size_t motorRuntimeConfigJsonCapacity(const String& rawConfig) {
    return MotorRuntimeConfig::jsonCapacity(rawConfig);
}

bool deserializeMotorRuntimeConfigDocument(const String& rawConfig, DynamicJsonDocument& document) {
    return MotorRuntimeConfig::deserializeDocument(rawConfig, document);
}

bool updateMotorRuntimeDurationSetting(int8_t channelIndex, bool forward, uint32_t durationMs, String& error) {
    if (settings == nullptr || settingsManager == nullptr || mqttManager == nullptr) {
        error = "Motor settings are unavailable.";
        return false;
    }
    if (channelIndex < 0 || channelIndex > 1) {
        error = "Unknown motor channel.";
        return false;
    }

    DynamicJsonDocument document(motorRuntimeConfigJsonCapacity(settings->ui.motorRuntimeConfig));
    if (!deserializeMotorRuntimeConfigDocument(settings->ui.motorRuntimeConfig, document)) {
        error = "Saved motor settings are invalid.";
        return false;
    }
    const char* channelKey = channelIndex == 0 ? "a" : "b";
    JsonObject channel = document[channelKey].as<JsonObject>();
    if (channel.isNull()) {
        channel = document[channelKey].to<JsonObject>();
    }
    const String targetRole = forward ? String("opening") : String("closing");
    const String forwardRole = movementRoleFromRuntimeConfig(channel, true);
    const String backwardRole = movementRoleFromRuntimeConfig(channel, false);
    const char* directionKey = nullptr;
    if (forwardRole == targetRole && backwardRole != targetRole) {
        directionKey = "forward";
    } else if (backwardRole == targetRole && forwardRole != targetRole) {
        directionKey = "backward";
    } else {
        directionKey = forward ? "forward" : "backward";
    }
    JsonObject direction = channel[directionKey].as<JsonObject>();
    if (direction.isNull()) {
        direction = channel[directionKey].to<JsonObject>();
    }
    direction["durationMs"] = clampPersistedMotorDuration(durationMs);

    String serialized;
    serializeJson(document.as<JsonObjectConst>(), serialized);
    settings->ui.motorRuntimeConfig = serialized;
    settingsManager->save(*settings);
    mqttManager->applySettings(*settings);
    error = "";
    return true;
}

void persistLearnedMotorStateIfNeeded() {
    if (settings == nullptr || settingsManager == nullptr || !motorController.available()) {
        return;
    }

    JsonDocument statusDoc;
    appendAugmentedMotorStatus(statusDoc.to<JsonObject>());
    if (!(statusDoc["available"] | false)) {
        return;
    }

    DynamicJsonDocument runtimeConfigDoc(motorRuntimeConfigJsonCapacity(settings->ui.motorRuntimeConfig));
    if (!deserializeMotorRuntimeConfigDocument(settings->ui.motorRuntimeConfig, runtimeConfigDoc)) {
        return;
    }
    JsonArrayConst channels = statusDoc["channels"].as<JsonArrayConst>();
    bool changed = false;
    for (uint8_t channelIndex = 0; channelIndex < 2 && channelIndex < channels.size(); ++channelIndex) {
        JsonObjectConst channelStatus = channels[channelIndex].as<JsonObjectConst>();
        if (channelStatus.isNull()) {
            continue;
        }

        String learnedState = normalizeMotorLearnedState(String(static_cast<const char*>(channelStatus["learnedState"] | "unknown")));
        if (learnedState == "unknown") {
            continue;
        }

        JsonObject channelConfig = runtimeConfigDoc[motorRuntimeChannelKey(channelIndex)].as<JsonObject>();
        if (channelConfig.isNull()) {
            channelConfig = runtimeConfigDoc[motorRuntimeChannelKey(channelIndex)].to<JsonObject>();
        }
        const String currentState = normalizeMotorLearnedState(String(static_cast<const char*>(channelConfig["learnedState"] | "unknown")));
        if (currentState == learnedState) {
            continue;
        }

        channelConfig["learnedState"] = learnedState;
        changed = true;
    }

    if (!changed) {
        return;
    }

    String serialized;
    serializeJson(runtimeConfigDoc.as<JsonObjectConst>(), serialized);
    settings->ui.motorRuntimeConfig = serialized;
    settingsManager->save(*settings);
}

void publishMotorStateIfNeeded() {
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    const uint32_t currentVersion = motorController.stateVersion();
    if (currentVersion == lastProcessedMotorStateVersion) {
        return;
    }

    lastProcessedMotorStateVersion = currentVersion;
    persistLearnedMotorStateIfNeeded();

    if (mqttManager == nullptr || !mqttManager->isConnected()) {
        return;
    }

    lastPublishedMotorStateVersion = currentVersion;
    mqttManager->publishState();
    // Async MQTT can still have the first state packet in flight when a motor
    // output changes at the exact end of its run. Repeat the retained settled
    // state shortly afterward so Home Assistant reliably consumes open/closed.
    motorStateRepublishRemaining = 2;
    motorStateRepublishAt = millis() + 300UL;
}

void serviceMotorStateRepublish() {
    if (motorStateRepublishRemaining == 0 || mqttManager == nullptr || !mqttManager->isConnected()) {
        return;
    }
    const uint32_t now = millis();
    if (static_cast<int32_t>(now - motorStateRepublishAt) < 0) {
        return;
    }
    mqttManager->publishState();
    --motorStateRepublishRemaining;
    motorStateRepublishAt = now + 900UL;
}

void handleMqttCommand(const PlaybackCommand& command) {
    if (!command.skipNotificationCue &&
        !settings->effects.notificationFile.isEmpty() &&
        (command.action == "play" || command.action == "tts" || command.action == "alarm_start" || command.action == "ota_install" ||
         command.action == "ota_install_latest" || command.action == "ota_install_selected")) {
        runtimeAudio.pendingCommand = command;
        runtimeAudio.pendingCommand.skipNotificationCue = true;
        runtimeAudio.pendingCommandAfterNotification = playConfiguredEffectSource("effect-notification", "MQTT Notification");
        if (runtimeAudio.pendingCommandAfterNotification) {
            return;
        }
    }
    executePlaybackCommand(command);
}

void executePlaybackCommand(const PlaybackCommand& command) {
    if (command.action == "ota_check") {
        String error;
        if (otaManager != nullptr && otaManager->triggerReleaseRefresh(error)) {
            appState->setLastError("");
        } else {
            const String message = error.isEmpty() ? String("OTA release refresh request was rejected.") : error;
            if (otaManager != nullptr) {
                otaManager->reportError(message);
            }
            appState->setLastError(message);
        }
        mqttManager->publishState();
    } else if (command.action == "ota_select_version") {
        String error;
        if (otaManager != nullptr && otaManager->selectReleaseOption(command.label, error)) {
            appState->setLastError("");
        } else {
            const String message = error.isEmpty() ? String("OTA firmware selection was rejected.") : error;
            if (otaManager != nullptr) {
                otaManager->reportError(message);
            }
            appState->setLastError(message);
        }
        mqttManager->publishState();
    } else if (command.action == "ota_auto_update") {
        bool enabled = false;
        if (!payloadEnablesSwitch(command.payload, enabled)) {
            const String message = "OTA auto-update MQTT payload must be ON or OFF.";
            appState->setLastError(message);
            mqttManager->publishState();
            return;
        }

        deferredActions->pendingSettings = *settings;
        deferredActions->pendingSettings.ota.autoUpdate = enabled;
        if (enabled) {
            deferredActions->pendingSettings.ota.autoCheck = true;
        }
        deferredActions->settingsApplyPending = true;
        deferredActions->settingsApplyWifiPowerOnly = false;
        appState->setLastError("");
    } else if (command.action == "ota_install_latest") {
        if (otaManager != nullptr && otaManager->triggerCheck(true)) {
            appState->setLastError("");
        } else {
            const String message = "OTA install request was rejected.";
            if (otaManager != nullptr) {
                otaManager->reportError(message);
            }
            appState->setLastError(message);
        }
        mqttManager->publishState();
    } else if (command.action == "ota_install_selected") {
        String error;
        if (otaManager != nullptr && otaManager->triggerInstallSelected(error)) {
            appState->setLastError("");
        } else {
            const String message = error.isEmpty() ? String("OTA install request was rejected.") : error;
            if (otaManager != nullptr) {
                otaManager->reportError(message);
            }
            appState->setLastError(message);
        }
        mqttManager->publishState();
    } else if (command.action == "ota_install") {
        String error;
        if (otaManager != nullptr && otaManager->triggerInstallVersion(command.version, command.assetName, error)) {
            appState->setLastError("");
        } else {
            const String message = error.isEmpty() ? String("OTA install request was rejected.") : error;
            if (otaManager != nullptr) {
                otaManager->reportError(message);
            }
            appState->setLastError(message);
        }
        mqttManager->publishState();
    } else if (command.action == "alarm_start") {
        startAlarmPlayback();
        mqttManager->publishState();
    } else if (command.action == "alarm_stop") {
        stopAlarmPlayback();
        mqttManager->publishState();
    } else if (command.action == "notify") {
        if (!tryOverlayConfiguredEffect(settings->effects.notificationFile, 35, effectVolumeForSource("effect-notification"))) {
            playConfiguredEffectSource("effect-notification", command.payload.isEmpty() ? "MQTT Notification" : command.payload);
        }
    } else if (command.action == "sd_remount") {
        String message;
        if (storageBusy(StorageTarget::Sd)) {
            message = "SD card is busy. Stop playback or transfers before remounting.";
        } else if (remountStorageBackend(StorageTarget::Sd, *settings)) {
            appState->setLastError("");
            mqttManager->publishState();
            return;
        } else {
            message = "SD card remount failed.";
        }

        appState->setLastError(message);
        mqttManager->publishState();
    } else if (command.action == "reboot") {
        appState->setLastError("");
        requestRestartSequence("mqtt", false);
        mqttManager->publishState();
    } else if (command.action == "web_ui_lock") {
        if (webServer != nullptr) {
            webServer->setWebUiLocked(true);
        }
        appState->setLastError("Web UI locked. Send MQTT payload 'unlock' to <baseTopic>/cmd/web_ui to restore access.");
        mqttManager->publishState();
    } else if (command.action == "web_ui_unlock") {
        if (webServer != nullptr) {
            webServer->setWebUiLocked(false);
        }
        appState->setLastError("");
        mqttManager->publishState();
    } else if (command.action == "stop" || command.action == "pause") {
        stopAlarmPlayback();
        audioPlayer->stop();
        mqttManager->publishState();
    } else if (command.action == "display_trigger") {
        triggerWapeDisplay();
    } else if (command.action == "motor_run") {
        String error;
        if (motorController.runChannel(static_cast<uint8_t>(command.motorChannelIndex), command.motorForward, command.motorDurationMs, command.motorLimitInputIndex, error)) {
            appState->setLastError("");
        } else {
            appState->setLastError(error.isEmpty() ? String("Motor command failed.") : error);
        }
        mqttManager->publishState();
        lastProcessedMotorStateVersion = motorController.stateVersion();
        lastPublishedMotorStateVersion = motorController.stateVersion();
    } else if (command.action == "motor_set_duration") {
        String error;
        if (updateMotorRuntimeDurationSetting(command.motorChannelIndex, command.motorForward, command.motorDurationMs, error)) {
            appState->setLastError("");
        } else {
            appState->setLastError(error.isEmpty() ? String("Motor duration update failed.") : error);
        }
        mqttManager->publishState();
    } else if (command.action == "volume") {
        settings->device.savedVolumePercent = command.volumePercent;
        audioPlayer->setVolumePercent(command.volumePercent);
        soundEffects->setVolumePercent(command.volumePercent);
        deferredActions->pendingVolume = command.volumePercent;
        deferredActions->volumePending = false;
        deferredActions->volumeSavePending = true;
        deferredActions->volumeSaveAt = millis() + kVolumePersistDelayMs;
        mqttManager->publishState();
    } else if (command.action == "play" && command.url.isEmpty()) {
        // Home Assistant media-source flows can emit a bare play command before
        // the real playmedia payload arrives. Replaying the current URL here
        // causes the previous source to start briefly and then switch.
        return;
    } else if (command.action == "playpause") {
        const AppStateSnapshot snapshot = appState->snapshot();
        if (snapshot.playback.state == "playing" || snapshot.playback.state == "buffering") {
            audioPlayer->stop();
            mqttManager->publishState();
        } else if (!snapshot.playback.url.isEmpty()) {
            String ignored;
            playRequest(snapshot.playback.url, snapshot.playback.title, snapshot.playback.type, snapshot.playback.source, ignored, true);
        }
    } else if (command.action == "next") {
        stepPlaybackHistory(1);
    } else if (command.action == "previous") {
        stepPlaybackHistory(-1);
    } else {
        runtimeAudio.alarmActive = false;
        restoreAmbientVolumeIfNeeded();
        scheduleAmbientResume();
        String ignored;
        playRequest(command.url, command.label, command.mediaType.isEmpty() ? command.action : command.mediaType, command.source, ignored, true);
    }
}

__attribute__((noinline)) void processQueuedAudioSource() {
    if(otaManager != nullptr && otaManager->isFirmwareTransferActive())buzzerMelody.stop();else buzzerMelody.loop();
    char* sourcePayload=nullptr;
    if (logicAudioQueue && xQueueReceive(logicAudioQueue,&sourcePayload,0)==pdTRUE) {
        JsonDocument descriptor;String error;auto parsed=deserializeJson(descriptor,sourcePayload);heap_caps_free(sourcePayload);
        if (otaManager != nullptr && otaManager->isFirmwareTransferActive()) error="Firmware update is active";
        else if (parsed!=DeserializationError::Ok) error="Invalid audio source payload";
        else if(descriptor["__logic"]["stop"].as<bool>()){
            String owner=descriptor["__logic"]["owner"]|"";bool buzzer=descriptor["__logic"]["buzzer"]|false;
            if(buzzer&&(owner.isEmpty()||owner==currentBuzzerLogicOwner)){buzzerMelody.stop();currentBuzzerLogicOwner="";}
#ifndef APP_DISABLE_AUDIO
            if(!buzzer&&(owner.isEmpty()||owner==currentDacLogicOwner)){audioPlayer->stop();currentDacLogicOwner="";}
#endif
        }
        else if(LogicAudioDispatch::outputToken(!descriptor["output"].isNull())!=(descriptor["__logic"]["outputToken"]|uint32_t(0)))return;
        else if(!LogicAudioDispatch::valid(descriptor["__logic"]["owner"]|"",descriptor["__logic"]["token"]|uint32_t(0)))return;
        else if(!descriptor["output"].isNull()){if(buzzerMelody.begin(descriptor.as<JsonVariantConst>(),*settings,error))currentBuzzerLogicOwner=descriptor["__logic"]["owner"]|"";}
#ifndef APP_DISABLE_AUDIO
        else if(audioPlayer->playSource(descriptor.as<JsonVariantConst>(),error))currentDacLogicOwner=descriptor["__logic"]["owner"]|"";
#endif
        if (appState != nullptr) appState->setLastError(error);
    }
}

void processDeferredActions() {
    processQueuedAudioSource();
    if (deferredActions == nullptr) {
        return;
    }

    if (deferredActions->settingsApplyPending) {
        flushPendingSettingsNow();
    }

    if (deferredActions->mqttConnectionChangePending) {
        String error;
        if (deferredActions->mqttConnectRequested) {
            if (!mqttManager->requestConnect(error) && appState != nullptr && !error.isEmpty()) {
                appState->setLastError(error);
            }
        } else {
            mqttManager->requestDisconnect(error);
        }
        deferredActions->mqttConnectionChangePending = false;
    }

    if (deferredActions->stopPending) {
        clearPreviewResumeState();
        audioPlayer->stop();
        deferredActions->stopPending = false;
        mqttManager->publishState();
    }

    if (deferredActions->volumePending) {
        settings->device.savedVolumePercent = deferredActions->pendingVolume;
        audioPlayer->setVolumePercent(deferredActions->pendingVolume);
        soundEffects->setVolumePercent(deferredActions->pendingVolume);
        deferredActions->volumePending = false;
        deferredActions->volumeSavePending = true;
        deferredActions->volumeSaveAt = millis() + kVolumePersistDelayMs;
        mqttManager->publishState();
    }

    if (deferredActions->volumeSavePending && static_cast<long>(millis() - deferredActions->volumeSaveAt) >= 0) {
        settingsManager->save(*settings);
        deferredActions->volumeSavePending = false;
    }

    if (deferredActions->equalizerPending) {
        settings->audio.equalizerPreset = deferredActions->pendingEqualizerPreset;
        settings->audio.equalizerLowDb = deferredActions->pendingEqualizerLowDb;
        settings->audio.equalizerPresenceDb = deferredActions->pendingEqualizerPresenceDb;
        settings->audio.equalizerHighDb = deferredActions->pendingEqualizerHighDb;
        audioPlayer->setEqualizer(settings->audio.equalizerPreset,
                                  settings->audio.equalizerLowDb,
                                  settings->audio.equalizerPresenceDb,
                                  settings->audio.equalizerHighDb);
        settingsManager->saveAudioEqualizer(settings->audio);
        deferredActions->equalizerPending = false;
        mqttManager->publishState();
    }

    if (deferredActions->playPending && otaManager != nullptr && otaManager->isFirmwareTransferActive()) {
        deferredActions->playPending = false;
    }
    // A release check can already own TLS buffers when a manual play request
    // arrives. Keep that request queued until the background task releases them.
    if (deferredActions->playPending && (otaManager == nullptr || !otaManager->isBusy())) {
        const AppStateSnapshot playbackSnapshotBeforeStart = appState != nullptr ? appState->snapshot() : AppStateSnapshot{};
        bool started = false;
        const bool guardPlayback = isResumablePlaybackSelection(
            deferredActions->playUrl, deferredActions->playType, deferredActions->playSource);
        if (guardPlayback) armAudioPlaybackGuard(deferredActions->playUrl);
        if (deferredActions->playSource == "effect-ambient") {
            restoreEffectVolumeIfNeeded();
            runtimeAudio.ambientPreviousVolume = settings != nullptr ? settings->device.savedVolumePercent : runtimeAudio.ambientPreviousVolume;
            audioPlayer->setVolumePercent(ambientPlaybackVolumePercent());
            runtimeAudio.ambientVolumeApplied = true;
        } else {
            restoreAmbientVolumeIfNeeded();
            if (deferredActions->playSource.startsWith("effect-")) {
                audioPlayer->setVolumePercent(effectVolumeForSource(deferredActions->playSource));
                runtimeAudio.effectVolumeApplied = true;
            } else {
                restoreEffectVolumeIfNeeded();
                audioPlayer->setVolumePercent(settings != nullptr ? settings->device.savedVolumePercent : DefaultConfig::DEFAULT_VOLUME_PERCENT);
            }
        }
        if (deferredActions->playFromStorage) {
            started = audioPlayer->playStorageFile(
                deferredActions->playStorageTarget,
                deferredActions->playStoragePath,
                deferredActions->playLabel,
                deferredActions->playType,
                deferredActions->playSource);
        } else {
            started = audioPlayer->play(
                deferredActions->playUrl,
                deferredActions->playLabel,
                deferredActions->playType,
                deferredActions->playSource);
        }
        const bool previewSwitchFromAmbient =
            deferredActions->playFromStorage &&
            deferredActions->playSource.startsWith("effect-") &&
            deferredActions->playSource != "effect-ambient" &&
            playbackSnapshotBeforeStart.playback.source == "effect-ambient" &&
            (playbackSnapshotBeforeStart.playback.state == "playing" || playbackSnapshotBeforeStart.playback.state == "buffering");
        if (!started && previewSwitchFromAmbient) {
            delay(kEffectPreviewRetryDelayMs);
            if (deferredActions->playFromStorage) {
                started = audioPlayer->playStorageFile(
                    deferredActions->playStorageTarget,
                    deferredActions->playStoragePath,
                    deferredActions->playLabel,
                    deferredActions->playType,
                    deferredActions->playSource);
            }
        }
        if (!started && guardPlayback) clearAudioPlaybackGuard();
        if (started && deferredActions->playAddToHistory) {
            rememberPlaybackSelection(
                deferredActions->playUrl,
                deferredActions->playLabel,
                deferredActions->playType,
                deferredActions->playSource);
        }
        if (started) {
            if (deferredActions->playFromStorage && deferredActions->playResumePositionSeconds > 0) {
                audioPlayer->seekStorageFile(deferredActions->playResumePositionSeconds);
            }
            deferredActions->playResumePositionSeconds = 0;
            persistResumablePlaybackSelection(
                deferredActions->playUrl,
                deferredActions->playLabel,
                deferredActions->playType,
                deferredActions->playSource,
                true);
            if (appState != nullptr) {
                appState->setLastError("");
            }
        } else if (appState != nullptr) {
            deferredActions->playResumePositionSeconds = 0;
            if (runtimeAudio.previewResumePending && deferredActions->playSource == runtimeAudio.previewActiveSource) {
                restoreEffectVolumeIfNeeded();
                resumeQueuedPreviewPlayback();
            }
            appState->setLastError(playbackStartFailureMessage(*deferredActions));
        }
        deferredActions->playPending = false;
        mqttManager->publishState();
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    DebugLog.begin();
    beginSystemMetrics();
    activeCpuFrequencyMhz = ESP.getCpuFreqMHz();

    const esp_reset_reason_t resetReason = esp_reset_reason();
    loadRollbackState();
    wokeFromDeepSleep = resetReason == ESP_RST_DEEPSLEEP;
    DebugLog.printf("\n[boot] app=%s version=%s built=%s\n", APP_NAME, APP_VERSION, APP_BUILD_DATE);
    DebugLog.printf("[boot] reset reason=%s (%d)\n", resetReasonToString(resetReason), static_cast<int>(resetReason));
    if (!lastRolledBackVersion.isEmpty()) {
        DebugLog.printf("[rollback] previous OTA rollback detected: version=%s reason=%s\n", lastRolledBackVersion.c_str(), lastRollbackReason.c_str());
    }
    if (otaPendingVerification) {
        DebugLog.printf("[rollback] running pending-verify image %s\n", otaPendingVersion.c_str());
    }
    Serial.flush();

    if (!initializeRuntimeObjects()) {
        DebugLog.println("[boot] failed to construct runtime objects");
        Serial.flush();
        if (otaPendingVerification) {
            rollbackAndReboot("Runtime objects failed to initialize during OTA health check.");
        }
        delay(1000);
        DebugLog.service(true);
        ESP.restart();
    }

    if (!appState->begin()) {
        DebugLog.println("[boot] failed to initialize app state mutex");
        Serial.flush();
    }

    settingsManager->begin();
    *settings = settingsManager->load();
    const uint8_t abnormalResetCount=updateAbnormalResetCounter(resetReason);
    runtimeSafeModeActive=abnormalResetCount>=kRuntimeSafeModeThreshold;
    if(abnormalResetCount>0)armPowerCycleCounterClear();
    recoverInterruptedAudioPlayback(resetReason);
    if(runtimeSafeModeActive){
        settings->audio.lastPlayback.resumeAfterBoot=false;
        DebugLog.printf("[boot-guard] runtime safe mode active after %u consecutive abnormal resets\n",static_cast<unsigned>(abnormalResetCount));
    }
    repairLowBatteryBootCounterOnce();

    // Take ownership of configured bridge inputs before storage, networking, or
    // the USB serial grace period can delay startup. Both inputs must stay LOW
    // until an explicit motor command is received.
    motorController.begin([](int8_t limitInputIndex) {
        return limitInputIndex >= 0 ? readConfiguredLimitSwitchPressed(static_cast<size_t>(limitInputIndex)) : false;
    });
    motorController.applySettings(*settings);

    waitForSerialConsole();
    delay(200);
    if (settings->device.powerCycleFactoryResetEnabled) {
        const uint8_t powerCycleCount = updatePowerCycleCounter(resetReason);
        if (powerCycleCount > 0) {
            DebugLog.printf("[boot-guard] power-cycle count=%u/%u\n",
                          static_cast<unsigned>(powerCycleCount),
                          static_cast<unsigned>(kPowerCycleFactoryResetThreshold));
        }
        if (powerCycleCount >= kPowerCycleFactoryResetThreshold) {
            DebugLog.println("[boot-guard] factory reset triggered by repeated power cycles");
            flashFactoryResetIndicator();
            settingsManager->reset();
            clearPowerCycleCounter();
            Serial.flush();
            logicDevice.shuttingDown();
        motorController.prepareForRestart();
            delay(200);
            DebugLog.service(true);
            ESP.restart();
        }
        if (powerCycleCount > 0) {
            armPowerCycleCounterClear();
        }
    } else {
        clearPowerCycleCounter();
    }
    beginStorageBackends(*settings);
    DebugLog.service(true);
    activeStatusLedPin = settings->device.statusLedPin;
    activeStatusLedGreenPin = settings->device.statusLedGreenPin;
    activeStatusLedBluePin = settings->device.statusLedBluePin;
    activeStatusLedIsNeoPixel = statusLedTypeIsNeoPixel(settings->device.statusLedType);
    activeStatusLedIsRgb = settings->device.statusLedType.equalsIgnoreCase("rgb");
    activeWapeTriggerPin = settings->oled.displayType == "wape" ? settings->oled.wapeTriggerPin : 0;

    initializeButtons();
    motorController.reclaimConfiguredPins();

    initializeStatusLed();
    writeStatusLed(false);
    initializeWapeTriggerPin();

    displayManager->begin(settings->oled);
    displayManager->setBootMessage("Booting");

    appState->setDevice(settings->device.deviceName, settings->device.friendlyName, settings->usingSavedSettings);

    wifiManager->begin(*settings, *appState);

    batteryMonitor->begin(settings->battery, settings->battery.adcPin, *appState);

    activeI2sBclkPin = settings->audio.bclkPin;
    activeI2sWsPin = settings->audio.wsPin;
    activeI2sDoutPin = settings->audio.doutPin;
    activeAudioOutputEnabled = settings->audio.enabled;
    audioPlayer->begin(activeI2sBclkPin, activeI2sWsPin, activeI2sDoutPin, settings->device.savedVolumePercent, activeAudioOutputEnabled, *appState);
    logicDevice.begin(ELMA_COMPILED_LOGICS,*appState,[](JsonObject root){
        root["firmware"]["version"]=APP_VERSION;
        root["battery"]["available"]=batteryMonitor->enabled() && batteryMonitor->latest().filteredVoltage>0;
        root["battery"]["percentage"]=estimateBatteryPercent(root["battery"]["voltage"] | 0.0f);
    },[](JsonObjectConst node,JsonVariantConst args,std::string& message){
        String error;bool ok=false;std::string type=node["type"] | "";
        if(std::string(node["peripheral"]["profile"]|"").find("buzzer")!=std::string::npos) {
            std::string command=args["action"]|"";
            if(command=="stop"){bool stopped=queueLogicAudioStop(node,error,args["all"]|false);if(!stopped)message=error.c_str();return stopped;}
            if(command=="play"){JsonDocument source;source.set(args["source"]);source["output"]["kind"]=node["parameters"]["buzzerMode"]|"passive";source["output"]["slot"]=(std::string(node["binding"]["group"]|"")+":"+std::to_string(node["binding"]["index"]|0));ok=queueAudioSourceOwned(source.as<JsonVariantConst>(),error,node["id"]|"");}
        } else if(node["binding"]["group"]=="audio") {
            // OTA intentionally retains saved settings. The primary audio action
            // therefore follows the DAC that audioPlayer initialized at boot,
            // even when the graph was compiled with older pin metadata.
            if(!activeAudioOutputEnabled) {
                message="Compiled Logics audio action requires an enabled DAC";return false;
            }
            std::string command=args["action"] | "";
            if(command=="play")ok=queueAudioSourceOwned(args["source"],error,node["id"]|"");
            else if(command=="stop")ok=queueLogicAudioStop(node,error,args["all"]|false);
            else if(command=="volume" || command=="set"){double value=args["value"] | -1.0;if(value>=0&&value<=100){audioPlayer->setVolumePercent(uint8_t(value));ok=true;}else error="Invalid Logics volume";}
        } else if(node["binding"]["group"]=="display") {
            if(!displayManager||!displayManager->available()||node["binding"]["index"]!=0||node["peripheral"]["profile"]!="i2c-oled"||node["binding"]["pins"]["SDA"]!=settings->oled.sdaPin||node["binding"]["pins"]["SCL"]!=settings->oled.sclPin){message="Configured OLED is unavailable or Logics pins do not match";return false;}
            if(type=="peripheral.clear")ok=displayManager->clearLogicText();
            else if(type=="peripheral.text") {
                double seconds=args["seconds"]|0.0;const char* text=args["text"]|nullptr;
                if(text&&strlen(text)<=256&&std::isfinite(seconds)&&seconds>0&&seconds<=86400){displayManager->showTemporaryCenterText(text,uint32_t(seconds*1000));ok=true;}
                else error="Display text requires <=256 bytes and a duration >0 to 86400 seconds";
            }
        } else if(type=="mainboard.mqtt.publish") {
            double qos=args["qos"]|-1.0;
            if(qos==1)ok=mqttManager->publishLogicMessage(args["topic"]|"",args["payload"]|"",args["retained"]|false,uint8_t(qos),error);
            else error="MQTT QoS must be 1";
        } else if(type=="mainboard.mqtt.connect")ok=mqttManager->requestConnect(error);
        else if(type=="mainboard.mqtt.disconnect")ok=mqttManager->requestDisconnect(error);
        else if(type=="mainboard.mqtt.rediscover")ok=mqttManager->requestRediscovery(error);
        else if(type=="mainboard.ota.check")ok=otaManager->triggerCheck(false);
        else if(type=="mainboard.device.reboot"){requestRestartSequence("logics",false);ok=true;}
        if(!ok)message=error.isEmpty()?"Unsupported or failed Logics action":error.c_str();
        return ok;
    });
    if(runtimeSafeModeActive){
        const char* warning="Recovery safe mode: automatic playback and Logics were stopped after repeated abnormal restarts. Review them before restarting.";
        logicDevice.enterRecoverySafeMode(warning);
        appState->setLastError(warning);
    }

#if APP_AUDIO_DIAGNOSTIC_TEST
    if (activeAudioOutputEnabled) {
        audioPlayer->setDirectLibraryVolume(DefaultConfig::AUDIO_DIAGNOSTIC_LIBRARY_VOLUME);
        DebugLog.printf("[audio-test] build enabled, waiting for Wi-Fi to start %s at library volume %u using BCLK=%u WS=%u DOUT=%u\n",
                      DefaultConfig::AUDIO_DIAGNOSTIC_STREAM_URL,
                      static_cast<unsigned>(DefaultConfig::AUDIO_DIAGNOSTIC_LIBRARY_VOLUME),
                      static_cast<unsigned>(activeI2sBclkPin),
                      static_cast<unsigned>(activeI2sWsPin),
                      static_cast<unsigned>(activeI2sDoutPin));
    }
#endif

    soundEffects->begin(*settings);
    otaManager->begin(*settings, *appState);
    otaManager->setRestartHandler(handleOtaRestartRequest);
    refreshRollbackStateInOtaManager();
    otaManager->setProgressCallback(pumpOtaDisplayProgress);

    mqttManager->begin(*settings, *appState, *wifiManager, *otaManager, handleMqttCommand, [](JsonObject root) {
        appendAugmentedMotorStatus(root);
    });
    if (!logicAudioQueue) logicAudioQueue=xQueueCreate(9,sizeof(char*));
    webServer->setLogicsHandlers([](JsonDocument& result,bool graph){logicDevice.snapshot(result,graph);},
        [](JsonVariantConst command,JsonDocument& result,String& error){return logicDevice.request(command,result,error);});
    webServer->setAudioSourceHandler(queueAudioSource);
    webServer->begin(
        *appState,
        *wifiManager,
        *settingsManager,
        *otaManager,
        []() { return *settings; },
        saveSettingsFromJson,
        [](const String& url, const String& label, const String& type, String& error) {
            const bool effectSelection = type.startsWith("effect-");
            const bool fileManagerSelection = type == "file-manager";
            const bool addToHistory = !effectSelection;
            const String source = effectSelection ? type : (fileManagerSelection ? String("file-manager") : String(""));
            const String normalizedType = effectSelection ? String("effect") : (fileManagerSelection ? String("media") : type);
            if (effectSelection && appState != nullptr) {
                const AppStateSnapshot snapshot = appState->snapshot();
                queuePreviewResume(snapshot, source);
            } else {
                clearPreviewResumeState();
            }
            return playRequest(url, label, normalizedType, source, error, addToHistory);
        },
        []() {
            deferredActions->stopPending = true;
            deferredActions->playPending = false;
        },
        [](uint8_t volume) {
            deferredActions->pendingVolume = volume;
            deferredActions->volumePending = true;
        },
        [](const String& preset, int8_t lowDb, int8_t presenceDb, int8_t highDb) {
            deferredActions->pendingEqualizerPreset = preset;
            deferredActions->pendingEqualizerLowDb = lowDb;
            deferredActions->pendingEqualizerPresenceDb = presenceDb;
            deferredActions->pendingEqualizerHighDb = highDb;
            deferredActions->equalizerPending = true;
        },
        [](uint32_t positionSeconds) {
            if (audioPlayer == nullptr || appState == nullptr || appState->snapshot().playback.source != "file-manager") {
                return false;
            }
            return audioPlayer->seekStorageFile(positionSeconds);
        },
        [](bool apply) { return otaManager->triggerCheck(apply); },
        [](const String& action, String& error) {
            if (action == "rediscover") {
                return mqttManager->requestRediscovery(error);
            }

            const bool connect = action != "disconnect";
            if (connect) {
                const String host = deferredActions->settingsApplyPending
                    ? deferredActions->pendingSettings.mqtt.host
                    : settings->mqtt.host;
                if (host.isEmpty()) {
                    error = "Enter an MQTT host first.";
                    return false;
                }
            }
            deferredActions->mqttConnectRequested = connect;
            deferredActions->mqttConnectionChangePending = true;
            error = "";
            return true;
        },
        [](uint8_t channelIndex, bool forward, uint32_t durationMs, int8_t limitInputIndex, String& error) {
            if (!motorController.available()) {
                error = "DRV8833 control is not configured.";
                return false;
            }
            if (!motorController.runChannel(channelIndex, forward, durationMs, limitInputIndex, error)) {
                return false;
            }
            if (appState != nullptr) {
                appState->setLastError("");
            }
            if (mqttManager != nullptr) {
                mqttManager->publishState();
            }
            lastProcessedMotorStateVersion = motorController.stateVersion();
            lastPublishedMotorStateVersion = motorController.stateVersion();
            return true;
        },
        saveMotorRuntimeConfigFromJson,
        [](JsonObject root) {
            appendAugmentedMotorStatus(root);
        },
        []() { triggerWapeDisplay(); },
        []() {
            if (webServer != nullptr) {
                webServer->setWebUiLocked(true);
            }
            requestWebUiLockSequence();
        },
        []() { requestRestartSequence("manual", false); },
        []() { requestRestartSequence("factory_reset", true); });

    displayManager->setBootMessage("Idle");
#if !APP_AUDIO_DIAGNOSTIC_TEST
    runtimeAudio.startupEffectPending = !runtimeSafeModeActive && !effectFileForSource("effect-startup").isEmpty();
    runtimeAudio.startupEffectActive = false;
    runtimeAudio.startupEffectAttempts = 0;
    runtimeAudio.startupEffectEligibleAt = millis() + kStartupEffectDelayMs;
#endif
    runtimeAudio.ambientEligibleAt = millis() + kAmbientResumeDelayMs;
    runtimeAudio.bootUpdateCheckQueued = settings->ota.autoCheck || settings->ota.autoUpdate;
    runtimeAudio.resumeSavedPlaybackPending = !runtimeSafeModeActive && settings->audio.rememberLastPlayed && settings->audio.lastPlayback.resumeAfterBoot && !settings->audio.lastPlayback.url.isEmpty();
    runtimeAudio.resumeSavedPlaybackEligibleAt = 0;
    runtimeAudio.updateAvailableEffectEligibleAt = 0;
    if (settings->oled.displayType == "wape" && settings->oled.wapeTriggerEvent == "device_start") {
        requestWapeTriggerPulse();
    }

    applyRuntimeSettings();
    DebugLog.printf("[clone] provisioning ready identity=%s\n", settings->device.deviceName.c_str());
    Serial.flush();
    DebugLog.service(true);
}

namespace {
void flushPendingSettingsNow() {
    if (deferredActions == nullptr || !deferredActions->settingsApplyPending) {
        return;
    }
    settingsManager->save(deferredActions->pendingSettings);
    *settings = settingsManager->load();
    if (deferredActions->settingsApplyWifiPowerOnly) {
        wifiManager->applySettings(*settings);
    } else {
        applyRuntimeSettings();
    }
    deferredActions->settingsApplyPending = false;
    deferredActions->settingsApplyWifiPowerOnly = false;
    mqttManager->publishState();
}
}

void processSoundEffectTransitions(const AppStateSnapshot& snapshot) {
#if APP_AUDIO_DIAGNOSTIC_TEST
    (void)snapshot;
    return;
#endif
    if (soundEffects == nullptr || settings == nullptr) {
        return;
    }

    String completedPlaybackSource;
    const bool playbackCompletionReported = audioPlayer != nullptr && audioPlayer->consumePlaybackCompletion(completedPlaybackSource);
    if (playbackCompletionReported) {
        DebugLog.printf("[audio] completion source=%s\n", completedPlaybackSource.c_str());
    }
    if (playbackCompletionReported && completedPlaybackSource == "effect-startup") {
        runtimeAudio.startupEffectActive = false;
        runtimeAudio.resumeSavedPlaybackEligibleAt = millis() + 250UL;
        runtimeAudio.updateAvailableEffectEligibleAt = millis() + 3000UL;
        restoreEffectVolumeIfNeeded();
    }

    const bool nowPlaying = snapshot.playback.state == "playing" || snapshot.playback.state == "buffering";
    const bool wasPlaying = previousPlaybackState == "playing" || previousPlaybackState == "buffering";
    const bool playbackStopped = wasPlaying && !nowPlaying &&
        (previousPlaybackSource != "effect-startup" ||
         (playbackCompletionReported && completedPlaybackSource == "effect-startup"));
    const bool playbackStarted = !wasPlaying && nowPlaying;

    if (!transitionStateInitialized) {
        previousWifiConnected = snapshot.network.wifiConnected;
        previousMqttConnected = snapshot.network.mqttConnected;
        previousPlaybackState = snapshot.playback.state;
        previousPlaybackSource = snapshot.playback.source;
        previousPlaybackType = snapshot.playback.type;
        previousCharging = snapshot.battery.charging;
        transitionStateInitialized = true;
        return;
    }

    if (!previousWifiConnected && snapshot.network.wifiConnected) {
        soundEffects->playWifiConnected();
    } else if (previousWifiConnected && !snapshot.network.wifiConnected) {
        soundEffects->playWifiDisconnected();
    }

    if (!previousMqttConnected && snapshot.network.mqttConnected) {
        soundEffects->playMqttConnected();
    }

    if (playbackStarted) {
        if (snapshot.playback.source == "effect-startup") {
            runtimeAudio.startupEffectActive = true;
        }
        if (runtimeAudio.previewResumePending && snapshot.playback.source == runtimeAudio.previewResumeSource) {
            clearPreviewResumeState();
        }
        if (!snapshot.playback.source.startsWith("effect-")) {
            soundEffects->playPlaybackStart();
        }
        if (snapshot.playback.source != "effect-ambient") {
            scheduleAmbientResume(snapshot.playback.source.startsWith("effect-") ? kAmbientResumeDelayMs : 0UL);
        }
        if (settings != nullptr && settings->oled.wapeTriggerEvent == "play_start") {
            requestWapeTriggerPulse();
        }
    } else if (playbackStopped && !previousPlaybackSource.startsWith("effect-")) {
        soundEffects->playPlaybackStop();
    }

    if (playbackStopped) {
        if ((previousPlaybackType == "stream" || previousPlaybackType == "media") && !previousPlaybackSource.startsWith("effect-")) {
            setSavedPlaybackResumeAfterBoot(false);
        }
        restoreEffectVolumeIfNeeded();
        if (runtimeAudio.pendingCommandAfterNotification) {
            runtimeAudio.pendingCommandAfterNotification = false;
            clearPreviewResumeState();
            executePlaybackCommand(runtimeAudio.pendingCommand);
        } else if (runtimeAudio.pendingAutoUpdateInstall) {
            runtimeAudio.pendingAutoUpdateInstall = false;
            clearPreviewResumeState();
            String error;
            if (otaManager != nullptr && !snapshot.ota.latestVersion.isEmpty()) {
                otaManager->triggerInstallVersion(snapshot.ota.latestVersion, error);
            }
        } else if (runtimeAudio.previewResumePending && previousPlaybackSource == runtimeAudio.previewActiveSource) {
            resumeQueuedPreviewPlayback();
        } else if (runtimeAudio.restartPending) {
            if (previousPlaybackSource == "effect-update-success") {
                if (!playConfiguredEffectSource("effect-shutdown", "Restarting")) {
                    if (runtimeAudio.restartFactoryReset) {
                        factoryResetRequested = true;
                    }
                    runtimeAudio.restartPending = false;
                    runtimeAudio.restartForcePending = false;
                    scheduleReboot(250);
                }
            } else if (previousPlaybackSource == "effect-shutdown") {
                if (runtimeAudio.restartFactoryReset) {
                    factoryResetRequested = true;
                }
                runtimeAudio.restartPending = false;
                runtimeAudio.restartForcePending = false;
                scheduleReboot(250);
            }
        } else if (runtimeAudio.webUiLockPending && previousPlaybackSource == "effect-shutdown") {
            runtimeAudio.webUiLockPending = false;
            applyWebUiLockNow();
        } else if (runtimeAudio.alarmActive && previousPlaybackSource == "effect-alarm") {
            playConfiguredEffectSource("effect-alarm", "Alarm");
        } else if (previousPlaybackSource == "effect-ambient") {
            restoreAmbientVolumeIfNeeded();
            if (!runtimeAudio.alarmActive) {
                runtimeAudio.ambientPreviousVolume = settings->device.savedVolumePercent;
                audioPlayer->setVolumePercent(ambientPlaybackVolumePercent());
                runtimeAudio.ambientVolumeApplied = true;
                if (!playConfiguredEffectSource("effect-ambient", "Ambient Sound")) {
                    restoreAmbientVolumeIfNeeded();
                    scheduleAmbientResume(kAmbientResumeDelayMs);
                }
            }
        } else if (previousPlaybackSource == "effect-low-battery") {
            runtimeAudio.lowBatteryCueActive = false;
        }
    }

    if (!runtimeAudio.updateAvailableHandled && snapshot.ota.updateAvailable && !snapshot.ota.latestVersion.isEmpty() &&
        !runtimeAudio.startupEffectPending && !runtimeAudio.startupEffectActive && !runtimeAudio.resumeSavedPlaybackPending &&
        snapshot.playback.state == "idle" && deferredActions != nullptr && !deferredActions->playPending &&
        (runtimeAudio.updateAvailableEffectEligibleAt == 0 ||
         static_cast<long>(millis() - runtimeAudio.updateAvailableEffectEligibleAt) >= 0)) {
        runtimeAudio.updateAvailableHandled = true;
        runtimeAudio.pendingAutoUpdateInstall = settings->ota.autoUpdate;
        const bool playedUpdateAvailableCue =
            tryOverlayConfiguredEffect(settings->effects.updateAvailableFile, 40, effectVolumeForSource("effect-update-available")) ||
            playConfiguredEffectSource("effect-update-available", "Update Available");
        if (!playedUpdateAvailableCue && runtimeAudio.pendingAutoUpdateInstall) {
            runtimeAudio.pendingAutoUpdateInstall = false;
            String error;
            if (otaManager != nullptr) {
                otaManager->triggerInstallVersion(snapshot.ota.latestVersion, error);
            }
        }
    } else if (!snapshot.ota.updateAvailable) {
        runtimeAudio.updateAvailableHandled = false;
        runtimeAudio.updateAvailableEffectEligibleAt = 0;
    }

    if (!previousCharging && snapshot.battery.charging) {
        if (settings != nullptr && settings->oled.wapeTriggerEvent == "charging_start") {
            requestWapeTriggerPulse();
        }
    }

    previousWifiConnected = snapshot.network.wifiConnected;
    previousMqttConnected = snapshot.network.mqttConnected;
    previousPlaybackState = snapshot.playback.state;
    previousPlaybackSource = snapshot.playback.source;
    previousPlaybackType = snapshot.playback.type;
    previousCharging = snapshot.battery.charging;
}

void serviceRuntimeAudioAutomation(const AppStateSnapshot& snapshot) {
    if (otaManager != nullptr && otaManager->isFirmwareTransferActive()) return;
    if (settings == nullptr) {
        return;
    }

    if (runtimeAudio.startupEffectPending && snapshot.playback.state != "playing" && snapshot.playback.state != "buffering") {
        const bool eligible = runtimeAudio.startupEffectEligibleAt != 0 && static_cast<long>(millis() - runtimeAudio.startupEffectEligibleAt) >= 0;
        if (eligible && deferredActions != nullptr && !deferredActions->playPending) {
            const String startupEffectRef = effectFileForSource("effect-startup");
            StorageTarget storageTarget = StorageTarget::Flash;
            String storagePath;
            const bool storageReference = parseStorageFileReference(startupEffectRef, storageTarget, storagePath);
            if (startupEffectRef.isEmpty()) {
                runtimeAudio.startupEffectPending = false;
                runtimeAudio.startupEffectActive = false;
                runtimeAudio.startupEffectAttempts = 0;
            } else {
                if (storageReference && !storageMounted(storageTarget)) {
                    remountActiveStorageBackend(storageTarget);
                }

                runtimeAudio.startupEffectAttempts++;
                String startupEffectError;
                if (playConfiguredEffect(startupEffectRef, "Startup", "effect-startup", &startupEffectError)) {
                    runtimeAudio.startupEffectPending = false;
                    runtimeAudio.startupEffectActive = true;
                    runtimeAudio.startupEffectAttempts = 0;
                } else {
                    const bool retryableStorageFailure = storageReference && runtimeAudio.startupEffectAttempts < kStartupEffectMaxAttempts;
                    if (retryableStorageFailure) {
                        runtimeAudio.startupEffectEligibleAt = millis() + kStartupEffectRetryDelayMs;
                        DebugLog.printf("[effect] startup retry %u/%u pending: %s\n",
                                      static_cast<unsigned>(runtimeAudio.startupEffectAttempts),
                                      static_cast<unsigned>(kStartupEffectMaxAttempts),
                                      startupEffectError.c_str());
                    } else {
                        runtimeAudio.startupEffectPending = false;
                        runtimeAudio.startupEffectActive = false;
                        runtimeAudio.startupEffectAttempts = 0;
                        if (!startupEffectError.isEmpty()) {
                            DebugLog.printf("[effect] startup not played: %s\n", startupEffectError.c_str());
                            if (appState != nullptr) {
                                appState->setLastError(startupEffectError);
                            }
                        }
                    }
                }
            }
        }
    }

    if (runtimeAudio.previewResumePending && !runtimeAudio.previewResumeUrl.isEmpty() &&
        snapshot.playback.state != "playing" && snapshot.playback.state != "buffering") {
        StorageTarget storageTarget = StorageTarget::Flash;
        String storagePath;
        const bool storageReference = parseStorageFileReference(runtimeAudio.previewResumeUrl, storageTarget, storagePath);
        const bool resumeReady = storageReference ? storageMounted(storageTarget) : (wifiManager != nullptr && wifiManager->isConnected());
        if (resumeReady) {
            resumeQueuedPreviewPlayback();
        }
    }

    if (audioPlayer != nullptr && audioPlayer->consumeOverlayFinished() && runtimeAudio.pendingAutoUpdateInstall) {
        runtimeAudio.pendingAutoUpdateInstall = false;
        String error;
        if (otaManager != nullptr && !snapshot.ota.latestVersion.isEmpty()) {
            otaManager->triggerInstallVersion(snapshot.ota.latestVersion, error);
        }
    }

    // Resuming radio and starting a TLS release check together exhausts the
    // non-PSRAM ESP32 heap before the MP3 decoder can allocate its buffers.
    if (runtimeAudio.bootUpdateCheckQueued && wifiManager != nullptr && wifiManager->isConnected() && otaManager != nullptr && !snapshot.ota.busy &&
        !runtimeAudio.resumeSavedPlaybackPending && (deferredActions == nullptr || !deferredActions->playPending) &&
        snapshot.playback.state != "playing" && snapshot.playback.state != "buffering") {
        runtimeAudio.bootUpdateCheckQueued = false;
        otaManager->triggerCheck(false);
    }

    const bool savedPlaybackResumeEligible = runtimeAudio.resumeSavedPlaybackEligibleAt == 0 ||
        static_cast<long>(millis() - runtimeAudio.resumeSavedPlaybackEligibleAt) >= 0;
    if (!runtimeAudio.startupEffectPending && !runtimeAudio.startupEffectActive && runtimeAudio.resumeSavedPlaybackPending &&
        savedPlaybackResumeEligible && deferredActions != nullptr && !deferredActions->playPending &&
        snapshot.playback.state != "playing" && snapshot.playback.state != "buffering") {
        StorageTarget storageTarget = StorageTarget::Flash;
        String storagePath;
        const bool storageReference = settings != nullptr && parseStorageFileReference(settings->audio.lastPlayback.url, storageTarget, storagePath);
        const bool resumeReady = storageReference || (wifiManager != nullptr && wifiManager->isConnected());
        if (resumeReady) {
            DebugLog.printf("[audio] resuming saved playback after boot: %s\n", settings->audio.lastPlayback.url.c_str());
            runtimeAudio.resumeSavedPlaybackPending = false;
            queueSavedPlaybackResume();
        }
    }

    const bool validBatteryReading = batteryReadingIsValidForSleepPolicy(snapshot.battery);
    const uint8_t batteryPercent = validBatteryReading ? estimateBatteryPercent(snapshot.battery.voltage) : 100;
    if (!validBatteryReading || batteryPercent > settings->device.lowBatterySleepThresholdPercent) {
        runtimeAudio.lowBatteryCueActive = false;
    } else if (!runtimeAudio.lowBatteryCueActive && !settings->effects.lowBatteryFile.isEmpty() && !runtimeAudio.alarmActive &&
        !runtimeAudio.restartPending && !runtimeAudio.startupEffectPending && !runtimeAudio.startupEffectActive) {
        runtimeAudio.lowBatteryCueActive =
            tryOverlayConfiguredEffect(settings->effects.lowBatteryFile, 40, effectVolumeForSource("effect-low-battery")) ||
            playConfiguredEffectSource("effect-low-battery", "Low Battery");
    }

    if (!runtimeSafeModeActive && snapshot.playback.source != "effect-ambient") {
        startAmbientIfEligible(snapshot);
    }

    if (runtimeAudio.restartForcePending && static_cast<long>(millis() - runtimeAudio.restartForceAt) >= 0) {
        runtimeAudio.restartForcePending = false;
        runtimeAudio.restartPending = false;
        if (runtimeAudio.restartFactoryReset) {
            factoryResetRequested = true;
        }
        scheduleReboot(250);
    }
}

void serviceAudioDiagnosticTest() {
#if !APP_AUDIO_DIAGNOSTIC_TEST
    return;
#else
    static bool diagnosticAudioStartQueued = false;
    static unsigned long diagnosticAudioRetryAt = 0;

    if (appState == nullptr || audioPlayer == nullptr || wifiManager == nullptr) {
        return;
    }

    if (!wifiManager->isConnected()) {
        diagnosticAudioStartQueued = false;
        diagnosticAudioRetryAt = 0;
        return;
    }

    const AppStateSnapshot snapshot = appState->snapshot();
    if (snapshot.playback.state == "playing" || snapshot.playback.state == "buffering") {
        diagnosticAudioStartQueued = true;
        return;
    }

    if (diagnosticAudioRetryAt != 0 && static_cast<long>(millis() - diagnosticAudioRetryAt) < 0) {
        return;
    }

    audioPlayer->setDirectLibraryVolume(DefaultConfig::AUDIO_DIAGNOSTIC_LIBRARY_VOLUME);
    const AudioPlayer::DiagnosticsSnapshot diagnostics = audioPlayer->diagnostics();
    DebugLog.printf("[audio-test] init driver=ESP32-audioI2S fmt=std-i2s preferred_rate=%lu stereo=%s bclk=%u ws=%u dout=%u lib_volume=%u url=%s\n",
                  static_cast<unsigned long>(diagnostics.requestedSampleRateHz),
                  diagnostics.stereoEnabled ? "on" : "off",
                  static_cast<unsigned>(activeI2sBclkPin),
                  static_cast<unsigned>(activeI2sWsPin),
                  static_cast<unsigned>(activeI2sDoutPin),
                  static_cast<unsigned>(DefaultConfig::AUDIO_DIAGNOSTIC_LIBRARY_VOLUME),
                  DefaultConfig::AUDIO_DIAGNOSTIC_STREAM_URL);

    if (audioPlayer->play(DefaultConfig::AUDIO_DIAGNOSTIC_STREAM_URL, "MAX98357A Test Stream", "stream", "diagnostic-test")) {
        diagnosticAudioStartQueued = true;
        diagnosticAudioRetryAt = 0;
        return;
    }

    diagnosticAudioRetryAt = millis() + 5000UL;
    DebugLog.println("[audio-test] playback start failed, retrying in 5s");
#endif
}

void loop() {
    if (appState == nullptr || settingsManager == nullptr || wifiManager == nullptr || batteryMonitor == nullptr ||
        displayManager == nullptr || audioPlayer == nullptr || otaManager == nullptr || mqttManager == nullptr ||
        webServer == nullptr) {
        delay(100);
        return;
    }

    const unsigned long now = millis();
    serviceCloneProvisioningSerial();
    static bool audioReleasedForUpdate = false;
    if (otaManager->isFirmwareTransferActive() && !audioReleasedForUpdate) {
        audioReleasedForUpdate = true;
        DebugLog.println("[ota] releasing playback buffers for firmware transfer");
        audioPlayer->releaseResourcesForUpdate();
        audioPlayer->setEqualizer(settings->audio.equalizerPreset, settings->audio.equalizerLowDb,
                                  settings->audio.equalizerPresenceDb, settings->audio.equalizerHighDb);
    } else if (!otaManager->isFirmwareTransferActive()) {
        audioReleasedForUpdate = false;
    }
    logicDevice.loop(now,otaManager->isFirmwareTransferActive());
    processDeferredActions();
    serviceWapeTriggerPulse();
    if (now - lastInputPollAt >= kInputPollIntervalMs) {
        lastInputPollAt = now;
        pollPhysicalButtons();
    }
    motorController.loop();
    publishMotorStateIfNeeded();
    serviceMotorStateRepublish();
    wifiManager->loop();
    serviceAudioDiagnosticTest();
    if (activeAudioOutputEnabled) {
        audioPlayer->loop();
    }
    static unsigned long lastPlaybackProgressAt = 0;
    if (now - lastPlaybackProgressAt >= 500UL) {
        lastPlaybackProgressAt = now;
        const AppStateSnapshot progressSnapshot = appState->snapshot();
        if (progressSnapshot.playback.source == "file-manager" &&
            (progressSnapshot.playback.state == "playing" || progressSnapshot.playback.state == "buffering")) {
            appState->setPlaybackProgress(audioPlayer->currentPositionSeconds(), audioPlayer->durationSeconds());
        }
    }
    pollStorageBackends();
    DebugLog.service();
    const bool batteryUpdated = batteryMonitor->enabled() && batteryMonitor->loop(isBatterySamplingAllowed());
    otaManager->loop();
    mqttManager->loop();

    if (now - lastCpuGovernorSampleAt >= kCpuGovernorSampleIntervalMs) {
        lastCpuGovernorSampleAt = now;
        sampleCpuLoadMetrics();
        applyCpuFrequencyPolicy();
    }

    if (!runtimeStateSnapshotInitialized || now - lastRuntimeStateServiceAt >= kRuntimeStateServiceIntervalMs) {
        lastRuntimeStateServiceAt = now;
        runtimeStateSnapshot = appState->snapshot();
        runtimeStateSnapshotInitialized = true;
        processSoundEffectTransitions(runtimeStateSnapshot);
        serviceRuntimeAudioAutomation(runtimeStateSnapshot);
        serviceAudioPlaybackGuard(runtimeStateSnapshot);
        displayManager->loop(runtimeStateSnapshot);
        handleLowBatterySleepPolicy(runtimeStateSnapshot);
        confirmOtaHealthIfReady();
        publishOtaStateIfNeeded(runtimeStateSnapshot);
    }
    maybeClearPowerCycleCounterAfterStableBoot();

    if (millis() - lastHeapUpdateAt > 2000UL) {
        lastHeapUpdateAt = millis();
        sampleSystemMetrics();
        appState->setFreeHeap(getSystemMetricsSnapshot().freeHeapBytes);
        mqttManager->publishChipTemperature();
        updateStatusLedForNetwork(wifiManager->isConnected(), wifiManager->isApMode());
    }

    if (batteryUpdated) {
        const BatteryReading reading = batteryMonitor->latest();
        mqttManager->publishBattery(reading.filteredVoltage, reading.rawAdcVoltage, reading.rawAdc, reading.charging);
        mqttManager->publishState();
    }

    if (factoryResetRequested) {
        settingsManager->reset();
        clearLogicRecord();
        startFactoryResetLedBlink(millis());
        const unsigned long minimumRebootAt = millis() + kFactoryResetLedSuccessWindowMs;
        if (!rebootRequested || static_cast<long>(rebootAt - minimumRebootAt) < 0) {
            rebootRequested = true;
            rebootAt = minimumRebootAt;
        }
        factoryResetRequested = false;
    }

    serviceStatusLedOverrides(millis());

    const bool otaTransferActive = otaManager != nullptr && otaManager->isBusy();

    if (!otaTransferActive && !recoveryRebootScheduled && wifiManager->shouldRebootForRecovery()) {
        recoveryRebootScheduled = true;
        DebugLog.printf("[recovery] scheduling reboot after %u failed Wi-Fi attempts\n",
                      static_cast<unsigned>(wifiManager->consecutiveFailureCount()));
        Serial.flush();
        requestRestartSequence("wifi_recovery", false);
    }

    if (!otaTransferActive && !recoveryRebootScheduled && mqttManager->shouldRebootForRecovery()) {
        recoveryRebootScheduled = true;
        DebugLog.printf("[recovery] scheduling reboot after %u failed MQTT attempts\n",
                      static_cast<unsigned>(mqttManager->consecutiveFailureCount()));
        Serial.flush();
        requestRestartSequence("mqtt_recovery", false);
    }

    // Never interrupt an inactive-partition write. A restart queued by another
    // subsystem waits until OTA has completed or aborted.
    if (!otaTransferActive && rebootRequested && static_cast<long>(millis() - rebootAt) >= 0) {
        logicDevice.shuttingDown();
        motorController.prepareForRestart();
        DebugLog.service(true);
        ESP.restart();
    }

    const bool latencySensitive = runtimeStateSnapshotInitialized &&
        (runtimeStateSnapshot.playback.state == "playing" || runtimeStateSnapshot.playback.state == "buffering" ||
         runtimeStateSnapshot.ota.busy);
    wifiManager->setLowLatencyMode(latencySensitive);
    delay(latencySensitive ? kActiveLoopDelayMs : kIdleLoopDelayMs);
}

#endif
