# ELMA IoT

![ELMA IoT logo](Docs/elma_iot_logo.svg)

ELMA IoT stands for Elnur Mehdiyev Automation and Internet of Things. This project is a custom PlatformIO firmware base for ESP32 and ESP32-S3 home automation devices with a browser-based configuration UI, MQTT and Home Assistant integration, local storage management, GitHub-release-based update discovery, and configurable pin mapping for audio, OLED, battery, SD, and status hardware.

Project story and current device write-up:

- Drive2 article: https://www.drive2.ru/c/735319567747779562/


## Current Release

- Firmware version: `v0.1.54`
- Primary release repository: `elma-iot/ELMA-IoT`
- GitHub Releases feed: `https://api.github.com/repos/elma-iot/ELMA-IoT/releases`
- Default ESP32-S3 HACS asset: `esp32s3-notifier-hacs-v0.1.54.bin`

v0.1.54 is the current firmware source version. Compile configured images using the Windows or Android application. No generic firmware binary release is published for this source version. Windows application bundles are available only in the private ELMA-IoT-Windows repository.

### v0.1.54 highlights

The device web interface now opens centralized online documentation through lightweight localized Help links. Every Logics block routes to its stable topic ID; no manuals or screenshots consume firmware flash or OTA headroom.

Standalone PlatformIO targets now select their matching board profile at build time, just as Android and Windows already do. This excludes unrelated board artwork and metadata from the image and preserves the existing dual-OTA partition layout and update compatibility.

### v0.1.53 highlights

MQTT broker sessions now append a stable per-chip hardware suffix to the configured client ID, preventing cloned devices from repeatedly disconnecting each other. The configured topic and Home Assistant identity remain unchanged.

Logics audio actions follow the active saved primary DAC after OTA, so retained GPIO settings no longer invalidate an otherwise compatible compiled graph.

### v0.1.52 highlights

Windows and Android now share the current core device-web Logics editor. Compiled initial master mode is restored correctly; selection, compatible connector snapping, keyboard visibility, complete node help and piano recording/preview controls receive parity fixes. Bare builds use compact inactive-peripheral blocks.

MQTT Publish message sends separate Text and live Value inputs using the bounded QoS 1 queue. Text can append primitive values; configured primary SSD1306/SH1106 OLEDs support temporary text and Clear text actions. Existing relay controls remain available.

### Earlier releases

- Classic ESP32 internal temperature monitoring rejects the invalid raw-128 conversion (53.3 C), retains valid samples for at most 30 seconds, and displays sample age. Values are labelled estimates because the legacy API has no factory-calibrated accuracy guarantee. No smoothing hides temperature changes; expired readings become unavailable. Other targets retain their sensor driver.
- Last saved desktop configurations autoload, and legacy settings without peripheral-profile metadata restore audio/display/storage selections from their enabled hardware settings. Explicitly saved selections remain authoritative.
- Idle station mode enables Wi-Fi modem sleep again; playback and OTA retain low-latency operation. The Logs viewer polls only while visible and does not keep the radio permanently awake.
- Device-specific Full builds embed only the selected board's HTML option, JavaScript metadata and SVG. All peripheral definitions and illustrations remain available; the desktop Designer retains the full board catalogue.
- Flash assets stream directly from immutable flash through a bounded TCP window. Non-PSRAM audio uses a 16 KiB input allocation to preserve contiguous Wi-Fi memory. AsyncTCP 3.5.0 and ESPAsyncWebServer 3.7.6 are pinned for reproducible builds.
- Playback waits for an already-running release check to free its TLS buffers. Background release checks defer while playback is active; requested firmware installs still release playback resources before starting.
- Logs retain current/previous boot tails internally, shrinking checkpoints if configuration fills NVS. With SD available, `/rebootlog.txt` rotates at 10 MiB. The Logs tab provides a scrollable text view and Copy button.
- MQTT state changes are coalesced onto the main loop, discovery is paced, and at most four QoS1 publishes can await acknowledgement. Client connection strings have persistent ownership, and low-memory publishes wait instead of exhausting the heap.
- JSON web responses use one checked allocation instead of a repeatedly growing stream buffer. Upload-status routing returns the correct resume offset. Firmware transfers release audio decoder/network buffers and temporarily prevent playback; automatic startup release checks wait while saved radio playback starts.
- The low-battery deep-sleep checkbox, threshold and wake interval are now visible in the Battery tab in both ELMA Flasher and the full on-device configurator. Deep sleep remains disabled by default.
- A missing, disconnected or implausible battery ADC reading can no longer put the device into a sleep loop. Firmware keeps a 30-second post-boot configuration window and clears the power-cycle guard before deliberate sleep.
- The upgrade performs a one-time cleanup of the legacy power-cycle counter, protecting devices that accumulated resets while older firmware repeatedly entered low-battery sleep.
- LAN discovery uses a more tolerant timeout for weak Wi-Fi devices while remaining off the UI thread.

### v0.1.42 highlights

- ELMA Flasher detects classic ESP32 devices running ELMA v0.1.10/v0.1.11 and automatically compiles a legacy-partition-compatible image for their 0x190000-byte OTA slot instead of attempting an oversized normal image.
- The compatibility image preserves NVS settings and operational audio, MQTT, GPIO, display and control code, while replacing the large illustrated browser configurator with a compact recovery page. The flasher reapplies the selected configuration after the device restarts.
- LAN discovery gives the manually entered IP a longer direct probe before the parallel subnet scan, improving detection of weak or slow devices. The new application version can also start independently while an older flasher is still open.

