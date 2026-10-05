# I²C sharing and BNO055 — 2026-10-05, unreleased

Devices share SDA and SCL by **distinct seven-bit device addresses**. Internal register numbers may overlap. Both lines must match; crossed SDA/SCL or sharing only one line is rejected. Reset/interrupt pins remain exclusive GPIOs.

Windows and web configuration expose an I²C address selector. It declares the address actually selected by the module's straps/jumpers; it cannot change a soldered hardware address. With no declared address, the checker conservatively reserves every possible address. Unknown/custom devices require a declared address before sharing. Multiple PCA9685 devices may share their broadcast All Call address, but must have distinct unicast addresses; All Call cannot be selected as a unicast address. Address metadata is excluded from GPIO ownership and diagram pin labels.

The shared profile table covers every catalog entry with SDA/SCL. It feeds Windows catalog generation and firmware validation, and is used directly by the browser. Compatibility does not create a missing runtime driver.

| Catalog device | Seven-bit addresses considered | Runtime coverage in this audit |
| --- | --- | --- |
| I²C OLED | 0x3C / 0x3D; actual OLED setting used | SSD1306 / SH1106 driver, primary display |
| DFRobot BNO055 | 0x28 / 0x29 | New full ESP32 service, one sensor |
| DS3231 RTC | 0x68 | Existing clock service, one RTC |
| MPU6050 | 0x68 / 0x69 | Catalog/wiring; no sensor service found |
| BNO080 / BNO085 | 0x4A / 0x4B | Catalog/wiring; no sensor service found; see vendor ESP32 interoperability warning |
| WM8960 audio codec | 0x1A | Catalog/wiring; no codec-control initialization found |
| ES8388 audio codec | 0x10 / 0x11 | Catalog/wiring; no codec-control initialization found |
| MCP23017 | 0x20–0x27 | Catalog/wiring; no expander service found |
| PCF8574 / A | 0x20–0x27 / 0x38–0x3F | Catalog/wiring; no expander service found |
| PCF8575 | 0x20–0x27 | Catalog/wiring; no expander service found |
| ADS1015 / ADS1115 | 0x48–0x4B | Catalog/wiring; no ADC service found |
| MCP4725 | 0x60–0x67 across factory variants | Catalog/wiring; no DAC service found |
| PCA9685 | Hardware range 0x40–0x7F; selector excludes reserved 0x78–0x7F; also reserves power-on All Call 0x70 | Catalog/wiring; no PWM service found |
| Custom display / generic I²C expander / communication I²C | Unknown until declared | No generic register driver assumed |

## Runtime bus ownership

- OLED, DS3231 and BNO055 use one shared external bus and a recursive transaction lock. HTTP RTC writes cannot interleave with display or sensor transactions. Reapplying the same pins does not tear down the bus.
- Normal external bus: controller 0. Sunton capacitive touch stays on controller 1. Camera SDK configuration uses controller 1 for SCCB.
- VIEWE onboard touch stays on controller 0; external devices use controller 1 instead.
- RTC no longer unconditionally takes controller 1. C3 can share its one controller between compatible OLED/RTC/BNO055 devices.
- Implemented external drivers require one matching SDA/SCL pair. Independent additional buses and I²C multiplexers are not implemented.
- All shared external transactions, including OLED transfers, use 100 kHz. Use 3.3 V-compatible bus pull-ups, common ground and appropriate total pull-up resistance. Module supply voltage alone does not establish bus voltage compatibility. Address tables describe the named chips: some breakout boards add another chip (for example DS3231 boards with EEPROM, or Gravity BNO055 with BMP280). Those extra responders must also be checked against the other devices; the editor cannot discover an unidentified physical module remotely.
- ESP8266/ESP8285/C2 compact profiles retain their existing driver restrictions; this does not add unsupported sensor drivers to compact firmware.

## BNO055

Support targets the BNO055 functions shared by DFRobot SEN0374 and SEN0253. It does not assume the Gravity module's separate BMP280 exists. Optional INT is not needed for polling.

Windows/web expose an icon-only Orientation sensor tab when selected, with a live magnetic compass, X/Y/Z acceleration (m/s²), gyro (degrees/s), magnetic field (µT), roll/pitch and calibration levels. LCD provides a compact live readout and controls in its dropdown. All use `/api/bno055` cached status and queued commands for sampling, NDOF/IMU modes and reinitialization. No random/simulated readings are shown; stale/offline data is cleared. Compass is magnetic and calibration-dependent. Reinitializing restarts calibration. Current calibration is not persisted across restart.

The main loop performs bounded reads about every 60 ms and nonblocking initialization delays. UI clients poll only while visible. No BNO055 physical module has been tested in this session.

## Primary references

- [DFRobot SEN0374 BNO055 reference](https://wiki.dfrobot.com/sen0374/docs/21868), [DFRobot BNO055 library](https://github.com/DFRobot/DFRobot_BNO055), [Bosch register definitions](https://github.com/boschsensortec/BNO055_driver).
- [Adafruit OLED wiring](https://learn.adafruit.com/monochrome-oled-breakouts/wiring-128x64-oleds), [MPU6050](https://learn.adafruit.com/mpu6050-6-dof-accelerometer-and-gyro), [BNO085 interoperability](https://learn.adafruit.com/adafruit-9-dof-orientation-imu-fusion-breakout-bno085/arduino).
- [Analog Devices DS3231](https://www.analog.com/media/en/technical-documentation/data-sheets/DS3231.pdf).
- [NXP PCF8574/A](https://www.nxp.com/docs/en/data-sheet/PCF8574_PCF8574A.pdf), [PCF8575](https://www.nxp.com/docs/en/data-sheet/PCF8575.pdf), [PCA9685](https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf), [WM8960 address](https://mcuxpresso.nxp.com/api_doc/dev/4594/a00050.html).
- [Microchip MCP23017](https://ww1.microchip.com/downloads/en/devicedoc/20001952c.pdf), [MCP4725](https://ww1.microchip.com/downloads/aemDocuments/documents/MSLD/ProductDocuments/DataSheets/MCP4725-Data-Sheet-20002039E.pdf).
- [TI ADS1015](https://www.ti.com/lit/ds/symlink/ads1015.pdf), [ADS1115 address example](https://www.ti.com/content/dam/videos/external-videos/es-mx/8/3816841626001/6235798782001.mp4/subassets/adcs-introduction-to-i2c-example-presentation.pdf).
- [Everest ES8388 datasheet, vendor copy hosted by Boardcon](https://www.boardcon.com/download/ES8388_datasheet.pdf).

## Validation

- 36 Windows model/GPIO/address-sharing tests pass, including complete-pair allocation, invalid addresses and PCA9685 multicast behavior.
- Six browser I²C policy tests and seven LCD/media regression tests pass.
- Native workspace smoke test passes; the orientation widget was rendered with known test vectors and visually inspected.
- Host C++ tests verify signed BNO055 decoding, unit scales and boundary values.
- Full LCD firmware compile/link checks and web bundling run locally. No device has been flashed for this change, and no physical BNO055, bus pull-ups, concurrent touch/sensor operation or audio performance has been verified.
