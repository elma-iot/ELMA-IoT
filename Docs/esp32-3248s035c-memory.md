# ESP32-3248S035C memory and audio policy

This classic ESP32 board has no PSRAM. Its LCD, SD card and internal-DAC audio must share internal RAM. The firmware therefore uses a board-specific web bundle and runtime policy rather than exposing every generic peripheral.

## UI lifetime

- Routine web status polling requests `detail=compact` outside Hardware and Info. The full status response remains available to older clients and hardware-monitor pages.
- LCD page changes discard the previous page's JSON snapshot. Idle radio directory and SD directory caches are discarded when leaving their pages, or while the LCD is disabled. Active directory requests finish normally before their results are released.
- The SD transfer buffer is released on upload completion, cancellation and failure.
- Background TLS firmware checks require sufficient internal 8-bit RAM for the worker stack, TLS handshake and the active device services. On this factory-partition board, low-memory checks defer with USB update guidance instead of consuming the web/audio headroom.
- LVGL opacity layers use a 4 KB target / 2 KB fallback, and software rotation uses at most 2 KB. Large areas are rendered in tiles.
- Idle LVGL glyph/mask scratch buffers are released after drawing under memory pressure and after the startup animation.
- The 3248 AsyncTCP task now reserves 8 KB. A measured 12 KB stack left about 7 KB unused; 8 KB keeps roughly 3 KB of margin while returning 4 KB to the heap. Full panic capture identified a Wi-Fi watchdog, not stack overflow. Recheck the high-water mark with new routes.
- HTTP admission and request-header parsing check internal 8-bit memory before entering throwing STL allocations. Generic streamed responses have a bounded transmit window on this board; built-in flash assets retain their existing zero-copy, two-packet window.
- Disabling the LCD removes its LVGL display/input objects, DMA pixels and panel instance, stops touch and SPI activity, and sleeps the controller. The power rail cannot be disconnected by software. Enabling it recreates the UI.
- Dim timeout halves the configured brightness. Screen-off timeout destroys the LVGL widgets and pixel buffers, sleeps the controller and turns off the backlight. The small touch driver remains alive for wake. Zero disables either timer.

These changes do not pause the audio worker, wired Logics, light/RGB logic, telemetry triggers or required metrics sampling. Only presentation data and inactive UI resources are discarded. Disabling required sampling merely because a tab is hidden would break user logic graphs.

