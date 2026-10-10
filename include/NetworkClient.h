#pragma once
#ifdef ESP32
#include <esp_arduino_version.h>
#endif
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
#include <Network.h>
#else
#include <WiFiClient.h>
using NetworkClient = WiFiClient;
#endif