### v0.1.41 highlights

- The Wi-Fi tab in ELMA Flasher and Full device firmware has separate STA/AP transmit-power bars (2–19.5 dBm requested). Apply saves the two power values; the device reports the actual driver limit, which can be rounded/capped by the chip. This changes transmit power, not received RSSI. ESP32 uses one radio-wide limit: AP+STA uses the higher request. Lower power can disconnect a weak link.
- Power selections are included in saved configuration, USB provisioning and compiled Full-image defaults. Existing saved device power values take precedence over OTA defaults; adjust them on the device's Wi-Fi page. Generic release images retain the previous 15 dBm default. Minimal recovery deliberately uses its maximum recovery-power request and does not overwrite the saved Full configuration.
- Firmware recovery reboots wait while an OTA write is active. The PC flasher sends 8 KiB chunks, resumes after interrupted acknowledgements, and supports the older ELMA multipart upload endpoint used by v0.1.27.
- Full/Minimal firmware selection is available beneath Flash connection and Target chip. Compile and Save honors the same selection. Unchanged IP-upload retries reuse a SHA-256-checked compiled image; configuration or source changes invalidate it.
- Minimal recovery images for ESP32, ESP32-S3 and ESP32-C3 contain Wi-Fi and a small resumable OTA page, omit peripheral modules and board illustrations, disable Wi-Fi sleep, and remove the old STA transmit-power ceiling. They read existing ELMA Wi-Fi configuration without rewriting the saved `notifier` NVS namespace. Install Minimal over OTA, then replace it with Full firmware for the same chip; it is not a normal operating image.
- The flasher supports non-blocking LAN discovery, target-chip checks, confirmation before replacing foreign firmware, and cancellation at safe transfer boundaries. Tasmota/ESPHome configuration import provides a reviewable migration path; ESPHome GPIO mapping requires its source YAML rather than guessing hidden configuration from the running device.
- Configuration files have native Open, Save and Save As workflows; the board autodetect control is visible and manual selection remains available in the EXE. Device firmware includes only the selected board illustration while retaining compatible peripherals.
- Motor/control assignment, state reporting and web-edit responsiveness fixes are included, together with separated GitHub release and firmware-web handlers.

Recovery note: the smaller image helps weak links but cannot repair antenna, power-supply or access-point faults. Back up configuration before updating. These builds are compile/regression tested; a successful build is not a live-device OTA guarantee.

### v0.1.40 development highlights

- ELMA Flasher now permits one instance per Windows user. Launching it again restores and activates the current startup or Designer window.
- The portable EXE opens an ELMA-styled startup window immediately on first launch, reports byte-accurate extraction progress with smooth animation, validates the embedded runtime, and starts the Designer only after reaching 100%.
- Heavy compiler, Qt WebEngine and ESP support files are cached under a versioned per-user runtime after their first verified extraction, avoiding PyInstaller's previous 243 MB unpack on every start.
- Configuration and generated firmware continue to live beside the downloaded launcher EXE rather than inside its runtime cache.

### v0.1.39 development highlights

- ELMA Flasher now presents one PC-focused Device Designer; compile, target selection, erase, USB flash, verification and provisioning are consolidated in its Firmware page.
- Wi-Fi scanning and credential validation use the Windows Wi-Fi adapter, MQTT Connect performs a real broker handshake, and successful settings persist for later firmware builds.
- Device-only power, storage and runtime controls are removed from Designer mode; Hardware instead shows estimated and compiler-confirmed memory use, while Info documents the portable application and project.
- Designer edits autosave beside the portable EXE, generated firmware binaries remain there under standard release names, and the C3 Super Mini view uses the optimized supplied board SVG with its physical pin order.
- The complete native interface remains centered at normal widths and proportionally scales text, controls, illustrations and spacing together when horizontal space becomes tight.

### v0.1.37 development highlights

- DRV8833 inputs are forced inactive immediately after saved settings load, before USB serial waiting, storage, Wi-Fi, MQTT, or web startup can leave them floating.
- Configured motor outputs are stopped and GPIO-held LOW across controlled software restarts, preventing a valve from moving while the ESP reboots.
- Direction changes now include a short all-inputs-off interval and verify that the selected output reached HIGH, returning a clear wiring/short-circuit error when it did not.

### v0.1.36 development highlights

- ELMA Flasher now opens directly into one native application window with `Device Designer` and `Flash USB Device` tabs at the top.
- Device Designer is the default tab. Switching to Flash USB Device saves the complete live designer configuration, and flashing reads that shared configuration before compiling and provisioning the connected target.

### v0.1.35 development highlights

- Device Designer now saves the status LED electrical type independently from its GPIO: regular LED or RGB NeoPixel / WS2812.
- ESP32-C3 Designer builds default the onboard GPIO8 link indicator to NeoPixel mode. Network indication remains steady when connected, blinks in AP mode, and flashes while disconnected.
- ELMA Flasher now waits for the target provisioning acknowledgement and sends configuration in paced chunks, preventing large Wi-Fi, MQTT, GPIO, and peripheral profiles from being truncated over USB serial.

### v0.1.34 development highlights

- Fixed Device Designer firmware compilation failing before the compiler ran because strict repository release validation tried to read flasher, README, and release-note files that are intentionally absent from the portable reduced build workspace.
- Portable builds now validate the bundled firmware version header while normal repository builds and CI retain the complete cross-file release synchronization checks.
- Fixed the frozen compiler runner's PlatformIO helper-process handling and error propagation, and added a release test that compiles an ESP32-C3 image using the finished portable EXE.

