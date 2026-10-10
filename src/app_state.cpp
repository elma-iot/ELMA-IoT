#include "app_state.h"

#include <cstring>
#include <ctime>

namespace {
void lockAppState(SemaphoreHandle_t mutex) {
    if (mutex != nullptr) {
        xSemaphoreTake(mutex, portMAX_DELAY);
    }
}

void unlockAppState(SemaphoreHandle_t mutex) {
    if (mutex != nullptr) {
        xSemaphoreGive(mutex);
    }
}
}  // namespace

AppState::AppState() : mutex_(nullptr), mutexBuffer_{} {}

AppState::~AppState() {
    mutex_ = nullptr;
}

bool AppState::begin() {
    return ensureMutex();
}

bool AppState::ensureMutex() const {
    if (mutex_ != nullptr) {
        return true;
    }

    memset(&mutexBuffer_, 0, sizeof(mutexBuffer_));
    mutex_ = xSemaphoreCreateMutexStatic(&mutexBuffer_);
    return mutex_ != nullptr;
}

void AppState::setDevice(const String& deviceName, const String& friendlyName, bool usingSaved, int16_t clockUtcOffsetMinutes) {
    if (!ensureMutex()) {
        return;
    }
    lockAppState(mutex_);
    state_.device.deviceName = deviceName;
    state_.device.friendlyName = friendlyName;
    state_.device.clockUtcOffsetMinutes = clockUtcOffsetMinutes;
    state_.settings.usingSaved = usingSaved;
    unlockAppState(mutex_);
}

void AppState::setWiFiStatus(bool connected, bool apMode, const String& ssid, const IPAddress& ip, int32_t rssi, const String& apSsid) {
    if (!ensureMutex()) {
        return;
    }
    lockAppState(mutex_);
    state_.network.wifiConnected = connected;
    state_.network.apMode = apMode;
    state_.network.ssid = ssid;
    state_.network.ip = ip.toString();
    state_.network.wifiRssi = rssi;
    state_.network.apSsid = apSsid;
    unlockAppState(mutex_);
}

void AppState::setMqttConnected(bool connected) {
    if (!ensureMutex()) {
        return;
    }
    lockAppState(mutex_);
    state_.network.mqttConnected = connected;
    unlockAppState(mutex_);
}

void AppState::setPlayback(const String& state, const String& type, const String& title, const String& url, const String& source, uint8_t volumePercent) {
    if (!ensureMutex()) {
        return;
    }
    lockAppState(mutex_);
    if (state_.playback.state == state && state_.playback.type == type && state_.playback.title == title &&
        state_.playback.url == url && state_.playback.source == source && state_.playback.volumePercent == volumePercent) {
        unlockAppState(mutex_);
        return;
    }
    state_.playback.state = state;
    state_.playback.type = type;
    state_.playback.title = title;
    state_.playback.url = url;
    state_.playback.source = source;
    state_.playback.volumePercent = volumePercent;
    if (url.isEmpty()) {
        state_.playback.positionSeconds = 0;
        state_.playback.durationSeconds = 0;
    }
    unlockAppState(mutex_);
}

void AppState::markPlaybackCompleted(const String& url, const String& source) {
    if (!ensureMutex()) {
        return;
    }
    lockAppState(mutex_);
    state_.playback.completionSequence++;
    state_.playback.completedUrl = url;
    state_.playback.completedSource = source;
    unlockAppState(mutex_);
}

void AppState::setPlaybackProgress(uint32_t positionSeconds, uint32_t durationSeconds) {
    if (!ensureMutex()) {
        return;
    }
    lockAppState(mutex_);
    state_.playback.positionSeconds = positionSeconds;
    state_.playback.durationSeconds = durationSeconds;
    unlockAppState(mutex_);
}

void AppState::setBattery(float voltage, float rawAdcVoltage, uint16_t rawAdc, bool charging) {
    if (!ensureMutex()) {
        return;
    }
    lockAppState(mutex_);
    state_.battery.voltage = voltage;
    state_.battery.rawAdcVoltage = rawAdcVoltage;
    state_.battery.rawAdc = rawAdc;
    state_.battery.charging = charging;
    unlockAppState(mutex_);
}

void AppState::setOta(bool busy, bool updateAvailable, const String& latestVersion, const String& lastResult, const String& lastError,
                      const String& phase, uint8_t progressPercent) {
    if (!ensureMutex()) {
        return;
    }
    lockAppState(mutex_);
    state_.ota.busy = busy;
    state_.ota.updateAvailable = updateAvailable;
    state_.ota.latestVersion = latestVersion;
    state_.ota.lastResult = lastResult;
    state_.ota.lastError = lastError;
    state_.ota.phase = phase;
    state_.ota.progressPercent = progressPercent;
    unlockAppState(mutex_);
}

void AppState::setInputs(const InputChannelSnapshot& button1, const InputChannelSnapshot& button2) {
    if (!ensureMutex()) {
        return;
    }
    lockAppState(mutex_);
    state_.input.button1 = button1;
    state_.input.button2 = button2;
    unlockAppState(mutex_);
}

