# v0.1.54 — centralized online Help

The device web interface now opens the public ELMA-IoT documentation site through lightweight, localized Help links. Major tabs and every Logics block route to a stable topic ID in the system browser. Full articles, tutorials, search data, and screenshots remain online and do not consume ESP flash or OTA headroom.

Standalone PlatformIO targets now select their matching board profile at build time, as Android and Windows builds already do. Unrelated board artwork and metadata are omitted while the existing dual-OTA partition layout remains compatible.

Planned asset names:
- `esp32-notifier-v0.1.54.bin`
- `esp32-notifier-hacs-v0.1.54.bin`
- `esp32-notifier-hacs-slim-v0.1.54.bin`
- `esp32-notifier-hacs-legacy-ota-v0.1.54.bin`
- `esp32s3-notifier-v0.1.54.bin`
- `esp32s3-notifier-hacs-v0.1.54.bin`
- `esp32s3-notifier-hacs-slim-v0.1.54.bin`
- `esp32c3-notifier-hacs-v0.1.54.bin`
- `esp32-ota-bridge-v0.1.54.bin`
- `esp32s3-ota-bridge-v0.1.54.bin`
- `esp32c3-ota-bridge-v0.1.54.bin`
- `ELMA-Flasher-v0.1.57.exe`
