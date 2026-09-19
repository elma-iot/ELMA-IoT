# v0.1.53 — stable cloned-device MQTT and retained DAC settings

MQTT connections use the configured client ID as a readable prefix and append a stable six-character hardware suffix. Multiple ESPs flashed from the same project therefore keep independent broker sessions instead of repeatedly disconnecting one another. MQTT topics and Home Assistant identifiers are unchanged.

Compiled Logics audio actions now target the active primary DAC initialized from saved settings. OTA can retain device settings without producing a stale compile-time DAC-pin mismatch.

Planned asset names:
- `esp32-notifier-v0.1.53.bin`
- `esp32-notifier-hacs-v0.1.53.bin`
- `esp32-notifier-hacs-slim-v0.1.53.bin`
- `esp32-notifier-hacs-legacy-ota-v0.1.53.bin`
- `esp32s3-notifier-v0.1.53.bin`
- `esp32s3-notifier-hacs-v0.1.53.bin`
- `esp32s3-notifier-hacs-slim-v0.1.53.bin`
- `esp32c3-notifier-hacs-v0.1.53.bin`
- `esp32-ota-bridge-v0.1.53.bin`
- `esp32s3-ota-bridge-v0.1.53.bin`
- `esp32c3-ota-bridge-v0.1.53.bin`
- `ELMA-Flasher-v0.1.56.exe`
