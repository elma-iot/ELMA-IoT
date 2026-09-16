#include "device_log.h"
#include "sound_effects.h"

namespace {
uint8_t clampVolume(uint8_t volumePercent) {
    return volumePercent > 100 ? 100 : volumePercent;
}
}  // namespace

void SoundEffectsManager::begin(const SettingsBundle& settings) {
    applySettings(settings);
}

void SoundEffectsManager::applySettings(const SettingsBundle& settings) {
    muted_ = settings.device.audioMuted;
    volumePercent_ = clampVolume(settings.device.savedVolumePercent);
}

void SoundEffectsManager::setMuted(bool muted) {
    muted_ = muted;
}

void SoundEffectsManager::setVolumePercent(uint8_t volumePercent) {
    volumePercent_ = clampVolume(volumePercent);
}

void SoundEffectsManager::playBoot() {
    playEffect("boot");
}

void SoundEffectsManager::playWifiConnected() {
    playEffect("wifi_connected");
}

void SoundEffectsManager::playWifiDisconnected() {
    playEffect("wifi_disconnected");
}

void SoundEffectsManager::playMqttConnected() {
    playEffect("mqtt_connected");
}

void SoundEffectsManager::playPlaybackStart() {
    playEffect("playback_start");
}

void SoundEffectsManager::playPlaybackStop() {
    playEffect("playback_stop");
}

void SoundEffectsManager::playEffect(const char* effectName) {
    if (muted_) {
        return;
    }
    DebugLog.printf("[sfx] %s requested at %u%%\n", effectName, volumePercent_);
}