void AppState::setLastError(const String& lastError) {
    if (!ensureMutex()) {
        return;
    }
    lockAppState(mutex_);
    if (state_.system.lastError == lastError) {
        unlockAppState(mutex_);
        return;
    }
    state_.system.lastError = lastError;
    unlockAppState(mutex_);
}

void AppState::setFreeHeap(uint32_t freeHeap) {
    if (!ensureMutex()) {
        return;
    }
    lockAppState(mutex_);
    if (state_.system.freeHeap == freeHeap) {
        unlockAppState(mutex_);
        return;
    }
    state_.system.freeHeap = freeHeap;
    unlockAppState(mutex_);
}

AppStateSnapshot AppState::snapshot() const {
    if (!ensureMutex()) {
        return state_;
    }
    lockAppState(mutex_);
    AppStateSnapshot copy = state_;
    unlockAppState(mutex_);
    return copy;
}

void AppState::toJson(JsonObject root) const {
    const AppStateSnapshot copy = snapshot();
    JsonObject device = root["device"].to<JsonObject>();
    device["deviceName"] = copy.device.deviceName;
    device["friendlyName"] = copy.device.friendlyName;
    device["clockUtcOffsetMinutes"] = copy.device.clockUtcOffsetMinutes;
    const time_t clockNow = time(nullptr);
    JsonObject clock = root["clock"].to<JsonObject>();
    clock["synced"] = clockNow > 1577836800;
    clock["utc"] = clockNow > 1577836800 ? int64_t(clockNow) : int64_t(0);

    JsonObject network = root["network"].to<JsonObject>();
    network["wifiConnected"] = copy.network.wifiConnected;
    network["apMode"] = copy.network.apMode;
    network["mqttConnected"] = copy.network.mqttConnected;
    network["ssid"] = copy.network.ssid;
    network["ip"] = copy.network.ip;
    network["apSsid"] = copy.network.apSsid;
    network["wifiRssi"] = copy.network.wifiRssi;

    JsonObject playback = root["playback"].to<JsonObject>();
    playback["state"] = copy.playback.state;
    playback["type"] = copy.playback.type;
    playback["title"] = copy.playback.title;
    playback["url"] = copy.playback.url;
    playback["source"] = copy.playback.source;
    playback["volumePercent"] = copy.playback.volumePercent;
    playback["completionSequence"] = copy.playback.completionSequence;
    playback["completedUrl"] = copy.playback.completedUrl;
    playback["completedSource"] = copy.playback.completedSource;
    playback["positionSeconds"] = copy.playback.positionSeconds;
    playback["durationSeconds"] = copy.playback.durationSeconds;

    JsonObject battery = root["battery"].to<JsonObject>();
    battery["voltage"] = copy.battery.voltage;
    battery["rawAdcVoltage"] = copy.battery.rawAdcVoltage;
    battery["rawAdc"] = copy.battery.rawAdc;
    battery["charging"] = copy.battery.charging;

    JsonObject ota = root["ota"].to<JsonObject>();
    ota["busy"] = copy.ota.busy;
    ota["updateAvailable"] = copy.ota.updateAvailable;
    ota["latestVersion"] = copy.ota.latestVersion;
    ota["lastResult"] = copy.ota.lastResult;
    ota["lastError"] = copy.ota.lastError;
    ota["phase"] = copy.ota.phase;
    ota["progressPercent"] = copy.ota.progressPercent;

    JsonObject settings = root["settings"].to<JsonObject>();
    settings["usingSaved"] = copy.settings.usingSaved;

    JsonObject input = root["input"].to<JsonObject>();
    JsonObject button1 = input["button1"].to<JsonObject>();
    button1["configuredIndex"] = copy.input.button1.configuredIndex;
    button1["pin"] = copy.input.button1.pin;
    button1["profile"] = copy.input.button1.profile;
    button1["nativeTouch"] = copy.input.button1.nativeTouch;
    button1["touchSupported"] = copy.input.button1.touchSupported;
    button1["active"] = copy.input.button1.active;
    button1["sensitivityPercent"] = copy.input.button1.sensitivityPercent;
    button1["rawValue"] = copy.input.button1.rawValue;
    button1["baselineValue"] = copy.input.button1.baselineValue;

    JsonObject button2 = input["button2"].to<JsonObject>();
    button2["configuredIndex"] = copy.input.button2.configuredIndex;
    button2["pin"] = copy.input.button2.pin;
    button2["profile"] = copy.input.button2.profile;
    button2["nativeTouch"] = copy.input.button2.nativeTouch;
    button2["touchSupported"] = copy.input.button2.touchSupported;
    button2["active"] = copy.input.button2.active;
    button2["sensitivityPercent"] = copy.input.button2.sensitivityPercent;
    button2["rawValue"] = copy.input.button2.rawValue;
    button2["baselineValue"] = copy.input.button2.baselineValue;

    JsonObject system = root["system"].to<JsonObject>();
    system["freeHeap"] = copy.system.freeHeap;
    system["lastError"] = copy.system.lastError;
}
