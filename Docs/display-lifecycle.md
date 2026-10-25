# Display lifecycle and updates

Display timeout is shared by the web interface, Windows designer, Android
designer and firmware. Zero seconds means always on. The timeout checkbox
defaults to enabled; OLEDs default to 60 seconds. A timed-out OLED enters its
controller's display-off mode and wakes when displayed content changes or an
alert arrives. Clock ticks alone do not wake it. Dimming and screen-off remain
separate settings. Onboard touch/encoder handling remains available to wake an
idle screen.

OTA uses the existing firmware upload protocol and host progress controls.
Before writing, the main loop suspends Logics execution, releases its execution
caches without changing saved run modes, stops audio/peripheral outputs,
disconnects MQTT and releases normal display widgets. During the transfer only
the OTA service, compact progress status and update display remain active. A
cancelled update restores the normal interface and saved execution modes; a
successful update reboots through the existing verified-update path.

The round LCD update screen uses native-resolution Montserrat coverage and a
blue perimeter arc matching the clock accent. Its scanout callback and data
reside in internal RAM and require no flash calls or PSRAM framebuffer. Color
SPI panels reuse this renderer with a straight bar for rectangular screens.
Monochrome I2C OLEDs retain their lightweight GFX progress renderer. USB ROM
bootloader flashing cannot run application display code; progress remains in
the flashing application during that operation.

## External SPI controllers

The shared controller catalog is `web/modules/spi-display-catalog.json`.
Supported drivers are GC9A01, ST7735R, ST7789, ST7796S, ILI9341, SSD1331,
SSD1351, SSD1306 SPI and SH1106 SPI, with explicit panel dimensions and tab
variants. Select the exact module variant; a matching controller name alone
does not establish resolution, offsets or wiring.

SCK, MOSI, CS and DC are required. RST and backlight are optional, and MISO
is not needed for this write-only interface. Each display keeps its own
controller, orientation, brightness, interface and timeout configuration in
its existing peripheral bindings. A free hardware SPI host is used when
available; otherwise the display uses software SPI without remapping an
active SD or onboard display bus.

These drivers target the full ESP32/S2/S3/C3 firmware profiles. The separate
compact ESP8266/8285/C2/C6 firmware does not advertise display-driver support.
External modules require physical verification; constructor/build checks do
not establish correct panel offsets or colors for every vendor variant.

Pinned vendor revisions, file hashes and retained upstream license notices
are recorded in `lib/external-display-vendors.json`. Font subsets are generated
from the project's existing LVGL Montserrat assets.