### v0.1.33 development highlights

- Device Designer now opens as a native ELMA application view backed by the Qt WebEngine renderer bundled inside the portable executable; it no longer opens the system browser or depends on Edge/WebView2.
- Closing Device Designer returns to the main flasher window, while its loopback-only compiler, configuration, COM-port detection, and flashing backend remain internal to ELMA Flasher.
- Packaged release validation now loads the complete Designer UI through the bundled renderer before the EXE can be uploaded.

### v0.1.32 development highlights

- Fixed Open Device Designer in the portable ELMA Flasher by assigning the loopback designer server to the main application, removing an erroneous recursive server allocation from compile jobs, and closing the local server cleanly when the application exits.
- The packaged UI smoke test now starts the designer server and verifies its local status API so this launch-path regression is caught before release.

### v0.1.31 development highlights

- Successful GitHub OTA and resumable local firmware uploads now open the existing reboot countdown overlay, wait for the device to disconnect and return, clear browser caches, and hard-refresh the page so the new firmware UI and status are loaded.

### v0.1.30 development highlights

- ELMA Flasher now includes a local Device Designer that reuses the full device web interface for future-device configuration, graphical peripheral wiring, capability selection, local compilation, native COM-port flashing, and post-flash serial provisioning.
- Added ESP32-C3 maximum-fit firmware support with the compatible web configurator, Wi-Fi, MQTT/HACS, OTA, GPIO/motor, supported display, sensor, input, control, expansion, storage, and communication features. The network-audio engine is excluded on C3 while ESP32-S3 maximum mode retains the complete multimedia and peripheral feature set.
- The builder detects the connected ESP and flash capacity before erase, validates manual chip overrides, chooses full or selected-feature compile profiles, verifies binary family and OTA-slot size, and preserves hardware-derived device and MQTT identities.

### v0.1.29 development highlights

- Added browser-based USB flashing from the Firmware tab, with clone-current-device and local-file modes, optional full-chip erase, target-family validation, circular progress, and actionable serial/permission errors.
- Clone mode transfers the current firmware plus saved configuration, Wi-Fi, and MQTT credentials while regenerating the target device name, friendly name, MQTT client ID, and base topic from the target's own hardware ID.
- After cloning, the USB provisioning channel reports the target's Wi-Fi address so the browser can offer the newly flashed device directly.
- `ELMA-Flasher-v0.1.29.exe` is the recommended Windows path when the device UI is opened over HTTP. It is a single portable executable with the ELMA interface, COM-port selection, Espressif flashing engine, serial support, and ESP32/ESP32-S3 boot support embedded; Python, PlatformIO, esptool, and browser Web Serial are not required on the target PC.
- The portable flasher supports Clone Current Device and Flash From File, optional full erase, chip-family validation, verified writes, hardware-ID-safe configuration provisioning, and opening the cloned device's reported Wi-Fi IP.
- The localhost browser helper remains available as a development fallback: run `python scripts/usb_flasher_proxy.py <device-ip>` or double-click `scripts/start_usb_flasher_proxy.cmd`.

- Fixed manual GitHub-release installation after refreshing the Firmware list: selecting an installed, older, or alternate compatible asset now proceeds into download/flashing instead of remaining stuck at `Resolving release` with OTA marked busy. Release refresh and install TLS operations are serialized, and Install Selected stays disabled while the list is refreshing.
- Local firmware uploads now use idempotent 16 KiB chunks with server-confirmed offsets, automatic reconnect/resume, and indefinite weak-link retries. While active, Upload Local Firmware becomes Cancel Upload Local Firmware; cancellation aborts the inactive OTA image without rebooting or activating it.
- Reduced genuinely idle ESP32-S3 load from roughly 25-31% aggregate (about 50% on Core 1) to typically 0-2% by rate-limiting unchanged Wi-Fi state publication, servicing runtime state at 50 Hz, bypassing disabled audio/battery paths, and yielding longer only while playback and OTA are inactive.
- Verified the idle optimization on the ceiling-speaker hardware at 80 MHz: internal die temperature fell from 71 C to approximately 42 C while Wi-Fi remained connected.
- Improved setup-network handoff: the AP page reports the new station IP, waits for the client to rejoin the home network, and then opens the device automatically; Wi-Fi edits are applied only by the explicit Connect action.
- Restored reliable asynchronous Wi-Fi scanning during failed station handshakes without erasing saved credentials or repeatedly tearing down the fallback AP.
- Dynamic ESP32 CPU policy: 80 MHz while idle, 160 MHz during stable playback, and 240 MHz while buffering, changing EQ filters, updating firmware, or serving access-point mode.
- Hardware Monitor now reports the live clock rate, aggregate CPU load, and per-core CPU load instead of allowing the old adaptive estimate to drift toward a false 0%.
- The Audio tab now includes a persistent three-band equalizer with mouse-controllable gains plus Flat, Clear, Rock, Bass, Classical, Voice, Jazz, Podcast, and Night presets applied directly to the I2S DSP.
- Equalizer changes use an independent real-time path, avoiding the storage remount and full settings reapply that previously interrupted or jittered active playback.
- Play/Stop now treats the looping ambient effect as background audio, so stopping a radio station reliably changes the control back to Play.
- The External Storage tab now has an inline folder player with synchronized volume, current-track highlighting, selected-track-aware Play/Stop, previous/next, shuffle, repeat, and autoplay controls.
- Folder and dashboard transport controls use aligned 3D buttons with larger Play/Stop icons and double-triangle Previous/Next icons; clicks change tracks or radio stations according to the active source, while holding seeks through local files.
- The dashboard player embeds the track/station name in a progress meter that shows local-track position or radio buffering without redundant Playing/Stopped text.
- Storage playback advances within the open folder and moves each newly started track to the top of the list once; current-track highlighting remains live while manual page, folder, and list scrolling is preserved until the next track starts.
- Boot audio is serialized using real decoder completion events: startup effects cannot be interrupted by remembered media or update cues, and ambient playback resumes after the configured quiet interval.
- The Firmware tab displays persistent OTA health-check and rollback results, including the attempted firmware and the stored failure cause after the bootloader restores the previous image.
- CPU sampling uses cache-safe IRAM FreeRTOS tick hooks, including during NVS and OTA flash operations.
- The redundant embedded ICO favicon was removed while retaining the visually equivalent SVG favicon; all embedded SVG illustrations continue through multipass SVGO and maximum gzip compression.
- Legacy OTA settings for `ESP32-S3-Ceiling-Speaker` migrate to `elma-iot/ELMA-IoT` even when the owner had already been updated separately.
- OTA checks now substitute the target release version into the asset filename and default to the installed build variant, preventing version-mismatched 404s and accidental HACS-to-HACS-Slim selection.

