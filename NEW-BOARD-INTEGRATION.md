# Display and camera integration — work in progress

## ESP32-S3-SPK N16R8 — 2026-10-05

The existing selector ID `esp32-spk-n16r8` is retained for saved-project compatibility. Its supplied SVG stays 30 × 50 mm, with explicit contacts at the 38 header pad centres. NC and EN are not GPIO choices. No unexposed camera or memory pin is invented as a header contact.

Pin provenance: `MINIEXCO/V6.0/MiniExco_v_2_00_12/MiniExco_v_2_00_12.ino` and `MINIEXCO/V6.0/ESP32S3-SPK_V1.0_Schematics.png`. The schematic shows external quad PSRAM, not the octal memory wiring of an ESP32-S3-WROOM N16R8 module. The build uses QIO flash and QSPI PSRAM, 16 MB flash with dual OTA application slots. PSRAM size is detected at runtime.

| Function | Confirmed GPIOs |
| --- | --- |
| Camera D0–D7 | 7, 5, 4, 6, 8, 42, 48, 47 |
| Camera XCLK/PCLK/VSYNC/HREF | 33 / 41 / 35 / 34; XCLK 10 MHz as in MINIEXCO |
| Camera SCCB SDA/SCL | 37 / 36; reset and power-down unconnected |
| NS4168 speaker DATA/BCLK/LRCLK/CTRL | 9 / 10 / 45 / 46 |
| Dual MEMS microphones DATA/BCLK/WS | 38 / 39 / 40, stereo slots |
| SPI microSD CS/CLK/MOSI/MISO | 2 / 11 / 3 / 12 |
| SK6812 status LED | 21 |
| CH340 UART TX/RX | 43 / 44 |
| Native USB D−/D+ | 19 / 20; CH340 serial is the default build console |

Speaker playback uses ELMA's existing AudioPlayer and Logics actions. CTRL goes low when audio is disabled and high after initialization. Microphone capture uses a separate I2S controller and bounded, nonblocking reads; both channels feed the existing audio-reactive analysis. No voice-recognition feature is implied.

GPIO46 is input/output on **S3**, confirmed by the Espressif datasheet and SDK output mask; the earlier shared S2/S3 input-only restriction was wrong. Its strapping warning remains. The schematic microphone part marking differs from the product description, but both describe a stereo I2S pair; hardware testing is still required.

Size-fit retries retain audio and SD libraries on SPK and all three LCD boards even when presently disabled. The LCD boards use the ESP32 internal DAC on GPIO26 plus an onboard amplifier. Unused SVGs are reduced first; a remaining oversized image fails safely rather than removing these built-in capabilities.

Updated 2026-10-05. These source additions have firmware build checks and have not been flashed or physically tested. They are not part of the previous release or packaged Windows EXE.

| Selector | Display | Touch | Artwork |
| --- | --- | --- | --- |
| ESP32-2432S028R | ILI9341, 240 × 320 | XPT2046, resistive | User-supplied Fritzing breadboard SVG |
| ESP32-2432S028C | ILI9341, 240 × 320 | CST820, capacitive | Identical to the R variant, as requested |
| ESP32-3248S035C / ESP32-48S035 | ST7796, 320 × 480 | GT911, capacitive | User-supplied rear SVG |
| ESP32-CAM | OV2640 camera | None | User-supplied Fritzing breadboard SVG |

The two 2.8-inch variants intentionally share external connector geometry, pin labels, LCD wiring, speaker output and SD wiring. Their touch controllers and internal touch GPIO reservations differ. The supplied Fritzing schematic supplies connector names; breadboard pad IDs supply label anchors. Separate selector entries choose the correct firmware touch driver.

The Sunton profiles use LCD SCK14/MOSI13/MISO12/CS15/DC2; backlight21 for 2.8-inch and27 for 3.5-inch. SD uses a separate SPI bus, CS5/SCK18/MOSI23/MISO19. The speaker amplifier is fed by ESP32 internal DAC2 on GPIO26, rather than an external I2S DAC.

ESP32-CAM currently targets the conventional AI-Thinker OV2640 pin map. The Fritzing file identifies connector geometry, not the installed sensor. Camera and PSRAM pins must remain reserved. SD uses native one-bit mode on CLK14/CMD15/D0=2, leaving the flash LED GPIO4 independent. The camera page retrieves bounded-rate JPEG frames only while visible, with an overlay settings button.

The new 4 MB profiles use a single factory application partition. Conventional dual-slot OTA is not available with this layout; USB flashing is required. The existing OTA manager rejects updates when no separate writable target partition is available.

## Validation record

