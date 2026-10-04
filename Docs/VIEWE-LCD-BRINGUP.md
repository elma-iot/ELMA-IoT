# VIEWE UEDX24320028E-WB-A LCD investigation


## Current result — 2026-10-05

The physical test unit is the 3.5-inch 320×480 variant despite its 2.8-inch PCB marking. Selecting the manufacturer 3.5-inch driver resolved the black picture, and the user confirmed ELMA displays. Windows 0.1.77 provides separate 2.8-inch and 3.5-inch profiles. The portrait interface update subsequently built, booted and drew successfully; physical review of that new layout remains pending. The diagnostic candidates below are retained as history, not as current setup instructions.

## 2026-10-04

Hardware: ESP32-S3, GC9307 240×320 panel, CHSC6540 touch. The test board
responds at 192.168.1.179. The first updated image (build 21:15:46) booted,
but the user confirmed that the LCD remained black while touch woke the
backlight. Successful SPI/DMA completion does not confirm visible pixels.

### Reference comparison

- [VIEWE reference repository](https://github.com/VIEWESMART/UEDX24320028ESP32-2.8inch-Touch-Display)
  identifies the panel and GPIOs: CS42, SCK40, MOSI45, DC41, reset39,
  backlight13; touch SDA1/SCL3.
- [Panel specification, UE028QV-RB40-A058A](https://github.com/VIEWESMART/UEDX24320028ESP32-2.8inch-Touch-Display/blob/main/information/UE028QV-RB40-A058A.pdf),
  page 6: four-wire, eight-bit serial uses IM2/IM1/IM0 = 1/1/0. Retain
  the manufacturer's GPIO47 low / GPIO48 high setup; do not copy the
  different 3.5-inch board's interface-selection levels.
- The same specification, page 12, specifies a minimum 50 ns serial write
  cycle. Limit this board to 20 MHz; the inherited 80 MHz setting exceeds
  that published timing, even though it appears in the vendor library.
- [Separate VIEWE implementation](https://github.com/elik745i/ESP32-2432S024C-Remote/blob/main/src/display_compat.cpp)
  uses 40 MHz for this target and explicitly writes RGB565 (0x3A/0x55)
  after the vendor register setup, before sleep-out. It waits 120 ms after
  sleep-out and 20 ms after display-on. Its 3.5-inch overrides must not be
  applied to the 2.8-inch board. Repository code is comparison evidence,
  not proof of operation on this particular device.
- [ESP-IDF 4.4.7 panel operations](https://github.com/espressif/esp-idf/blob/v4.4.7/components/esp_lcd/src/esp_lcd_panel_ops.c)
  confirm the existing compatibility adapter's display-on boolean inversion.
  Do not remove that inversion when building with Arduino 2 / IDF 4.
- [Zephyr's exact-board definition](https://github.com/zephyrproject-rtos/zephyr/blob/main/boards/viewe/uedx24320028e_wb_a/uedx24320028e_wb_a_procpu.dts)
  independently confirms 240×320, RGB565 and IM0 low / IM1 high. It also
  uses 80 MHz from the vendor setup, so exceeding the published timing
  is a compatibility concern, not yet a proven cause of this device's failure.

### Candidate changes

The VIEWE profile now uses 20 MHz, explicitly reapplies RGB565 after vendor
initialization and allows 20 ms after display-on. Other boards are unchanged.
The LVGL dashboard logs its first flush separately from the monochrome boot
canvas, allowing runtime confirmation that the UI itself reached the driver.

Build, OTA and physical validation results must be recorded separately.
Touch/backlight and transfer logs alone are insufficient to close the issue.

### Timing candidate result

The 2,407,056-byte image compiled successfully and booted (main boot log:
21:49:07). Settings and the graph were verified against a pre-upload backup.
The first LVGL flush completed at 240×20 RGB565. The user confirmed the
display was still black; the timing changes alone did not resolve it.

The HTTP build date remained 21:15:46 because `__DATE__`/`__TIME__` were
compiled into a cached web-server object. Do not use that field alone to
verify incremental builds. An arbitrary diagnostic upload filename also
triggered the existing version-based rollback warning despite the new
diagnostic code running; use the actual firmware version in OTA filenames.

Next diagnostic: enable the V1.1 schematic's LCD SDO connection on GPIO46
and log raw DCS reads after initialization. Use 5 MHz during readback,
below the datasheet's minimum 150 ns read cycle limit (6.66 MHz).
The user believes the panel is 2.8 inches and confirms it displayed a
picture before ELMA was installed. Continue software/controller diagnosis;
do not infer a defective panel from successful DMA with a black screen.

### Readback and SDK transport follow-up

The readback candidate booted and preserved settings/graph. DCS 04/09/0A/0B/0C
returned all-zero bytes during the first initialization and all-FF bytes
during the second; neither is a valid controller identity. Output-only
GPIOs read as zero with input sensing disabled, so those initial GPIO
readings are not evidence that reset or mode levels are wrong.

The offline Arduino 2 core uses the IDF 4 SPI LCD transport. Its
`panel_io_spi_tx_param` unconditionally sets `SPI_TRANS_CS_KEEP_ACTIVE`,
even when no parameters follow. [IDF 5.1.4's transport](https://github.com/espressif/esp-idf/blob/v5.1.4/components/esp_lcd/src/esp_lcd_panel_io_spi.c)
only sets that flag when there are parameters. Backport that change for
VIEWE/IDF 4 using the Apache-2.0 IDF 4.4.7 transport under a separate
factory symbol; keep other boards and IDF 5 builds on their stock transport.
This is a source-confirmed transport difference; physical validation is
still required to establish whether it explains this LCD failure.

### Factory ESP-IDF example cross-check

The manufacturer's examples/esp_idf/PCB_UEDX24320028_SDK/main/board.c
contains an explicit 2.8-inch GC9307 sequence. Its GPIO and IM0/IM1 levels
match ELMA. It writes MADCTL 0x48 and COLMOD 0x05 after FE/FE/EF;
its other vendor register values match the board profile. The checked-in
board.h selects the 3.5-inch 320x480 variant by default, so that default
must not be copied blindly to the 2.8-inch board. Its reset is software-only
and SPI host is SPI3; ELMA currently uses the schematic's GPIO39 reset
and SPI2. These remain comparison points if the transport fix fails.

Source: https://github.com/VIEWESMART/UEDX24320028ESP32-2.8inch-Touch-Display/blob/main/examples/esp_idf/PCB_UEDX24320028_SDK/main/board.c


### SPI compatibility candidate uploaded

The scoped IDF 4 transport compiled and linked successfully after adding
its C source to ESP32_Display_Panel/library.json. Binary: 2,407,104 bytes;
static RAM: 78,764 bytes; application flash: 2,406,669 / 4,194,304 bytes.
OTA boot confirmed by the unique CS-fix log marker (web build 22:08:11).
Settings and Logics were preserved. With input sensing enabled, both
initializations report IM0=0, IM1=1, reset=1, matching the schematic.
DCS reads remain FF and do not identify the controller. The first LVGL
240x20 transfer succeeds. Physical screen confirmation is pending; this
is not yet a verified resolution of the black screen.


The user confirmed that the SPI compatibility candidate still shows only
backlight. This excludes that change as a sufficient fix. At the user's
request, next test the manufacturer's original firmware/porting_lvgl_28.bin
without rebuilding it. Official image SHA256:
8e0aeb25468c37638c3fade00377b7cf04436278b07bff7ffbd09e704e330bfa.
Its embedded application is ESP32-S3 / IDF 5.3.1, built Nov 16 2024
11:48:45. Both checksum and validation hash pass. It is a merged image
with partition table at 0x8000 and factory application at 0x10000.
Back up the full ELMA flash before replacing the manufacturer layout.


### Original manufacturer image flashed

The unmodified manufacturer porting_lvgl_28.bin was written to COM11
at 0x0 (779,120 bytes). Esptool verified the flash hash and reset the board.
The LCD/touch result is awaiting the user's observation.

Long USB readbacks failed intermittently with truncated SLIP frames on
esptool 5.1 and 4.5.1. A blockwise backup succeeded: sixteen 64 KiB reads,
each verified by the device's MD5, assembled into elma-first-1MiB.bin.
SHA256: 9a0bc281bb8e4a68c4a739db7df761b0d395c2dbc04a556f1c5220521822e3e5.
Only the first 0xBF000 bytes were overwritten; flash beyond that was not
erased. Restoring the backed-up first 1 MiB restores ELMA's original
bootloader, partition table, OTA selection and overwritten application bytes.
Backups and logs are under Windows/.elma-flasher-build/viewe-original-demo-test.
Settings and Logics JSON backups are also saved there; keep them private.


### Corrected panel identification: 3.5-inch 320x480

The user corrected the earlier 2.8-inch estimate using the purchase listing:
selected variant is 320x480, 3.5 inches. The PCB photo reads
UEDX24320028E-WB-A V1.1 202502; this PCB marking is shared across panels
and must not by itself select GC9307/240x320.

Downloaded and flashed the unmodified official 3.5-inch high-resolution
portig_lvgl_h35.bin from VIEWESMART/UEDX24320028ESP32-3.5inch-320_480-Display.
Image: 783,936 bytes, merged at 0x0; application built Feb 22 2025 14:31:40
with IDF 5.3.2. Image checksum/hash and post-write flash hash passed.
SHA256: d1004f38959a1150c5f4de962dc9a6b67765fe9c76df33a582d477a0aa846c6d.
COM11 reset after successful write. The user confirmed the 3.5-inch demo displays correctly.
The original 2.8-inch demo remained black: the fitted panel variant was the root cause.
The existing first-1-MiB ELMA backup covers this entire write as well.


### Integrated panel choices

Windows exposes separate 2.8-inch 240x320 (board ID 14) and 3.5-inch 320x480
(board ID 25) profiles. The shared PlatformIO hardware environment selects the
manufacturer panel definition from the chosen board. The 3.5-inch variant uses
the official ST7789-compatible initialization and IM0/IM1 both high; 2.8-inch
retains its GC9A01-compatible definition and IM0 low. Framebuffer, LVGL geometry
and touch bounds follow the compiled variant. Speculative SPI transport changes
were removed. The subsequent ELMA build and physical display test passed.

Both profiles use the user-supplied Fritzing R3 artwork with machine-readable
pad anchors. EN and backlight-terminal labels were corrected against the PCB
photo and schematic. The two asset filenames permit board-specific web bundles.

### ELMA 3.5-inch integration validation

The final firmware build passed (2,405,680-byte binary; 78,644 bytes static RAM).
Restored the verified first-1-MiB backup, then uploaded ELMA through its inactive
OTA partition. At 192.168.1.179, status reports `viewe-uedx32480035e-wb-a`,
settings report 320x480 with touch enabled, and logs report an LVGL 320x20 flush.
The original Logics graph was preserved. The web board selector contains only
the compiled 3.5-inch profile. Its served R3 SVG has 40 marked pads, distinct EN
and two GND pads (SVG optimization removes connector IDs, so web wiring uses
the exported contact map). The user confirmed that ELMA now displays correctly.

Windows executable built successfully; packaged startup and native workspace
smoke tests passed. Both bundled SVGs were byte-compared against the source.

### Portrait interface follow-up

The touchscreen dropdown follows the web tab order and configured-feature
visibility. Logics and wiring use compact lists and controls; detailed graph
and diagram editing stays in the web and Windows interfaces, as agreed.
Settings and actions use the same firmware handlers as the web interface.
Browser settings refresh on touchscreen-equipped devices without overwriting
an actively edited form. Touchscreen drafts also survive remote updates.

A fixed status bar shows Wi-Fi AP/STA state, four RSSI bars, MQTT connection
state (red cross when disconnected), local time, and optional playback/storage/
battery indicators where the screen width permits. Status is read from live
device state, independently of the selected tab. Both 240x320 and 320x480
settings retain their compiled display geometry.

Validation on 2026-10-05: native settings-path tests and five browser
navigation/concurrent-edit/geometry tests pass. Firmware built successfully
(2,442,689 bytes used in the application partition; 58.2%) and was uploaded
through OTA to 192.168.1.179. The device reports touchscreen support and a
successful portrait LVGL flush. Display settings, peripheral UI configuration,
and the complete Logics graph were compared with the immediate pre-update
backup and preserved. Physical inspection of the new portrait layout remains
for the user; a successful flush alone does not verify its visual appearance.
The refreshed Windows executable passed startup and native workspace smoke
tests, and its bundled touchscreen source was hash-compared with the repository.