The ESP32-S3 HACS release candidate was validated on the ceiling-speaker hardware over USB and Wi-Fi: saved settings survived, Wi-Fi and MQTT reconnected, playback resumed, the playback clock settled at 160 MHz, EQ changes temporarily raised it to 240 MHz before a delayed downshift, CPU load reported per core, custom EQ gains persisted in NVS, Stop returned to Play when only ambient audio remained, and the Firmware tab consumed the persistent rollback fields without reporting a false failure on a healthy boot.

Latest release highlights:

- OTA installs now detect stalled downloads, retry from the last written offset when the server supports HTTP range requests, and reboot the device after repeated failures instead of leaving it hung mid-update.
- OTA recovery now surfaces retry and restart phases explicitly so the device comes back operational after broken firmware downloads.
- Web asset bundling now uses a cross-platform `npx` launcher so GitHub Actions release verification works on Linux runners as well as Windows development machines.

## What This Firmware Does

- Plays MP3, internet radio, URL streams, and TTS over I2S audio.
- Targets the MAX98357A mono amplifier path for the ceiling-speaker ESP32-S3 build.
- Exposes a local web UI for playback, Wi-Fi, MQTT, battery, OLED, GPIO reference, storage, firmware, and device monitoring.
- Exposes a Configuration workspace with board-aware peripheral planning, pin remapping, and a dynamic wiring diagram for the active build.
- Publishes MQTT state and accepts MQTT playback, transport, OTA, and control commands.
- Supports Home Assistant through standard MQTT topics and the HACS `mqtt_media_player` flow.
- Browses GitHub Releases for matching firmware assets and supports local firmware uploads.
- Supports OLED status display, touch buttons, buzzer, battery monitoring, low-battery sleep, SD storage, configurable pin mapping, and a broader peripheral catalog for audio, inputs, displays, storage, communications, controls, sensors, and expanders.

## Hardware Target

The current documented target is the ESP32-S3 Super Mini ceiling-speaker build using:

- ESP32-S3 Super Mini
- MAX98357A I2S mono amplifier
- Ceiling speaker connected directly to the amplifier output
- 0.96 inch I2C OLED display
- Two TTP223 touch buttons
- Active buzzer

Repository hardware references:

- Circuit diagram: [Docs/circuit.png](Docs/circuit.png)
- ESP32-S3 pinout image: [Docs/esp32-s3_pinout.jpeg](Docs/esp32-s3_pinout.jpeg)
- 3D assets: [3D](3D)
- STL folder: [3D/STL](3D/STL)
- Device story article: https://www.drive2.ru/c/735319567747779562/

![Current ceiling-speaker circuit diagram](Docs/circuit.png)

## Default ESP32-S3 Wiring

The ESP32-S3 environments in [platformio.ini](platformio.ini) default to this ceiling-speaker wiring:

| Function | GPIO |
|---|---:|
| I2S DOUT / DIN | 9 |
| I2S LRCLK / WS | 12 |
| I2S BCLK | 11 |
| Status LED | 10 |
| Battery ADC | 3 |
| OLED SDA | 4 |
| OLED SCL | 5 |
| Touch button 1 | 5 |
| Touch button 2 | 6 |
| Buzzer | 7 |
| SD CS | 4 |
| SD SCK | 5 |
| SD MOSI | 6 |
| SD MISO | 7 |

Notes:

- ESP32-S3 audio pin remapping is intentionally limited to the supported `GPIO9` to `GPIO12` I2S set.
- OLED, battery, LED, buzzer, and SD pins are sanitized to avoid active audio and required-function conflicts.
- The documented amplifier path for the ceiling-speaker profiles is MAX98357A only.

## Audio Path

Audio playback uses `schreibfaul1/ESP32-audioI2S` on the standard I2S path required by MAX98357A.

Important current behavior:

- The firmware uses standard I2S format, not a PCM5102-specific path.
- The earlier software-side audio boost that could clip or distort playback was removed.
- Active stream replacement uses fade-out and fade-in for smoother station switching.
- The Audio tab supports direct Radio Browser station switching while already playing.
- The hero playback card includes previous station, play or stop, and next station controls.
- The browser default radio selection is Azerbaijan with AvtoFM preselected.
- SD mounting now prefers 40 MHz first and falls back through 20, 10, 4, 1, and 0.4 MHz for cards or wiring that need a slower clock.

