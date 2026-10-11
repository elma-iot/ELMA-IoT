# MiniExco v2 integration

Select **MiniExco v2 robot (ESP32-S3-SPK N16R8)** in Windows or Android. This selects board ID 32, 16 MB flash, 8 MB octal PSRAM, and the Robot tab. Other boards retain their existing controls.

The robot is not connected during preparation. Motor direction, servo travel, ADC calibration, physical LED ordering and the installed PSRAM must be verified on the robot before normal driving.

## Wiring from the original project

| Function | GPIOs |
|---|---|
| Right / left / arm DRV8833 motor pairs | 16/17, 18/19, 43/44 |
| Bucket / auxiliary servos | 0 / 20 |
| 12-pixel WS2812 strip | 21 |
| Battery ADC, divider 22 kΩ / 10 kΩ | 13 |
| Charger ADC, divider 10 kΩ / 6.8 kΩ | 1 |
| OLED and BNO055 I2C | SDA 15 / SCL 14 |
| Microphone | WS 40 / SD 38 / SCK 39 |
| Speaker | DIN 9 / BCLK 10 / WS 45 / PA 46 |
| SD SPI | CS 2 / SCK 11 / MOSI 3 / MISO 12 |

Camera wiring matches the existing SPK preset. The OLED, BNO055, battery divider, microphone, speaker and SD profiles are selected automatically. Battery defaults reproduce the original divider ratio × 0.84 correction (2.688 total multiplier); calibrate against a meter in configuration. Charger voltage is informational, not charge control.

## Flash and install

Disconnect motor power during USB flashing: ROM UART traffic on GPIO43/44 can reach the arm driver before firmware takes control. GPIO19/20 also belong to robot peripherals, so native USB CDC and UART0 logging are unavailable after startup. Supply Wi-Fi details in the reviewed compile configuration, or join the robot access point and configure over HTTP/HTTPS. Existing saved settings can take precedence over compiled defaults.

After verified flashing and network discovery, the applications offer an **insert SD card / OK** prompt. Only OK starts upload. Each uploaded file is downloaded and SHA-256 checked before enabling the custom UI. Cancel leaves firmware installed; retry using **Upload robot web files to SD** in Robot controls. The card is never formatted.

Files use `/web/miniexco-v2/`, `/3d/miniexco.glb`, and `/pcm/`. Both `/web/index.html` and its precompressed gzip equivalent point to the robot page. Built-in firmware settings remain at `/system/ui`. Compression is performed on the PC by `node scripts/build_robot_web.mjs`, never on device boot or SD insertion.

## Controls and media

Browser controls provide a touch joystick, WASD drive, R/F arm, Space emergency stop, two servos, light modes, horn, live camera and the existing photo/video/audio overlays. Windows provides hold-to-drive controls with camera preview. Android opens the live robot page within the application. The device address can use HTTP or trusted HTTPS; all device requests and sockets use the same origin, with WSS under HTTPS. Browser microphone communication requires a trusted secure context; see [local HTTPS setup](local-https.md).

A controller sends bounded commands every 50 ms. A dedicated control task checks command expiry independently of the main loop. Commands expire after 350 ms, disconnect clears the owner, and other controllers cannot take over an active lease. Leaving controls, switching tabs, hiding the browser or losing focus stops movement. OTA inhibits motor and servo output. Motor reversals insert a coast interval. Servo PWM uses MCPWM separately from camera LEDC timing. This software stop is not a physical emergency disconnect.

Bluetooth gamepads are supported through the browser Gamepad API: pair with Windows/Android, enable Browser gamepad, and hold the right trigger to drive. The old project's direct-to-ESP Bluepad32 BSP is not included in the standard ELMA firmware build.

Recordings, two-way audio, person/BNO055 security triggers, MQTT, OTA and storage management reuse the existing ELMA services. Select `/pcm/capture.wav` as the capture effect and the uploaded robot GLB in the BNO055 view as desired. Existing custom effects/model selections are preserved.

Original robot project and supplied model/sounds/image: https://github.com/elik745i/miniexco.v2 . The old multi-server code and stored credentials are not copied into this integration.
