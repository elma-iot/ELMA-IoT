#pragma once
#include "settings_schema.h"
#include "app_state.h"
namespace ExternalDisplays {
void configure(const SettingsBundle&);
void loop(const AppStateSnapshot&);
bool prepareUpdate();
void progress(uint8_t percent);
void endUpdate();
void temporaryText(const String&,uint32_t durationMs);
bool available();
void powerOff();
}
