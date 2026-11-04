# Waveshare ESP32-S3 Touch AMOLED 1.91

Local testing integration, 2026-10-27. Hardware has not been tested: no board is available.

The selected device is the **ESP32-S3-(Touch-)AMOLED-1.91**, including the header-fitted `-M` variant. It is different from the ESP32-S3-Touch-LCD-1.9 in the original supplied illustration. Do not use that LCD board's pinout or firmware.

Source: [Waveshare examples](https://github.com/waveshareteam/ESP32-S3-AMOLED-1.91/tree/1986f380a4e668da5f4e2b93de9bd82555788018), specifically `02_Example/Arduino/03_LVGL_V8_Test`, `02_I2C_QMI8658`, `04_SD_Card`, and the [schematic](https://files.waveshare.com/wiki/ESP32-S3-AMOLED-1.91/ESP32-S3-AMOLED-1.91.pdf).

## Hardware contract

| Function | Configuration |
| --- | --- |
| MCU / memory | ESP32-S3R8, 16 MiB QIO flash, 8 MiB OPI PSRAM |
| Display | SH8601, 536 × 240, QSPI: CLK47, CS6, D0=18, D1=7, D2=48, D3=5, reset17 |
| Brightness | SH8601 command 0x51; no PWM backlight GPIO |
| Optional touch | FT3168, address 0x38, SDA40 / SCL39; vendor coordinate mapping |
| IMU | QMI8658, address 0x6B (0x6A fallback after WHO_AM_I), SDA40 / SCL39, INT45/46 |
| Motion samples | Acceleration in g, gyroscope in deg/s; unavailable/stale values are not reported as zero |
| Battery | GPIO1, 100k / 100k divider, nominal ×2 scale; charging uses the generic voltage-trend estimate, not a hardware charging GPIO |
| SD, current vendor revision | 1-bit SDMMC, CLK9 / CMD42 / D0=8 |
| USB | Native USB on GPIO19/20, CDC enabled |

The vendor's older schematic/pinout shows **SPI SD with CLK47, MOSI42, MISO8, CS9**. Its CLK47 is shared with the QSPI display. This integration follows the current repository's `VersionControl_V2` SDMMC example; it does not guess that the older SPI wiring is interchangeable. Verify the PCB revision before using SD on physical hardware. Failed mounts never autoformat a card.

Onboard touch and IMU share I2C controller 0 with a bounded lock and timeout. External OLED/RTC/BNO055 drivers retain controller 1. Display transfers, including boot and OTA graphics, respect SH8601's two-pixel alignment.

## Designer and artwork

Board ID: `esp32-s3-amoled-1.91`; compiled numeric ID: `33`. Windows and Android use the same generated defaults, safe GPIO catalog and physical contacts as the device web interface.

`web/esp32-s3-amoled-1.91.svg` is a rear-view vector board with 40 labelled header tabs. Each tab has a `data-labelpin` group, a `connectorNpin` contact and `data-pin` metadata. All contact centres and label baselines follow the same 28-unit pitch. The exported circuit anchors are the pad centres, not inferred text bounds. GPIO26 and GPIO33–37 appear as physical header markings but are unavailable because of flash/OPI PSRAM wiring. USB, display, touch, IMU and battery pins are reserved by default.

Hardware Monitor shows live IMU samples. The Mainboard / Onboard IMU Logics sources expose X/Y/Z acceleration and gyroscope values with a validity gate, suitable for Plotter or conditions. No magnetometer or fused absolute heading is claimed.

## Builds and flashing

PlatformIO environment: `waveshare_amoled_191`, isolated Arduino 3.1.3 / IDF 5.3.2 / GCC 13.2.0. Existing board environments retain their SDK selection. SH8601/QSPI sources are compiled only when their driver flags are enabled.

```powershell
$env:ELMA_PORTABLE_BUILDER='1'
$env:ELMA_PROJECT_DEFAULTS_JSON='{}'
pio run -e waveshare_amoled_191
```

USB installation uses bootloader at `0x0`, partition table at `0x8000`, OTA reset data at `0xE000`, application at `0x10000`. The 16 MiB partition table has two 4 MiB OTA application slots. OTA sends only the application image, with existing chip/capacity/authorization and post-reboot configuration checks retained. No physical USB or OTA success is asserted by a compile-only check.

Android has a separate `esp32s3_amoled` compiler pack with a captured native-object plan and the same pinned modern SDK as the rotary pack. Application sources remain editable; compiler defaults are empty. Windows resolves this board to the pinned modern SDK dependency family.

This board has no onboard audio output. The modern SDK profile currently supports GPIO buzzer playback, but disables DAC/I2S audio and microphone capture because the existing pinned legacy decoder is incompatible with GCC 13. The selectors and compilation preflight expose this limitation. Other board audio profiles are unchanged.

Required hardware checks remain: panel orientation/color order, touch corners under rotation, non-touch variant behavior, IMU axes/scales, SD PCB revision and mount, battery calibration, power/sleep/wake, and complete USB/OTA provisioning with reboot and configuration verification.
