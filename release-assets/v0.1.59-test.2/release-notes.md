# Firmware source 0.1.59-test.2

Testing source for the Windows 0.1.84 bundle. Includes the VIEWE LCD settings-stack fix, light SD confirmation, MS1285 Modbus monitor and nodes, corrected VIEWE audio and connector profiles, and MCP2551 classic CAN Configure/Send/Received/Status nodes. CAN requires a configured transceiver, compatible ESP32 and safe 3.3 V receive level shifting. Physical CAN/RS485 equipment is not yet validated. No SD formatting runs without explicit confirmation.

Host tests cover frame validation, graph routing, board gating and LCD dialog layout. See the Windows testing release notes for device verification results.

## Expected asset names

These names are the version-validation/build contract; this source note does not assert that every generic binary has been published.

- `esp32-notifier-v0.1.59-test.2.bin`
- `esp32-notifier-hacs-v0.1.59-test.2.bin`
- `esp32-notifier-hacs-slim-v0.1.59-test.2.bin`
- `esp32-notifier-hacs-legacy-ota-v0.1.59-test.2.bin`
- `esp32s3-notifier-v0.1.59-test.2.bin`
- `esp32s3-notifier-hacs-v0.1.59-test.2.bin`
- `esp32s3-notifier-hacs-slim-v0.1.59-test.2.bin`
- `esp32c3-notifier-hacs-v0.1.59-test.2.bin`
- `esp32-ota-bridge-v0.1.59-test.2.bin`
- `esp32s3-ota-bridge-v0.1.59-test.2.bin`
- `esp32c3-ota-bridge-v0.1.59-test.2.bin`

Windows bundle: `ELMA-Flasher-v0.1.84.exe`.
