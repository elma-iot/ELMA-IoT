# Firmware v0.1.59-test.69

Supported peripherals added in the device web interface now create runtime drivers even when absent from the compiled project. Saved driver identities survive reboot and configuration changes. Removing a peripheral releases its outputs and pauses Logics that still refer to it.

The web interface uses capabilities reported by the running image. Drivers omitted for size or unavailable on the target are excluded from new selections. Settings are checked for required pins, physical GPIO conflicts, driver instance limits and PWM capacity before saving. Compact boards gain web controls for their supported inputs and outputs.

Windows 0.1.111 and Android 1.0.48 include this source. Application interfaces remain bundled; compiler and board SDK packages are installed on demand. Configured images remain board and partition specific; this source release does not imply generic binaries or physical testing of every peripheral.

Expected generic binary naming conventions:

- `esp32-notifier-v0.1.59-test.69.bin`
- `esp32-notifier-hacs-v0.1.59-test.69.bin`
- `esp32-notifier-hacs-slim-v0.1.59-test.69.bin`
- `esp32-notifier-hacs-legacy-ota-v0.1.59-test.69.bin`
- `esp32s3-notifier-v0.1.59-test.69.bin`
- `esp32s3-notifier-hacs-v0.1.59-test.69.bin`
- `esp32s3-notifier-hacs-slim-v0.1.59-test.69.bin`
- `esp32c3-notifier-hacs-v0.1.59-test.69.bin`
- `esp32-ota-bridge-v0.1.59-test.69.bin`
- `esp32s3-ota-bridge-v0.1.59-test.69.bin`
- `esp32c3-ota-bridge-v0.1.59-test.69.bin`
- `ELMA-Flasher-v0.1.111.exe`
