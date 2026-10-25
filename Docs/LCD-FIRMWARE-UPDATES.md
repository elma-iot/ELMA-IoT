# LCD firmware updates

Firmware transfers handled by the running application use an exclusive update
presentation. Round VIEWE panels show centered `Updating` text, animated dots,
a percentage and a blue clockwise perimeter ring starting at twelve o'clock.
Rectangular panels use the same text with a blue horizontal bar underneath.
Monochrome OLEDs provide a white text/bar fallback.

Before the first flash write, loopTask suspends Logics and owned outputs,
releases evaluation caches, playback resources and normal LCD widgets, stops
motors/buzzer and disconnects MQTT. Normal storage polling, UI input, metrics,
peripheral polling and automation work are skipped. The Wi-Fi/HTTP transport,
watchdog, upload/status/cancel endpoints and dedicated progress renderer remain
available. Other HTTP requests receive a temporary 503 response. Existing saved
Logics modes and settings are not rewritten. Failed/cancelled updates restore
the interface and the saved Logics modes; playback remains stopped.

RGB LCD scanout normally reads PSRAM, which competes with flash writes. For an
update, the fitted controller's initialization and bus timings are retained,
but scanout changes to two 240x240 monochrome pages in internal RAM (14,400
bytes), expanded to RGB565 by an IRAM callback and two ten-line SRAM bounce
buffers (19,200 bytes). The ISR reads no PSRAM, font data or flash code. Pages
are exchanged at frame boundaries. Normal LVGL is not running during this mode.
This avoids requiring a different prebuilt Arduino SDK with PSRAM XIP enabled.

Successful updates retain the progress screen through verification and the
existing rollback-aware reboot. Shutdown/update sounds are skipped so they
cannot recreate a decoder during finalization.

Standard USB flashing resets the chip into its ROM download bootloader. The
application is not running and cannot display on-device progress during that
transfer; USB progress remains on the host. An installed firmware update is
needed before the new on-device OTA screen can appear on subsequent transfers.

Reference: [Espressif RGB LCD and bounce-buffer-only operation](https://docs.espressif.com/projects/esp-idf/en/v5.3.6/esp32s3/api-reference/peripherals/lcd/rgb_lcd.html).
