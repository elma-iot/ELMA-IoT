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
