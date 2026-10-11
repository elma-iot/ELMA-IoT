MiniExco v2 robot firmware. No physical robot was connected or flashed during preparation.
Disconnect motor power for first flashing: GPIO43/44 are also ROM UART pins.
Select MiniExco v2 in the application and compile with reviewed Wi-Fi/configuration settings for deployment.
This standalone image contains empty project defaults; configure through the robot AP/network.
After connecting to the network, insert SD and approve the robot web-file upload in the app.
Firmware.bin alone is the OTA image. Manifest offsets describe complete USB flashing.
Verify motor directions, servo limits, battery calibration and installed PSRAM before driving.