There is also a dedicated diagnostic build for isolated MAX98357A testing:

- `esp32s3_notifier_hacs_audio_test`

That profile is for troubleshooting and is not part of the standard release asset matrix.

## Sound Effects And Automation

Runtime automation and local sound-effect routing are coordinated in [src/main.cpp](src/main.cpp), [src/audio_player.cpp](src/audio_player.cpp), and [src/sound_effects.cpp](src/sound_effects.cpp).

Current behavior:

- Configurable effect-file routing is available for startup, alarm, notification, ambient, low-battery, shutdown, update-available, and update-success events.
- Ambient selection now starts the dedicated ambient playback source, resumes automatically after other playback stops, and keeps non-ambient effect dropdowns as one-shot previews.
- Ambient and alert cues can be selected from local storage and triggered without replacing the normal release asset flow.
- Remembered last-played stream and media selections now keep both the last source and whether it was stopped or active, so a reboot restores the correct stopped-versus-playing behavior instead of always resuming.
- Effect-file previews now use the full stop-preview-resume path, which restores interrupted playback more reliably after one-shot preview sounds finish or fail.
- Startup effects are now deferred briefly after boot and retried across transient SD mount instability so boot cues can start after the device finishes its early storage bring-up.
- Active SD-backed playback now keeps storage-summary reads on cached values so background status polling does not probe the card mid-stream.
- Live SD-to-SD playback switches now add a short settle delay and remount-retry path so changing SD-backed effects no longer drops into a false missing-file or SD removal state.
- SD folder navigation now keeps the requested path in sync with the rendered list during playback-safe cached views.
- SD folder paging now keeps loading indexed batches during playback instead of stopping at the first 20 entries.
- Low-battery handling can play a cue before entering deep sleep when that mode is enabled.
- OTA availability and success cues can be paired with the firmware action flow.

## Web UI

The web frontend lives in [web/index.html](web/index.html), [web/style.css](web/style.css), and [web/app.js](web/app.js), then gets embedded into firmware by [scripts/asset_embed.py](scripts/asset_embed.py).

Current UI highlights:

- Hero header with live firmware version, release channel badge, and author link.
- Header gear menu with inline refresh, reboot, and shutdown actions.
- Centered reboot countdown overlay that waits for reconnect before forcing a refresh.
- Live Wi-Fi, MQTT, playback, battery, heap, and firmware status.
- Hardware Monitor cards for aggregate and per-core CPU load, live CPU clock speed, chip temperature, SRAM, PSRAM, flash FS, SD, and board metadata.
- MQTT discovery now also exposes chip CPU temperature to Home Assistant as a retained temperature sensor.
- Radio Browser country and station selection with recent playback history.
- Direct URL playback and TTS playback entry.
- Previous, play or stop, and next station transport controls from the top playback card.
- Audio Effects tab for assigning local files to startup, alert, ambient, and OTA cues.
- Persistent three-band I2S equalizer below the main volume control, with visual 500 Hz, 3 kHz, and 6 kHz gain indicators and nine selectable presets.
- Audio Effects ambient playback now uses the real looping ambient source and automatically returns after manual playback or streams stop.
- Configuration tab with board selection, peripheral-profile selectors, conflict-aware GPIO mapping, and a live peripheral diagram.
- Peripheral diagram editing with draggable modules, label editing, node rotation, saved layout, and label-based rewiring.
- I2S pin remapping for MAX98357A wiring.
- OLED pin remapping plus display-mode selection between OLED and Wape trigger mode.
- Battery configuration, charging-sense pin selection, calibration helpers, and low-battery sleep controls.
- MQTT tab connect or disconnect control plus a dedicated Republish Discovery button for Home Assistant rediscovery.
- Motor tab runtime config now stores learned open or closed position, movement roles, and touch-button motor actions on the device instead of relying on browser state.
- GPIO Info tab with board selector, dedicated SVG board art, side-by-side pin guidance, and board suitability recommendations.
- Internal flash and SD storage tabs with file browsing, folder creation, upload support, selection tools, and SD pin configuration.
- SD storage playback now starts immediately from the preview modal and toolbar play action without blocking on artwork scans.
- SD reindex actions now stop playback first and then continue automatically instead of only showing a blocker message.
- Firmware release browsing, local firmware upload, and firmware action dashboard.
- Password reveal toggles, reboot, and factory-reset actions, with backup and restore available from the Device tab.
- Embedded favicon served from the device web UI.

## Configuration Tab And Peripheral Capabilities

The Configuration area is intended to let the device be adapted beyond the default ceiling-speaker wiring. The current frontend supports board-aware planning, persistent peripheral profile selection, helper bindings for unsupported combinations, and a dynamic wiring diagram that reflects the active configuration.

Current configuration capabilities:

- Board selector with autodetect-aware ESP board guidance and SVG board imagery.
- Conflict-aware pin sanitization for audio, OLED, SD, battery ADC, charging sense, status LED, and Wape trigger paths.
- Persistent peripheral-profile selection stored in browser UI state and reused across sessions.
- Dynamic peripheral diagram with drag positioning, label editing, rotation, saved layouts, and automatic wire routing.
- Diagram rewiring that can map current peripheral labels onto board labels and apply matching GPIO assignments.
- Storage-aware local file routing for sound effects and external-storage browsing, uploads, folder creation, deletion, reindex, and preview playback.

Current peripheral slot counts:

