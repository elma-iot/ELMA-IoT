# ESP32-3248S035C memory and audio policy

This classic ESP32 board has no PSRAM. Its LCD, SD card and internal-DAC audio must share internal RAM. The firmware therefore uses a board-specific web bundle and runtime policy rather than exposing every generic peripheral.

## UI lifetime

- Routine web status polling requests `detail=compact` outside Hardware and Info. The full status response remains available to older clients and hardware-monitor pages.
- LCD page changes discard the previous page's JSON snapshot. Idle radio directory and SD directory caches are discarded when leaving their pages, or while the LCD is disabled. Active directory requests finish normally before their results are released.
- The SD transfer buffer is released on upload completion, cancellation and failure.
- Background TLS firmware checks require sufficient internal 8-bit RAM for the worker stack, TLS handshake and the active device services. On this factory-partition board, low-memory checks defer with USB update guidance instead of consuming the web/audio headroom.
- LVGL opacity layers use a 4 KB target / 2 KB fallback, and software rotation uses at most 2 KB. Large areas are rendered in tiles.
- Idle LVGL glyph/mask scratch buffers are released after drawing under memory pressure and after the startup animation.
- The 3248 AsyncTCP task reserves 12 KB after the prior 6 KB limit left only 876�888 bytes during browser requests and an abort occurred during asset transfer. This restores stack safety margin rather than treating healthy free heap as proof of stack safety. Full panic capture subsequently identified a Wi-Fi-task watchdog timeout, not a stack-overflow exception.
- HTTP admission and request-header parsing check internal 8-bit memory before entering throwing STL allocations. Generic streamed responses have a bounded transmit window on this board; built-in flash assets retain their existing zero-copy, two-packet window.
- Disabling the LCD removes its LVGL display/input objects, DMA pixels and panel instance, stops touch and SPI activity, and sleeps the controller. The power rail cannot be disconnected by software. Enabling it recreates the UI.
- Dim timeout halves the configured brightness. Screen-off timeout sleeps the controller and turns off the backlight until touch; this lightweight sleep retains the UI for fast wake. Zero disables either timer.

These changes do not pause the audio worker, wired Logics, light/RGB logic, telemetry triggers or required metrics sampling. Only presentation data and inactive UI resources are discarded. Disabling required sampling merely because a tab is hidden would break user logic graphs.

The USB-powered 3248 board keeps Wi-Fi modem sleep disabled to reduce power-saving latency while audio is stopped. Other board profiles retain their existing power policy. Test.51 retains SDK default Wi-Fi buffers on this board while investigating loss of IP traffic despite association. Buffer and b/g-versus-b/g/n comparisons did not establish a single Wi-Fi root cause. Other Sunton profiles keep their existing buffer policy. See the [ESP-IDF 4.4 Wi-Fi guide](https://docs.espressif.com/projects/esp-idf/en/release-v4.4/esp32/api-guides/wifi.html).

The 3248 CPU stays at 240 MHz even with its LCD disabled. Captured stalls followed the manual governor downshift to 80 MHz; the Wi-Fi task starved IDLE0 and triggered the watchdog while internal heap remained healthy. Other profiles retain their governor. See [Arduino-ESP32 issue 7240](https://github.com/espressif/arduino-esp32/issues/7240) for a related CPU-clock/Wi-Fi report.

## Audio

The onboard SC8002B is an analog amplifier on GPIO26, driven by DAC2; GPIO25 belongs to the touch controller. PCM gain is applied before 8-bit DAC conversion. Start/stop gain and DAC-bias ramps reduce switching clicks. Test.48 removes experimental twice-rate interpolation, dither and error feedback after listening on the board showed worse sound despite a favorable host simulation. Playback uses native sample rates and one independently biased PCM frame per decoder frame. This is software processing; this ESP32 has no hardware MP3/AAC/FLAC decoder.

The audio owner task serializes decoder operations. MP3 workspaces that permit aligned 32-bit access can use IRAM; byte-addressed data stays in DRAM. Internal-DAC DMA and the audio manager use the native decoder clock and one frame per source sample. Pressing Stop reconstructs the muted idle engine to release retained decoder and network caches; switching tabs never does this. Music starts stopped after reboot.

## Validation limits

Use internal 8-bit heap and largest free block, not aggregate heap including word-only IRAM, to assess headroom. A universal “under 50% SRAM” target is not guaranteed for every simultaneous LCD, decoder and network workload. Validate repeated LCD toggles, browser polling, SD playback and radio with the actual device. Host tests establish frame size, native clock and silence behavior; speaker distortion and interference still require listening on the board.

Firmware updates preserve existing configuration, SD media and Wi-Fi credentials. The factory-only partition layout does not permit writing an OTA selector at 0xE000 because that address is inside NVS.

Before test.51, full serial capture showed the core-0 Wi-Fi task starving IDLE0 and triggering the task watchdog after the CPU downshifted to 80 MHz. Increasing the HTTP stack alone did not fix it. With the CPU fixed at 240 MHz and the LCD disabled, test.51 completed 28/29 parallel HTTP requests without a watchdog reset; one configuration request returned a controlled 503 busy response and succeeded on the follow-up. The follow-up settings, full status, HTML and 216,687-byte gzip JavaScript transfers all succeeded while AvtoFM played. Content lengths and gzip CRCs were checked, and the downloaded JavaScript passed `node --check`. HTTP stack headroom measured about 7 KB. Low-volume speaker quality still requires human listening.
