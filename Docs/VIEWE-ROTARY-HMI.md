# VIEWE 2.1-inch rotary HMI

Select **VIEWE UEDX48480021-MD80ET** with GC9503, or the separate ST7701S experimental option. Both use 16 MiB flash (DIO boot header) and 8 MiB octal PSRAM, 480 × 480 RGB565, and the existing ELMA display, provisioning, Logics, and OTA paths. GPIO48 belongs to LCD data on this board. The DevKitC N16R8's GPIO48 NeoPixel setting does not apply.

## Controller provenance

The [manufacturer repository](https://github.com/VIEWESMART/UEDX48480021-MD80ESP32-2.1inch-Touch-Knob-Display) README says ST7701S; its supplied board header selects GC9503 but supplies ST7701-style commands. The GC9503 rotary profile instead uses the 33-command LCD sequence recovered from the user's physically verified working pre-flash firmware (Arduino_GFX table at flash offset `0x1df92`; backup SHA256 `d670dc2743925d3ebde18eada5faaa6ccbaae145fe2f02b1e8addc2236fa34ae`). The ST7701S profile uses the library's default ST7701 sequence and requires physical panel revision testing. No GC9A01 SPI substitution is used.

The MD80ET CST826-compatible touch path skips the optional startup ID-register read, as the manufacturer's IDF example does: sleeping touch controllers may NACK it. A touch initialization failure falls back to LCD/encoder operation rather than preventing backlight startup. Rotary profiles use direct RGB DMA: the prebuilt SDK's bounce-buffer ISR otherwise reads inaccessible PSRAM during flash/NVS writes and crashes with cache disabled. The scoped `rotary_linker.py` copies the SDK linker script into the build directory and places GDMA HAL helpers used by the RGB restart interrupt in internal executable RAM; otherwise the SDK calls flash-resident helpers with cache disabled. The shared SDK package is not modified. Other board profiles retain their existing driver behavior.

## Electrical ownership

The rotary LVGL display explicitly initializes the shared panel theme and gives its screen an opaque background. LVGL's default theme is disabled in this firmware; creating the menu without initializing a theme leaves its objects transparent over the white display background. The startup logo and rotary LVGL pixels use native little-endian RGB565 for the RGB framebuffer.

| Function | GPIOs |
| --- | --- |
| RGB DE / VSYNC / HSYNC / PCLK | 17 / 3 / 46 / 9 |
| RGB data, low to high | 10, 11, 12, 13, 14, 21, 47, 48, 45, 38, 39, 40, 41, 42, 2, 1 |
| LCD command CS / SCK / SDA | 18 / 13 / 12 (shared with RGB; vendor driver releases control bus) |
| LCD reset / backlight PWM | 8 / 7 |
| CST826 touch SCL / SDA | 15 / 16, I2C address 0x15; vendor CST820-compatible protocol |
| Encoder A / B / switch | 6 / 5 / 0 |
| Native USB D− / D+ | 19 / 20 |
| Module UART TX / RX | 43 / 44 |
| Octal memory | 33–37 reserved |

Timing comes from the MD80ET vendor profile: 16 MHz pixel clock, horizontal pulse/back/front 8/20/40, vertical 8/20/50. The RGB framebuffer is allocated in PSRAM; LVGL uses a bounded ten-line internal DMA draw buffer. The physical screen clips square framebuffer corners.

GPIO0 is the BOOT strap. Holding the encoder switch during reset can enter download mode. The runtime never drives it. Decoder transitions reject impossible jumps; inverse bounce transitions cancel. Steps per detent may be 1, 2, or 4; sensitivity is 1–8; direction is configurable. Button debounce is 25 ms, long press 800 ms, and double press window 300 ms. Short press therefore fires after that window. Long press goes back; double press emits its separate event. Touch uses the same selection/edit state, with taps, >60-pixel swipes, and an 800-ms stationary long press.

## Adapter artwork

The supplied USB-TEST-MD50-V3.3 (202405) SVG is embedded as vector artwork. Its IO1–IO9 labels are **adapter labels, not verified ESP32 GPIOs**. `usb-test-md50-adapter.json` preserves SVG contact IDs and positions without inventing mappings.

The manufacturer MD80ET V1.0 schematic shows module J1 as 10 contacts at 0.5-mm pitch: USB−, USB+, EN, UART TX, UART RX, GND, ADC GPIO4, 5V, GND, GND. J2 has 12 contacts. The supplied illustration depicts 20- and 16-contact FPCs. Physical adapter compatibility, orientation, rail connections and contact mapping remain unverified. Consequently the profile exposes no external GPIO wiring. Its LCD, encoder, touch, USB and memory pins remain reserved even though the adapter illustration has labelled holes. No onboard SD, external audio or built-in LED is assumed.

## Project/editor workflow

The native **Circular Menu** workspace previews a 480 × 480 ring. Load the demo, edit its JSON, Preview, then Apply to project. Configuration is stored in existing `oled.circularMenu` settings and transported by existing provisioning. Root menu is parent 0. IDs are unique positive integers; each ring has at most eight items and the project at most 32. Submenus must be reachable and acyclic, with depth at most eight. Titles are at most 48 UTF-8 bytes; icons 32, units 16. Firmware font coverage remains the bundled LVGL font coverage.

Supported item kinds: action, menu, value, gauge, text, confirm. A value item toggles edit mode on press; rotation then clamps to its finite bounds and step. Confirmation requires a second press, and Back cancels it. A gauge/progress ring reflects the selected item's normalized value. Touching another item exits value editing.

Logics → **Circular Menu** provides rotary/touch events, selected/value-change events (optional item-ID filtering), create/add/submenu, navigation/selection, text/icon/value/gauge/progress, brightness and confirmation actions. Connect existing sensor/value nodes to display actions and value-change output to existing network or other supported actions. Event nodes establish an initial baseline instead of firing on boot. Runtime-created menu changes are transient; Apply in the native editor stores the initial configuration.

The editor preview uses sample values. The example project in `examples/rotary-hmi` binds Temperature to the chip temperature, Wi-Fi to connection events, and Brightness to the panel backlight. LED Control publishes its workflow value to an explicitly configured MQTT broker; it does not drive an onboard LED. The confirmation item emits a selection event for a downstream action. No GPIO is guessed for these actions.

## Build and hardware check

```
python -m platformio run -e viewe_rotary_gc9503
python -m platformio run -e viewe_rotary_st7701s
python -m platformio run -e viewe_rotary_gc9503_smoke
python -m platformio run -e viewe_rotary_st7701s_smoke
```

Only these rotary environments use Arduino-ESP32 3.1.3 / ESP-IDF 5.3.2 because the RGB driver requires the newer API, while the display library and Wire must share the legacy I2C API. Arduino 3.3 mixes incompatible I2C drivers in this integration. Other environments keep their existing SDK. Native USB CDC is the default; the corresponding `_uart` environments select module UART instead. Choose the transport in the Circular Menu workspace. Android requires a dedicated compiler pack for this SDK; the existing Arduino 2 offline packs cannot compile these RGB profiles.

The smoke firmware reports memory, panel initialization and HMI state changes. Serial `n`, `p`, `a`, and `b` simulate next, previous, activate and back. Encoder and touch tests require physical interaction.

Vendor reference: [VIEWE MD80ESP32 source](https://github.com/VIEWESMART/UEDX48480021-MD80ESP32-2.1inch-Touch-Knob-Display), inspected revision `830a74e6354aa482d382401bdb222a5efe82c917`. The vendor README names ST7701S, while its actual board configuration selects GC9503. Both profiles are available; ST7701S uses the library initialization sequence and remains experimental until tested on a matching panel.

Do not flash these images to an ESP32-S3 DevKitC just because its flash/PSRAM capacity matches. Connect the actual display module and verify its model/controller. After upload, check serial diagnostics for 16-MiB flash, 8-MiB PSRAM, filesystem mount and panel initialization; then test colors, all four rotations, touch corners, clockwise/counterclockwise detents, bounce, short/long/double presses, mixed touch/rotary navigation, dim/wake, Logics pulses and OTA recovery.

The ST7701S panel sequence and adapter wiring require hardware verification. Build and host tests do not establish those electrical facts.

## Round system dashboard

The default round UI uses the same twelve section icons as the web interface. Its clock, status pages, hardware metrics, memory, Logics groups, brightness, logs, and firmware actions use the existing firmware services. Select **System dashboard** in Display settings; disable it to use a custom Circular Menu. The configurable home timeout is 15–3600 seconds and does not interrupt an active OTA operation. A local display PIN uses separate security storage from the web PIN.

Hold the knob push continuously for **15 seconds** to reboot. A separate FreeRTOS task checks GPIO0, independently of the LVGL UI loop. Releasing before the threshold cancels the hold. Existing short press and long-press Back actions remain available. This resets the device without erasing saved settings; it is not a factory reset.

## Verified MD80E hardware (2026-10-10)

The supplied UEDX48480021-MD80E-V3.2 schematic describes the knob-only model. Glass taps did not change the original firmware either. Keep `oled.touchEnabled=false` on this hardware; CST826 remains an optional feature for actual MD80ET boards, not a tested feature of MD80E.

GC9503 operation has been verified on the device with 16 MiB flash and 8 MiB PSRAM. The round menu is steady at an 8 MHz RGB clock, clockwise selection is correct, and a press opens the selected page. This encoder has two quadrature transitions per physical detent; the rotary default and examples use `stepsPerDetent=2`. Explicit 1/2/4 settings remain available for other encoders.

Firmware 0.1.59-test.3 was uploaded through the web OTA endpoint with USB disconnected; it rebooted and remained accessible on Wi-Fi. The physical ST7701S variant and CST826 touch are still unverified. New rotary board presentation entries and a missing-presentation fallback prevent the GPIO page from stopping web startup.

## Round dashboard and clock

Firmware 0.1.59-test.6 centers the round gauges, applies encoder brightness changes immediately, shares LCD/Web security while preserving the existing PIN, and starts matching 256 × 256 static and animated boot artwork. The user confirmed these changes, including the locked clock, two-stage knob interaction and ten-second return to the clock. Subsequent clock refinement uses fixed digit cells, a 460-pixel ring with doubled stroke thickness, subsecond movement and contrasting successive colors; physical confirmation of that refinement is recorded separately from the earlier checks.

The user verified firmware 0.1.59-test.4: the system dashboard opens correctly and holding the knob push continuously for 15 seconds reboots to the dashboard. Firmware 0.1.59-test.5 places all twelve section icons on one enlarged ring. Device settings expose `clockUtcOffsetMinutes` (-720 to 840 in 15-minute steps), saved in NVS with default zero. The round idle clock and Web header apply this offset to device UTC; NTP and Logics schedules remain UTC. Alarm Clock already provides `valid`, `epoch`, and `clockTime` (YYYY-MM-DD HH:MM:SSZ).
