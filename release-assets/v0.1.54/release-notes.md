# v0.1.54 — centralized online Help

The device web interface now opens the public ELMA-IoT documentation site through lightweight, localized Help links. Major tabs and every Logics block route to a stable topic ID in the system browser. Full articles, tutorials, search data, and screenshots remain online and do not consume ESP flash or OTA headroom.

Standalone PlatformIO targets now select their matching board profile at build time, as Android and Windows builds already do. Unrelated board artwork and metadata are omitted while the existing dual-OTA partition layout remains compatible.

Recovery protection now prevents a saved radio stream or Logics automation from trapping a remote device in a reboot loop. Redirects are resolved before audio decoding, decoder/network buffers are reset between sources, unstable saved playback is prevented from resuming, and the responsible Logics group is stopped after a panic/watchdog reset. Replaying quarantined Logics requires an explicit confirmation in the web editor. Three consecutive abnormal resets activate a broader safe mode while Wi-Fi, the web interface, MQTT management, and OTA remain available.

The firmware can report its saved settings and editable Logics graph over USB before compilation. Android and Windows use this to compare the target with the open project and let the user keep device values, overwrite selected areas, or explicitly erase saved data. Erase remains off by default.

Configured buzzers now appear under Active peripherals and provide a Play action with Beep, Double beep, Alert, Doorbell, and Success presets. A Piano Melody can connect directly to Play Source; once connected, it replaces the preset and the editors collapse the preset selector.

The generic Buzzer hardware profile now correctly represents a bare two-wire buzzer with SIG and GND. Powered three-pin buzzer modules remain distinct hardware and are not shown under the generic profile.

Device-edited Logics now have a visible Save & apply control. Full 4 MiB USB installations reserve the final 64 KiB as LittleFS for editable graphs; compact chunked MessagePack in NVS remains the fallback when no filesystem or SD card is mounted. Rebuildable contracts are omitted, older records remain readable, and an inactive reboot-log checkpoint can be reclaimed if NVS is fragmented. OTA application-slot headroom is reported separately and does not describe graph-storage capacity. Existing saved graphs migrate in the stopped state, and an abnormal startup reset quarantines saved Logics until the user reviews and deliberately restarts them.

Nodes inside an automation group can now be repositioned individually. Dragging the group frame continues to move every contained node, and Ctrl-drag remains available for deliberate multi-node movement.

The Dynamic Peripheral Diagram now removes both light background gradients in dark mode. Saved legacy wire-control points are validated and excessive detours are replaced with orthogonal automatic routing, preventing power wires from cutting diagonally across the diagram.

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
- `ELMA-Flasher-v0.1.58.exe`
