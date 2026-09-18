# v0.1.52 — MQTT messages and display logic

Initial master mode is restored from compiled project defaults. Device web editing gains Android-equivalent selection, snapping, complete help, keyboard visibility and accessible piano record/play/pause/resume controls.

Event-triggered MQTT Publish message accepts separate Text and live Value inputs, with an optional separator and retained flag. QoS 1 uses the bounded acknowledged publish queue. Text nodes can append primitive values. Existing nodes gain optional ports without losing positions or connections.

Configured primary SSD1306/SH1106 OLEDs support temporary text and Clear text actions. Other display drivers remain explicitly unsupported. Existing relay ON/OFF/Toggle/Set/State behavior is unchanged.

Validated with 107 Windows tests, 120 web tests, the actual C++ runtime and Android emulator. Hardware MQTT broker/OLED testing remains outstanding.

Planned asset names:
- `esp32-notifier-v0.1.52.bin`
- `esp32-notifier-hacs-v0.1.52.bin`
- `esp32-notifier-hacs-slim-v0.1.52.bin`
- `esp32-notifier-hacs-legacy-ota-v0.1.52.bin`
- `esp32s3-notifier-v0.1.52.bin`
- `esp32s3-notifier-hacs-v0.1.52.bin`
- `esp32s3-notifier-hacs-slim-v0.1.52.bin`
- `esp32c3-notifier-hacs-v0.1.52.bin`
- `esp32-ota-bridge-v0.1.52.bin`
- `esp32s3-ota-bridge-v0.1.52.bin`
- `esp32c3-ota-bridge-v0.1.52.bin`
- `ELMA-Flasher-v0.1.55.exe`
