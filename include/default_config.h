#pragma once

#include <stdint.h>

#ifndef APP_DEFAULT_OLED_SDA_PIN
#define APP_DEFAULT_OLED_SDA_PIN 23
#endif

#ifndef APP_DEFAULT_OLED_SCL_PIN
#define APP_DEFAULT_OLED_SCL_PIN 19
#endif

#ifndef APP_DEFAULT_STATUS_LED_PIN
#define APP_DEFAULT_STATUS_LED_PIN 22
#endif

#ifndef APP_STATUS_LED_IS_NEOPIXEL
#define APP_STATUS_LED_IS_NEOPIXEL 0
#endif

#ifndef APP_DEFAULT_OLED_ENABLED
#define APP_DEFAULT_OLED_ENABLED 1
#endif

#ifndef APP_DEFAULT_BATTERY_ADC_PIN
#define APP_DEFAULT_BATTERY_ADC_PIN 36
#endif

#ifndef APP_DEFAULT_BUTTON1_PIN
#define APP_DEFAULT_BUTTON1_PIN 5
#endif

#ifndef APP_DEFAULT_BUTTON2_PIN
	#if defined(CONFIG_IDF_TARGET_ESP32)
		#define APP_DEFAULT_BUTTON2_PIN 18
	#else
		#define APP_DEFAULT_BUTTON2_PIN 6
	#endif
#endif

#ifndef APP_DEFAULT_I2S_DOUT_PIN
#define APP_DEFAULT_I2S_DOUT_PIN 25
#endif

#ifndef APP_DEFAULT_I2S_WS_PIN
#define APP_DEFAULT_I2S_WS_PIN 26
#endif

#ifndef APP_DEFAULT_I2S_BCLK_PIN
#define APP_DEFAULT_I2S_BCLK_PIN 27
#endif

#ifndef APP_AUDIO_FORCE_MONO
#define APP_AUDIO_FORCE_MONO 1
#endif

#ifndef APP_AUDIO_MAX_HARDWARE_VOLUME
#define APP_AUDIO_MAX_HARDWARE_VOLUME 18
#endif

#ifndef APP_AUDIO_DIAGNOSTIC_TEST
#define APP_AUDIO_DIAGNOSTIC_TEST 0
#endif

#ifndef APP_AUDIO_DIAGNOSTIC_LIBRARY_VOLUME
#define APP_AUDIO_DIAGNOSTIC_LIBRARY_VOLUME 6
#endif

#ifndef APP_AUDIO_DIAGNOSTIC_STREAM_URL
#define APP_AUDIO_DIAGNOSTIC_STREAM_URL "http://icecast.radiofrance.fr/fip-midfi.mp3"
#endif

#ifndef APP_AUDIO_DIAGNOSTIC_PREFERRED_SAMPLE_RATE_HZ
#define APP_AUDIO_DIAGNOSTIC_PREFERRED_SAMPLE_RATE_HZ 48000
#endif