- Audio outputs: up to 3
- Audio inputs: up to 3
- Displays: up to 2
- Sensors: up to 10
- Inputs: up to 10
- Storage devices: up to 3
- Communication modules: up to 4
- Control devices: up to 16
- Expansion devices: up to 4

Supported peripheral profiles currently exposed by the Configuration tab:

Audio output profiles:

- None
- MAX98357A I2S Amp
- PCM5102 I2S DAC
- UDA1334A I2S DAC
- ES9023 I2S DAC
- PT8211 I2S DAC
- CS4344 I2S DAC
- Internal DAC GPIO25/26
- PWM / Class-D Amp
- Analog Line-Out via DAC
- PAM8403 Analog Amp
- TPA3110 / TPA3116 Analog Amp
- Buzzer
- WM8960 Audio Codec
- ES8388 Audio Codec
- Bluetooth Audio Source
- Custom

Audio input profiles:

- None
- I2S Microphone Generic
- INMP441 I2S Mic
- SPH0645 / ICS-43434 I2S Mic
- MSM2615 I2S Mic
- PDM Microphone
- Analog Electret Mic ADC
- MAX9814 Mic ADC
- MAX4466 Mic ADC
- Line-In ADC
- External I2S ADC
- ES7243 / ES7210 I2S ADC
- WM8960 Audio Codec
- ES8388 Audio Codec
- Bluetooth Audio Sink
- Custom

Display profiles:

- None
- I2C OLED
- SPI TFT
- Waveshare Screen
- Custom

Sensor profiles:

- None
- BNO055
- BNO085 / BNO080
- MPU6050
- DS18B20
- Battery Voltage Divider (2x 220kOhms)
- Custom

Input profiles:

- None
- TTP223 Touch Button
- Physical Button
- Toggle Switch
- Rotary Encoder
- IR Receiver
- PIR Motion Sensor
- Reed Switch
- Limit Switch
- Joystick Analog
- Analog Potentiometer
- ESP32 Native Touch Pad
- Water Leak / Rain Sensor
- Vibration / Shock Sensor
- Hall Sensor
- Flow Meter Pulse Sensor
- Keypad Matrix
- RF 433MHz Receiver
- Wake Button
- Custom

Storage profiles:

- None
- MicroSD SPI
- MicroSD SDMMC
- Custom

Communication profiles:

- None
- UART
- RS485
- LoRa E22/E220
- I2C
- SPI
- Custom

Control profiles:

- None
- Servo
- Dual Servo
- PWM Fan
- DC Motor Driver Generic
- DRV8833 Dual Motor Driver
- TB6612FNG Dual Motor Driver
- L298N Dual Motor Driver
- BTS7960 High Power Motor Driver
- Stepper Driver A4988 / DRV8825
- Stepper Driver TMC2208 / TMC2209
- Relay Module
- MOSFET Switch
- Solenoid / Valve Driver
- LED / PWM Dimmer
- WS2812 / NeoPixel LED Strip
- Buzzer
- Vibration Motor
- Pump Driver
- Custom

Expansion profiles:

- None
- I2C GPIO Expander
- MCP23017 16-bit I/O Expander
- PCF8574 8-bit I/O Expander
- PCF8575 16-bit I/O Expander
- Shift Register 74HC595 Output Expander
- Shift Register 74HC165 Input Expander
- Analog Multiplexer CD4051 / 74HC4051 8-channel
- Analog Multiplexer CD74HC4067 16-channel
- External ADC ADS1115 16-bit I2C
- External ADC ADS1015 12-bit I2C
- External ADC MCP3008 SPI
- External DAC MCP4725 I2C
- PWM Expander PCA9685
- Custom

## Build Profiles

Release-oriented PlatformIO environments:

- `esp32_notifier`
- `esp32_notifier_hacs`
- `esp32_notifier_hacs_slim`
- `esp32s3_notifier`
- `esp32s3_notifier_hacs`
- `esp32s3_notifier_hacs_slim`

Additional diagnostic environment:

- `esp32s3_notifier_hacs_audio_test`

Default workspace target in [platformio.ini](platformio.ini):

- `esp32s3_notifier_hacs`

## 4MB Flash Layout

The current full-web release profiles are configured around 4MB hardware.

Current release behavior:

- ESP32 and ESP32-S3 release builds now use [partitions/ota_4m.csv](partitions/ota_4m.csv) with two OTA app slots so failed updates and bootloops can roll back to the previous firmware.
- [platformio.ini](platformio.ini) also sets `board_upload.flash_size = 4MB` for the ESP32-S3 environments to avoid generating an invalid 8MB image header.
- To make both OTA slots large enough for the current full-web images on 4MB hardware, the internal flash filesystem partition is removed from this layout.
- This preserves true OTA redundancy on 4MB boards while still avoiding the ESP32-S3 boot failure caused by flashing an 8MB-image header onto 4MB hardware.

If your board reports only `4096k` flash, use the current configuration as-is. OTA redundancy is available again, but internal flash storage is not.

## Build And Flash

Build the current default environment:

```powershell
pio run
```

Build a specific environment:

```powershell
pio run -e esp32s3_notifier_hacs
```

Upload:

```powershell
pio run -t upload
```

List serial devices:

```powershell
pio device list
```

Open the serial monitor:

```powershell
pio device monitor -b 115200
```

If flashing an ESP32-S3 fails to connect cleanly, hold `BOOT`, start upload, and release `BOOT` after `Connecting...` appears.

## VS Code Tasks

This workspace already includes PlatformIO-oriented tasks. The most useful ones are:

- `PlatformIO: Verify`
- `PlatformIO: Upload (Auto Port)`
- `PlatformIO: Monitor (Auto Port)`
- `PlatformIO: List Serial Devices`

