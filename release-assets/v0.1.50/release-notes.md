# v0.1.50 — native Windows migration

Starts the native Qt configuration and wiring-canvas migration. Existing advanced tools remain available during transition.

Planned release asset names (not all are published):

- `esp32-notifier-v0.1.50.bin`
- `esp32-notifier-hacs-v0.1.50.bin`
- `esp32-notifier-hacs-slim-v0.1.50.bin`
- `esp32-notifier-hacs-legacy-ota-v0.1.50.bin`
- `esp32s3-notifier-v0.1.50.bin`
- `esp32s3-notifier-hacs-v0.1.50.bin`
- `esp32s3-notifier-hacs-slim-v0.1.50.bin`
- `esp32c3-notifier-hacs-v0.1.50.bin`
- `esp32-ota-bridge-v0.1.50.bin`
- `esp32s3-ota-bridge-v0.1.50.bin`
- `esp32c3-ota-bridge-v0.1.50.bin`
- `ELMA-Flasher-v0.1.52.exe`

Device-side Logics editing, live telemetry, saved automation groups and independent persistent execution controls. Windows is distributed separately through its private repository.

Preserves builder Logics through compilation; device palette above full-width canvas, context editing and compact group controls.

Fixes temporary-key lifetime corruption in Logics live JSON; round connectors override form minimum size; zoomed grid fills viewport.

Logics persists on SD/NVS when LittleFS is absent; runtime controls work despite persistence failures. Offline voice controls and connection movement added.

Fix FAT save replacement using alternating verified records; compact prefs graph. Hide moved wires during reconnection. Play applies pending edits first.