The USB-powered 3248 board keeps Wi-Fi modem sleep disabled to reduce power-saving latency while audio is stopped. Other board profiles retain their existing power policy. Test.51 retains SDK default Wi-Fi buffers on this board while investigating loss of IP traffic despite association. Buffer and b/g-versus-b/g/n comparisons did not establish a single Wi-Fi root cause. Other Sunton profiles keep their existing buffer policy. See the [ESP-IDF 4.4 Wi-Fi guide](https://docs.espressif.com/projects/esp-idf/en/release-v4.4/esp32/api-guides/wifi.html).

Test.51 held 240 MHz after a captured Wi-Fi-task watchdog following a manual downshift. The new board-specific governor instead uses **80 MHz idle / 160 MHz busy**, selecting the 320 MHz PLL before Wi-Fi starts and never retuning it during operation. The SDK has `CONFIG_PM_ENABLE` disabled; this is an application governor, not SDK PM locks or light sleep. All web requests boost immediately, with a 10-second hold; audio buffering/playback, active LCD presentation, storage writes, OTA and sustained CPU load also hold 160 MHz. Other boards retain their existing governor. A lower clock does not guarantee a measured temperature reduction; that requires hardware measurement.

Espressif's [IDF 4.4 PM implementation](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_pm/pm_impl.c) explicitly avoids 240-to-80/160 switching when the APB lock is held because it requires disabling the PLL. The 80/160 pair shares the PLL and keeps APB at 80 MHz. The existing Arduino clock API updates peripheral callbacks and the FreeRTOS tick divisor. Clock changes are serialized across HTTP/audio/main tasks.

## LCD and web presentation ownership

Only this no-PSRAM board shares the heavy presentation resources. While LCD controls are active, `/` and `/fallback` show a small self-contained synchronized page, rather than loading the full app and diagrams. It shows current track, playback, volume and position, and offers Stop and ìUse web controls.î The latter queues the handoff on the main task; no LVGL object is destroyed on the AsyncTCP thread. Existing full pages detect the presentation response header and unload to this lightweight view.

After screen timeout or a web claim, LCD widgets, pixel/text buffers and LVGL scratch buffers are released. Touch detection remains active. Touch wake first gates new heavy HTTP work, consumes the wake gesture, waits for existing responses and memory headroom, then rebuilds LCD widgets without replaying the boot animation. Busy foreground SD writes and OTA defer touch takeover. If headroom stays insufficient, web ownership is retained and audio continues. Explicit ìEnable displayî off still destroys the whole panel/touch instance for interference testing.

The HTTP listener and tiny status/security/control endpoints remain available in both modes; turning off the listener completely would prevent synchronization and browser handoff. Heavy requests return a controlled 503 with `X-ELMA-Presentation: lcd` while LCD owns the UI. PIN locking and HTTP authentication continue to apply; claiming control requires an authenticated same-origin request, and locked interfaces require unlock first. Handoffs do not stop the audio worker or wired logic execution.


## Audio

The onboard SC8002B is an analog amplifier on GPIO26, driven by DAC2; GPIO25 belongs to the touch controller. PCM gain is applied before 8-bit DAC conversion. Start/stop gain and DAC-bias ramps reduce switching clicks. Test.48 removes experimental twice-rate interpolation, dither and error feedback after listening on the board showed worse sound despite a favorable host simulation. Playback uses native sample rates and one independently biased PCM frame per decoder frame. This is software processing; this ESP32 has no hardware MP3/AAC/FLAC decoder.

The audio owner task serializes decoder operations. MP3 workspaces that permit aligned 32-bit access can use IRAM; byte-addressed data stays in DRAM. Internal-DAC DMA and the audio manager use the native decoder clock and one frame per source sample. Pressing Stop reconstructs the muted idle engine to release retained decoder and network caches; switching tabs never does this. Music starts stopped after reboot.

## Validation limits

Use internal 8-bit heap and largest free block, not aggregate heap including word-only IRAM, to assess headroom. A universal ‚Äúunder 50% SRAM‚Äù target is not guaranteed for every simultaneous LCD, decoder and network workload. Validate repeated LCD toggles, browser polling, SD playback and radio with the actual device. Host tests establish frame size, native clock and silence behavior; speaker distortion and interference still require listening on the board.

Firmware updates preserve existing configuration, SD media and Wi-Fi credentials. The factory-only partition layout does not permit writing an OTA selector at 0xE000 because that address is inside NVS.

Before test.51, full serial capture showed the core-0 Wi-Fi task starving IDLE0 and triggering the task watchdog after the CPU downshifted to 80 MHz. Increasing the HTTP stack alone did not fix it. With the CPU fixed at 240 MHz and the LCD disabled, test.51 completed 28/29 parallel HTTP requests without a watchdog reset; one configuration request returned a controlled 503 busy response and succeeded on the follow-up. The follow-up settings, full status, HTML and 216,687-byte gzip JavaScript transfers all succeeded while AvtoFM played. Content lengths and gzip CRCs were checked, and the downloaded JavaScript passed `node --check`. HTTP stack headroom measured about 7 KB. Low-volume speaker quality still requires human listening.

## Native PCM integrity and optional quality diagnostics

| Area | Change | Expected effect / limit |
| --- | --- | --- |
| Stop / engine cleanup | Start the muted I2S clock before draining the old engine; bound failed drain retries and reset partial-frame cursors | Avoid waiting forever on stopped DMA; decoder/network buffers are released before LCD allocation |
| DMA backpressure | Preserve failed stereo frames and 8-bit mono half-frame state; cache processed PCM across retries | No dropped/repeated frames or second EQ/volume pass when DMA rejects a write |
| EQ / conversion | Saturate finite float output before PCM16 conversion, count clipping/nonfinite values; remove signed-left-shift undefined behavior | Prevent wrapping at overloaded EQ output; cannot repair a clipped source |
| LCD allocation | Lazy 1-bit canvas; no boot animation on re-enable; discard page caches | Remove a temporary 19,200-byte allocation and reduce enable peaks |
| Observability | Scalar counters and optional timing logs; UART bursts off by default | Input-empty observations indicate starvation risk, not measured DMA underruns; service time includes blocking writes |
| Physical DAC | Native sample clock, GPIO26 DAC2, upper 8 bits of each 16-bit word | Low-volume quantization and analog noise remain hardware limits; no claimed THD improvement without measurement |

Default conversion remains the previously validated raw path. USB commands `ELMA_DAC_ROUND`, `ELMA_DAC_TPDF`, and `ELMA_DAC_FEEDBACK` enable optional native-rate experiments; `ELMA_DAC_RAW` restores the default. TPDF is approximately one DAC LSB peak-to-peak, excludes exact digital silence, clamps at the rails, and trades quantization distortion for noise. Feedback is bounded first-order error feedback with no oversampling. Neither is enabled at boot or saved to settings. No invented calibration LUT or extra low-pass filter is applied.

`ELMA_AUDIO_DIAGNOSTICS` reports native sample/I2S clocks, compressed-input occupancy/capacity, peak PCM, clipping and driver write errors, empty-input observations and maximum decoder service gap/time. `ELMA_AUDIO_DIAGNOSTICS_RESET` clears the scalar counters; `ELMA_AUDIO_TIMING_ON/OFF` controls detailed periodic logging. Diagnostics do not perform continuous ADC sampling or allocate a waveform buffer.

The pipeline is software MP3/AAC/FLAC/WAV decode, signed PCM16 (8-bit sources are expanded), existing 6 dB EQ headroom, software volume, mono output, unsigned DAC bias, I2S0 hardware DMA, GPIO26 DAC2 and SC8002B amplifier. DMA is four 256-frame stereo16 buffers (4 KB, about 23 ms at 44.1 kHz); input buffering is 8 KB on this board. 24-bit WAV/FLAC is currently rejected, not silently truncated. There is no hardware compressed-audio decoder. See [Espressif DAC documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/dac.html).

Host tests inject errors and short writes into the actual patched frame pump for all four 8/16-bit mono/stereo cases, confirming ordered identical PCM and one filter/gain pass per accepted frame. Additional tests cover saturation, silence, bounded conversion, default raw mode, DMA priming and repeatable dependency migration. `tools/generate-dac-test-wav.py` creates optional 1 kHz PCM16 files externally with fades, silent gaps and selectable dBFS segments; it never executes on boot or insertion. Listening tests should start with a low device volume. Compare equal-level files with LCD on/off and Wi-Fi traffic; use a scope/audio analyzer for THD+N claims.

Supply/ground coupling from LCD/Wi-Fi, the board's analog reconstruction/coupling circuit, amplifier gain and the internal DAC's nonlinearity cannot be removed by increasing network buffers. Consider hardware changes only after isolating these with measurements; an external DAC is not required by these firmware changes.

## Hardware verification, October 20, 2026

Test.54 was flashed to COM13 using the application's automatic-reset fallback, with application-only hash verification and no BOOT button. Two controlled quiet intervals reached 80 MHz with Wi-Fi still associated; HTTP restarted automatically after 30 seconds and incoming requests boosted to 160 MHz. Chrome's background polling otherwise correctly kept the web lease active. The stopped audio worker now polls every 20 ms rather than every tick; active playback retains 1 ms service, and three consecutive load samples filter brief spikes.

The USB-only `ELMA_POWER_IDLE_TEST` command temporarily pauses HTTP for 30 seconds, only while playback is idle, web presentation owns the UI, OTA is idle and SD writes are finished. It does not disconnect Wi-Fi, alter credentials or change Windows settings, and it automatically restores the listener. It is a diagnostic command, not the normal power policy.

Repeated AvtoFM Play/Stop, LCD disable/enable, web claims and screen timeout completed without panic. Idle LCD-to-web handoff returned about 21 KB of internal memory. SD MP3 playback advanced from 5 to 16 seconds through LCD enable and timeout, retaining playing state. On-demand counters reported no clipped/nonfinite samples, driver errors or short writes; input-empty observations were zero in these runs. Service-gap counters include starts/settings transitions and do not establish gapless audio or acoustic quality. AsyncTCP's measured stack margin was 2,936 bytes; the main task retained about 2.3 KB.

The 216,775-byte gzip JavaScript and HTML passed length/CRC verification, and downloaded JavaScript passed Node syntax checking. The in-app Chromium browser loaded the small LCD page, switched to the full interface and displayed the running firmware version and display controls. Physical touch wake and audible distortion still require human validation. A screen-timeout form bug found during this browser test is fixed in Test.55: the value is now serialized as a number, including zero to disable the timer, rather than a string silently ignored by the strict firmware parser. Unit tests cover both 180 and zero seconds.

Test.55 final verification on October 21 repeated the 80 MHz idle / 160 MHz HTTP recovery and advancing SD playback checks without panic. Its timeout was edited through the browser, read back as 180 seconds, and retained after reboot with Wi-Fi/HTTP recovery. The final gzip JavaScript passed length, CRC, versioned ETag and syntax checks. Static RAM is 66,972 bytes (20.4%); the image fits the existing 3.69 MB application partition. Runtime headroom still depends on the workload.