There are also optional fixed-port monitor and upload tasks for boards that stay on a stable COM port.

## First Boot And Provisioning

On startup the firmware:

1. Loads saved settings from Preferences when available.
2. Falls back to compile-time defaults from [include/default_config.h](include/default_config.h) otherwise.
3. Attempts Wi-Fi station mode when credentials are configured.
4. Starts fallback AP mode if station credentials are missing or connection fails.

Fallback AP defaults:

- SSID prefix: `ELMA-IoT-XXXXXX`
- Password: `12345678`
- Config page: `http://192.168.4.1`

Default generated device identity:

- ESP32 builds: `elma-iot-xxxxxx`
- ESP32-S3 builds: `elma-iot-xxxxxx`

## MQTT And Home Assistant

Default base topic:

- `elma_iot`

Typical command topics:

- `esp32_notifier/cmd/play`
- `esp32_notifier/cmd/tts`
- `esp32_notifier/cmd/stop`
- `esp32_notifier/cmd/volume`
- `esp32_notifier/cmd/ota/select_version`
- `esp32_notifier/cmd/ota/install_version`

Typical state topics:

- `esp32_notifier/availability`
- `esp32_notifier/state/playback`
- `esp32_notifier/state/network`
- `esp32_notifier/state/battery`
- `esp32_notifier/state/battery_percent`
- `esp32_notifier/state/battery_charging`
- `esp32_notifier/state/volume`

Example play payload:

```json
{"url":"https://example.com/stream.mp3","label":"Test Stream","type":"stream"}
```

Example volume payload:

```json
{"volumePercent":55}
```

For Home Assistant media-player style control, use the HACS-oriented build with `bkbilly/mqtt_media_player`:

- Recommended ESP32-S3 build: `esp32s3_notifier_hacs`
- Vendored backup integration: [home_assistant/custom_components/mqtt_media_player](home_assistant/custom_components/mqtt_media_player)

When running multiple notifier devices on the same broker, keep these values unique per device:

- MQTT Client ID
- MQTT Base Topic
- Device Name
- Friendly Name

Home Assistant rename behavior:

- Changing only MQTT Client ID or Base Topic does not rename the Home Assistant device entry.
- MQTT discovery identity is derived from Device Name and Friendly Name.
- If an older device name is still shown in Home Assistant after a rename, remove the old device entry or clear the retained discovery topics for the old device name.

Current MQTT behavior:

- The MQTT tab can republish Home Assistant discovery without disconnecting the broker session.
- Generic broker reachability failures now keep retrying instead of forcing a device recovery reboot.
- Credential or client-ID rejections still surface as an explicit frontend error.
- Automatic broker reconnect now uses the same configure-and-connect path as the manual Connect button.
- Motor valve discovery now uses an optimistic switch entity so Home Assistant does not bounce the control back before state catches up.
- Motor state publishes now fire immediately for both MQTT-triggered and web-triggered valve movement.
- CPU temperature is published as a dedicated retained MQTT state topic and discovered in Home Assistant as a standard temperature sensor.

Current OTA and rollback behavior:

- Legacy OTA repository values are migrated to `elma-iot/ELMA-IoT` before release checks, including mixed saved owner/repository states.
- OTA rollback bookkeeping now suppresses harmless `Preferences` `NOT_FOUND` log spam on normal boots when no pending or bad-version keys exist in NVS.
- OTA release downloads now treat a no-progress socket as a stalled transfer after 15 seconds, retry up to three resumed HTTP range requests, and force a recovery reboot if the download still cannot progress.
- If the firmware host does not support OTA resume after a stall, the device aborts the update and reboots back into the current firmware instead of staying stuck in a busy flashing state.

## Firmware And Releases

The Firmware tab checks GitHub Releases by default and matches the expected asset name to the running build variant.


Release asset names for `v0.1.54`:

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
- `SHA256SUMS.txt`

GitHub release publishing is automated by [.github/workflows/platformio.yml](.github/workflows/platformio.yml): publishing a release triggers CI to build eight operating firmware variants (including the legacy-slot build), three minimal recovery variants and the Windows flasher. Existing manually verified assets are not overwritten. Full release images use ESP32-WROOM, ESP32-S3 Super Mini and ESP32-C3 board defaults respectively; use the EXE to build for another supported board. Binaries and the EXE are release attachments, not Git source files.

## Battery Monitoring

Battery monitoring is implemented in [src/battery_monitor.cpp](src/battery_monitor.cpp).

Current ESP32-S3 default behavior:

- The ESP32-S3 Super Mini profiles use `GPIO3` for voltage sensing.
- The default ESP32-S3 calibration multiplier is `2.0`.
- On some ESP32-S3 Super Mini boards this reflects a built-in VBUS divider rather than a true direct cell-voltage measurement.
- Battery MQTT discovery now exposes battery voltage, battery percentage, and charging state to Home Assistant.
- An optional charging-sense pin can be assigned and is now used directly for charging detection when configured.
- If no charging-sense pin is configured, charging falls back to filtered voltage-trend detection.
- Device settings now include low-battery sleep enablement, threshold percentage, and wake interval controls.

If the reported voltage is off, recalibrate it from the Battery tab using a multimeter measurement.

Important: if the battery voltage is indicated incorrectly and low-battery sleep is enabled, the device can enter deep sleep and stop being reachable from the web interface. While the device is connected to USB, open the Device tab and disable deep sleep until the measured voltage is corrected to match the real level and stay above the configured deep-sleep threshold.

