#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>

#include "settings_schema.h"
bool isSafeOutputPinForBoard(uint8_t pin);

class SettingsManager {
  public:
    bool begin();
    SettingsBundle load();
    bool save(const SettingsBundle& settings);
    bool saveAudioEqualizer(const AudioSettings& audio);
    bool reset();
    SettingsBundle defaults() const;
    void toJson(const SettingsBundle& settings, JsonObject root, const char* section=nullptr, bool editorState=true) const;
    bool updateFromJson(SettingsBundle& settings, JsonVariantConst root, String& error) const;

  private:
    Preferences preferences_;
    bool writeFailed_ = false;

    SettingsBundle sanitize(const SettingsBundle& input) const;
    bool writeStringIfChanged(const char* key, const String& value);
    bool writeBoolIfChanged(const char* key, bool value);
    bool writeUIntIfChanged(const char* key, uint32_t value);
    bool writeIntIfChanged(const char* key, int32_t value);
    bool writeFloatIfChanged(const char* key, float value);
    String readString(const char* key, const String& fallback);
    bool readBool(const char* key, bool fallback);
    uint32_t readUInt(const char* key, uint32_t fallback);
    int32_t readInt(const char* key, int32_t fallback);
    float readFloat(const char* key, float fallback);
};