- Shared R/C SVG bytes and connector coordinates match.
- Desktop R/C touch reservations differ as required.
- New display profiles preserve speaker, SD and screen settings through configuration snapshots.
- Existing VIEWE and new Sunton model tests: 16 passed.
- Broader board/GPIO/model checks: 50 passed with 4,035 subtests; one pre-existing ESP32-C3 contact-orientation assertion fails against the unchanged committed coordinates.
- All full web variants pass the single-target-board selector audit.
- All four firmware targets built successfully. Final display builds additionally validate the explicit LVGL configuration include path and RGB565 transfer format. Static RAM usage is about 80 KB for displays and 79 KB for ESP32-CAM; this does not measure runtime heap requirements.
- Camera controls, physical display/touch calibration, speaker playback and SD access still require device validation. A portable Windows package containing these additions has not been built or published.

## Reference implementations

- [CYD pin map](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display/blob/main/PINS.md)
- [Sunton ESP32-2432S028R definition](https://github.com/rzeldent/platformio-espressif32-sunton/blob/main/esp32-2432S028R.json)
- [Sunton ESP32-3248S035C definition](https://github.com/rzeldent/platformio-espressif32-sunton/blob/main/esp32-3248S035C.json)
- [ESP32-3248S035 examples](https://github.com/ardnew/ESP32-3248S035)
- [LovyanGFX](https://github.com/lovyan03/LovyanGFX)
- [Espressif camera driver](https://github.com/espressif/esp32-camera)

## Android follow-up

### LCD media controls — 2026-10-05 (unreleased)

- Audio has Country and Station selectors, station-name search, 20-station pages, and a compact player. Countries load on first opening Audio; changing country requests its stations. The directory uses the same [Radio Browser API](https://docs.radio-browser.info/) as the web UI, on a low-priority network worker with timeouts.
- The top status bar has a speaker button. It opens a volume slider; tapping outside closes it. Volume follows the device's shared playback state and uses the existing volume command, including when changed through the web UI.
- External Storage has root/parent navigation, 12-entry pages, folder buttons, playable file buttons, playback/stop, previous/next among displayed tracks, volume and a seek slider when duration is known. Directory results are cached until navigation, refresh, or mount-state changes. No card is required for the tab to remain visible.
- LCD playback uses the same play/stop/seek/volume handlers as web playback. SD playback uses the `file-manager` source and `sd:/...` references so the web file manager can follow it. Radio selection follows live URLs where the station is present in the displayed list; browsing different lists does not interrupt playback.
- Physical touch behavior, SD hot-plug, network resilience and uninterrupted audio under load still require hardware testing. These source changes are not yet flashed or packaged in the Windows EXE.
- Validation: the shared LCD player code compiled and linked for `sunton_2432s028r`; seven browser/LCD navigation and synchronization tests pass. Radio Browser country and station endpoints responded successfully from the development computer. Other panel targets and real-device playback were not re-tested for this change.

Port the four board profiles, shared R/C connector assets, controller-specific touch reservations, onboard speaker/SD selection rules and camera preview/settings modal. Reuse the board capability metadata rather than treating every classic ESP32 as interchangeable. Do not mark hardware support verified until the corresponding device tests pass.

## Built-in hardware audit — 2026-10-05

| Board | Built-in facilities and defaults |
| --- | --- |
| ESP32-2432S028R | LCD + XPT2046 touch, internal DAC/amplifier, SPI MicroSD slot, RGB status LED, GPIO34 light sensor |
| ESP32-2432S028C | LCD + CST820 touch, internal DAC/amplifier, SPI MicroSD slot, RGB status LED, GPIO34 light sensor |
| ESP32-48S035 / ESP32-3248S035C | LCD + GT911 touch, internal DAC/amplifier, SPI MicroSD slot, RGB status LED, GPIO34 light sensor |
| ESP32-CAM | OV2640 camera, one-bit SDMMC slot enabled by default, GPIO33 status LED; GPIO4 flash lamp remains independent of one-bit SD |
| ESP32-S3-SPK N16R8 | Camera, NS4168 I2S speaker, dual I2S microphones, SPI MicroSD slot, GPIO21 addressable status LED |

Fixed built-in profiles retain their GPIO reservations but do not produce external module SVGs or editable wiring fields. Extra external modules keep their own configuration. Board changes remove the old board's built-in profiles rather than treating them as external peripherals. Existing external sensor identities are preserved when adding the onboard light sensor. The light sensor exposes raw ADC readings through its Logics sensor-value node, not calibrated lux.

Storage slot availability is independent of card insertion; a missing card is a mount-state message, not grounds to remove the storage tab. Camera and microphones remain capability-controlled tabs. The LCD profiles do not claim microphones or cameras. BOOT/reset, USB/UART, regulators and memory are board infrastructure rather than configurable external peripherals. ESP32-CAM flash illumination is not yet exposed in the camera settings UI.

Reference verification: the CYD PINS.md and the Sunton 3.5-inch board definition linked above confirm the onboard amplifier/DAC, SD, RGB LED and GPIO34 light sensor. SPK remains based on the supplied MINIEXCO project and schematic. These are source-level and build checks, not new physical hardware validation.