## OLED Support

OLED support is handled by [src/display_manager.cpp](src/display_manager.cpp).

Current behavior:

- SSD1306 and SH1106 displays are supported.
- Display mode can switch between a normal OLED renderer and a Wape trigger mode.
- OLED pins can be remapped from the UI.
- Wape mode can fire a trigger pin on device start, playback start, or charging start.
- OLED pin choices are sanitized to avoid clashes with audio, battery, LED, SD, and other reserved functions.
- Only one display mode is intended to be active at a time.

## Storage Support

Storage management covers both internal flash and optional SD media.

Current behavior:

- Internal flash storage is available only when the selected partition table includes a flash filesystem partition.
- SD storage can be enabled and pinned through the UI.
- The web UI supports browsing, folder creation, upload actions, bulk selection controls, and in-browser preview helpers for available storage targets.
- SD pin choices are checked against active audio, battery, and status pin assignments.
- Storage summary polling now uses cached SD values during active SD reads so ambient and other SD-backed playback are not interrupted by status refreshes.

## Repository Layout

Key files and directories:

- [platformio.ini](platformio.ini)
- [partitions/ota_4m.csv](partitions/ota_4m.csv)
- [partitions/single_4m.csv](partitions/single_4m.csv)
- [include/default_config.h](include/default_config.h)
- [include/settings_schema.h](include/settings_schema.h)
- [include/version.h](include/version.h)
- [src/main.cpp](src/main.cpp)
- [src/audio_player.cpp](src/audio_player.cpp)
- [src/settings_manager.cpp](src/settings_manager.cpp)
- [src/ota_manager.cpp](src/ota_manager.cpp)
- [src/web_server.cpp](src/web_server.cpp)
- [web/index.html](web/index.html)
- [web/style.css](web/style.css)
- [web/app.js](web/app.js)
- [home_assistant](home_assistant)
- [3D](3D)
- [Docs](Docs)

## Known Limitations

- Native Home Assistant core MQTT discovery alone is still not enough for a first-class `media_player` entity on the standard build.
- Some streams and codecs may still require library-side tuning depending on the source.
- Firmware size is still tight, especially on ESP32-S3 4MB hardware.
- Basic web auth is supported, but it remains simple HTTP auth rather than a full access-control model.
- The project is output-only audio. No microphone or duplex voice path is implemented.

## Release Notes

Current release notes live here:

- [release-assets/v0.1.54/release-notes.md](release-assets/v0.1.54/release-notes.md)
- [release-assets/v0.1.53/release-notes.md](release-assets/v0.1.53/release-notes.md)
- [release-assets/v0.1.52/release-notes.md](release-assets/v0.1.52/release-notes.md)
- [release-assets/v0.1.42/release-notes.md](release-assets/v0.1.42/release-notes.md)
- [release-assets/v0.1.41/release-notes.md](release-assets/v0.1.41/release-notes.md)
- [release-assets/v0.1.39/release-notes.md](release-assets/v0.1.39/release-notes.md)
- [release-assets/v0.1.38/release-notes.md](release-assets/v0.1.38/release-notes.md)
- [release-assets/v0.1.37/release-notes.md](release-assets/v0.1.37/release-notes.md)
- [release-assets/v0.1.36/release-notes.md](release-assets/v0.1.36/release-notes.md)
- [release-assets/v0.1.35/release-notes.md](release-assets/v0.1.35/release-notes.md)
- [release-assets/v0.1.34/release-notes.md](release-assets/v0.1.34/release-notes.md)
- [release-assets/v0.1.33/release-notes.md](release-assets/v0.1.33/release-notes.md)
- [release-assets/v0.1.32/release-notes.md](release-assets/v0.1.32/release-notes.md)
- [release-assets/v0.1.31/release-notes.md](release-assets/v0.1.31/release-notes.md)
- [release-assets/v0.1.30/release-notes.md](release-assets/v0.1.30/release-notes.md)
- [release-assets/v0.1.29/release-notes.md](release-assets/v0.1.29/release-notes.md)
- File Manager autoplay now advances from an explicit firmware completion event, so next, shuffle, and repeat remain reliable even when status polling misses the brief idle transition. Queue advancement is restricted to File Manager playback and never applies to radio, ambient audio, effects, notifications, or direct URLs.
- File Manager playback now reports decoder position and duration and provides synchronized inline and preview seek controls that reset correctly between tracks.
- GitHub release checks now run on a background FreeRTOS task so TLS and JSON work cannot starve the audio loop; the decoder uses a 1.25 MiB PSRAM queue with only a small SRAM fallback.
- Startup audio now owns the decoder until natural completion; low-battery cues, update notifications, ambient resume, and saved playback wait behind it.
- Exclusive notification/update/low-battery effects now fade out the active source, preserve local-file position, play once, and resume with a fade-in; WAV cues still use the lighter overlay/ducking path. Alarm, shutdown, and reboot effects intentionally do not resume playback.
- The File Manager remembers its last storage target and directory across page/device restarts, falling back toward the root if the saved folder no longer exists.
- FLAC duration is cached once after decoder startup instead of being recalculated during every status sample, removing decoder interference/clicks; live File Manager status reconstructs and focuses the playing row so progress survives reloads.
- Dashboard and File Manager transport controls now share the 3D meter styling, larger centered icons, source-aware Previous/Next behavior, and hold-to-seek for local tracks.
- The dashboard title now lives inside its progress meter, while the playing file remains highlighted and is moved to the top only once per track change so status polling cannot fight manual scrolling.
