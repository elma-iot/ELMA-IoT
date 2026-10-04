#include "panel_settings.h"
#include <cassert>
int main() {
  JsonDocument settings, value;
  deserializeJson(settings, R"({"wifi":{"ssid":"saved","password":"secret"},"ui":{"arrays":[{"count":32},{"count":16}]}})");
  value.set(24);
  assert(PanelSettings::set(settings.as<JsonVariant>(), "ui/arrays/1/count", value.as<JsonVariantConst>()));
  assert(PanelSettings::get(settings.as<JsonVariantConst>(), "ui/arrays/1/count")==24);
  assert(settings["ui"]["arrays"][0]["count"]==32);
  assert(settings["wifi"]["password"]=="secret");
  value.set(20);
  assert(PanelSettings::set(settings.as<JsonVariant>(), "ui/newField", value.as<JsonVariantConst>()));
  assert(settings["ui"]["newField"]==20);
  value.set("updated");
  assert(PanelSettings::set(settings.as<JsonVariant>(), "wifi/ssid", value.as<JsonVariantConst>()));
  assert(settings["wifi"]["ssid"]=="updated");
  assert(settings["wifi"]["password"]=="secret");
  assert(!PanelSettings::acknowledged("local", "remote"));
  assert(PanelSettings::acknowledged("local", "local"));
}