namespace DefaultConfig {

constexpr char WIFI_SSID[] = "";
constexpr char WIFI_PASSWORD[] = "";
constexpr bool WIFI_AP_FALLBACK_ENABLED = true;
constexpr char WIFI_AP_SSID_PREFIX[] = "ELMA-IoT";
constexpr char WIFI_AP_PASSWORD[] = "12345678";

constexpr char DEVICE_NAME[] = "elma-iot";
constexpr char FRIENDLY_NAME[] = "ELMA IoT";

constexpr char MQTT_HOST[] = "";
constexpr uint16_t MQTT_PORT = 1883;
constexpr char MQTT_USERNAME[] = "";
constexpr char MQTT_PASSWORD[] = "";
constexpr char MQTT_BASE_TOPIC[] = "elma_iot";
constexpr bool MQTT_DISCOVERY_ENABLED = true;

constexpr char OTA_OWNER[] = "elma-iot";
constexpr char OTA_REPOSITORY[] = "ELMA-IoT";
constexpr char OTA_CHANNEL[] = "stable";
constexpr char OTA_ASSET_TEMPLATE[] = "esp32-notifier-${version}.bin";
constexpr char OTA_MANIFEST_URL[] = "";
constexpr bool OTA_ALLOW_INSECURE_TLS = true;

#if defined(CONFIG_IDF_TARGET_ESP32S3)
constexpr float BATTERY_CALIBRATION = 2.0f;
#else
constexpr float BATTERY_CALIBRATION = 3.866f;
#endif
constexpr uint32_t BATTERY_UPDATE_INTERVAL_MS = 10000;
constexpr uint16_t BATTERY_MOVING_AVERAGE_WINDOW = 10;

#if defined(CONFIG_IDF_TARGET_ESP32S3)
constexpr uint8_t DEFAULT_VOLUME_PERCENT = 35;
#else
constexpr uint8_t DEFAULT_VOLUME_PERCENT = 5;
#endif
constexpr bool DEFAULT_AUDIO_MUTED = false;
constexpr uint8_t BUTTON_VOLUME_STEP_PERCENT = 5;
// ESP32-audioI2S allocates exactly one of these buffers: PSRAM when present,
// otherwise the smaller internal-RAM fallback. Keep the large streaming queue
// out of SRAM on ceiling-speaker builds.
// Leave contiguous internal RAM for Wi-Fi RX and web requests on non-PSRAM
// boards. A larger ring can leave ample total heap but no packet-sized block.
constexpr int AUDIO_BUFFER_SIZE_RAM = 16 * 1024;
constexpr int AUDIO_BUFFER_SIZE_PSRAM = 1280 * 1024;
constexpr bool LOW_BATTERY_SLEEP_ENABLED = false;
constexpr uint8_t LOW_BATTERY_SLEEP_THRESHOLD_PERCENT = 20;
constexpr uint16_t LOW_BATTERY_WAKE_INTERVAL_MINUTES = 15;
constexpr bool POWER_CYCLE_FACTORY_RESET_ENABLED = true;
constexpr bool TOUCH_HOLD_FACTORY_RESET_ENABLED = true;

constexpr bool WEB_AUTH_ENABLED = false;
constexpr char WEB_USERNAME[] = "admin";
constexpr char WEB_PASSWORD[] = "admin";

constexpr bool OLED_ENABLED = APP_DEFAULT_OLED_ENABLED;
constexpr uint8_t OLED_I2C_ADDRESS = 0x3C;
constexpr char OLED_DRIVER[] = "ssd1306";
constexpr uint8_t OLED_WIDTH = 128;
constexpr uint8_t OLED_HEIGHT = 64;
constexpr uint16_t OLED_ROTATION = 0;
constexpr uint8_t OLED_SDA_PIN = APP_DEFAULT_OLED_SDA_PIN;
constexpr uint8_t OLED_SCL_PIN = APP_DEFAULT_OLED_SCL_PIN;
constexpr int8_t OLED_RESET_PIN = -1;
constexpr uint16_t OLED_DIM_TIMEOUT_SECONDS = 0;

constexpr uint8_t STATUS_LED_PIN = APP_DEFAULT_STATUS_LED_PIN;
constexpr bool STATUS_LED_IS_NEOPIXEL = APP_STATUS_LED_IS_NEOPIXEL;
constexpr const char* STATUS_LED_TYPE = APP_STATUS_LED_IS_NEOPIXEL ? "neopixel" : "regular";
constexpr uint8_t BATTERY_ADC_PIN = APP_DEFAULT_BATTERY_ADC_PIN;
constexpr uint8_t BUTTON1_PIN = APP_DEFAULT_BUTTON1_PIN;
constexpr uint8_t BUTTON2_PIN = APP_DEFAULT_BUTTON2_PIN;
constexpr char BUTTON1_DEFAULT_ACTION[] = "previous";
constexpr char BUTTON2_DEFAULT_ACTION[] = "next";
constexpr uint8_t I2S_DOUT_PIN = APP_DEFAULT_I2S_DOUT_PIN;
constexpr uint8_t I2S_WS_PIN = APP_DEFAULT_I2S_WS_PIN;
constexpr uint8_t I2S_BCLK_PIN = APP_DEFAULT_I2S_BCLK_PIN;
constexpr bool AUDIO_FORCE_MONO = APP_AUDIO_FORCE_MONO;
constexpr uint8_t AUDIO_MAX_HARDWARE_VOLUME = APP_AUDIO_MAX_HARDWARE_VOLUME;
constexpr bool AUDIO_DIAGNOSTIC_TEST = APP_AUDIO_DIAGNOSTIC_TEST;
constexpr uint8_t AUDIO_DIAGNOSTIC_LIBRARY_VOLUME = APP_AUDIO_DIAGNOSTIC_LIBRARY_VOLUME;
constexpr char AUDIO_DIAGNOSTIC_STREAM_URL[] = APP_AUDIO_DIAGNOSTIC_STREAM_URL;
constexpr uint32_t AUDIO_DIAGNOSTIC_PREFERRED_SAMPLE_RATE_HZ = APP_AUDIO_DIAGNOSTIC_PREFERRED_SAMPLE_RATE_HZ;

}  // namespace DefaultConfig
