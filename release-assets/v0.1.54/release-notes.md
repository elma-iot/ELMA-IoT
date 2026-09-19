# v0.1.54 — centralized online Help

The device web interface now opens the public ELMA-IoT documentation site through lightweight, localized Help links. Major tabs and every Logics block route to a stable topic ID in the system browser. Full articles, tutorials, search data, and screenshots remain online and do not consume ESP flash or OTA headroom.

Standalone PlatformIO targets now select their matching board profile at build time, as Android and Windows builds already do. Unrelated board artwork and metadata are omitted while the existing dual-OTA partition layout remains compatible.

Recovery protection now prevents a saved radio stream or Logics automation from trapping a remote device in a reboot loop. Redirects are resolved before audio decoding, decoder/network buffers are reset between sources, unstable saved playback is prevented from resuming, and the responsible Logics group is stopped after a panic/watchdog reset. Replaying quarantined Logics requires an explicit confirmation in the web editor. Three consecutive abnormal resets activate a broader safe mode while Wi-Fi, the web interface, MQTT management, and OTA remain available.

The firmware can report its saved settings and editable Logics graph over USB before compilation. Android and Windows use this to compare the target with the open project and let the user keep device values, overwrite selected areas, or explicitly erase saved data. Erase remains off by default.

Configured buzzers now appear under Active peripherals and provide a Play action with Beep, Double beep, Alert, Doorbell, and Success presets. A Piano Melody can connect directly to Play Source; once connected, it replaces the preset and the editors collapse the preset selector.

The generic Buzzer hardware profile now correctly represents a bare two-wire buzzer with SIG and GND. Powered three-pin buzzer modules remain distinct hardware and are not shown under the generic profile.

Device-edited Logics now use a compact MessagePack record in NVS Preferences when no filesystem or SD card is mounted. Rebuildable contracts are omitted, older JSON records remain readable, and an inactive reboot-log checkpoint can be reclaimed if NVS is fragmented. OTA application-slot headroom is reported separately and no longer implies that a small graph cannot be persisted.

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
