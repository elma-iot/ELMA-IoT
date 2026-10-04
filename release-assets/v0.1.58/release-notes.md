# Firmware source 0.1.58

- Compact Array / PIXEL / Effect configuration, independent chained strips/rings/panels, DOUT-to-DIN diagrams, and optional synchronization to Array 1.
- WLED controls and Configuration share settings; fixed rapid-edit/save races. Live diagram effects follow device telemetry; Logics can override configured defaults.
- Board changes preserve compatible peripherals and Logics identities, adapt GPIO assignments, and explain unsupported or over-capacity configurations.
- Test Logics checks the selected board, firmware validation and a 60-second scheduler simulation before compiling. Offers unambiguous reference repairs with confirmation and undo.
- Aligned native board/array pin labels. Device web wiring remains independent.
- Audio-enabled ESP32/S3 builds offer stream and I2S microphone spectrum, VU and pulse effects using bounded background visual processing.
- Configured arrays indicate AP/STA boot state. Orderly restarts blank LED outputs; abrupt resets clear arrays at initialization. Hardware watchdog/panic resets cannot guarantee cleanup before resetting.
- Updated online help and WINDOWS-ANDROID-PARITY.md for the future Android port. Android APK is unchanged.

Validation includes selected-board migration regressions, native pin geometry, settings-save race tests, LED shutdown tests, C3/ESP8266 builds and C3 OTA/web-reboot checks. Physical audio quality and every board/peripheral combination remain unverified. Large ESP8266 graphs can still exceed the 32 KiB compiled graph contract; Test Logics reports this limit.


## Expected asset names

These names are the version-validation/build contract; this source note does not assert that every generic binary has been published.

- `esp32-notifier-v0.1.58.bin`
- `esp32-notifier-hacs-v0.1.58.bin`
- `esp32-notifier-hacs-slim-v0.1.58.bin`
- `esp32-notifier-hacs-legacy-ota-v0.1.58.bin`
- `esp32s3-notifier-v0.1.58.bin`
- `esp32s3-notifier-hacs-v0.1.58.bin`
- `esp32s3-notifier-hacs-slim-v0.1.58.bin`
- `esp32c3-notifier-hacs-v0.1.58.bin`
- `esp32-ota-bridge-v0.1.58.bin`
- `esp32s3-ota-bridge-v0.1.58.bin`
- `esp32c3-ota-bridge-v0.1.58.bin`
