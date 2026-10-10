# 0.1.59-test.5

All twelve LCD section icons fit one ring. A saved Device UTC-offset selection controls the LCD and live Web header clock, with date/time placed before the device name. NTP and Logics schedules retain UTC. The existing Alarm Clock node provides validity, UTC seconds and formatted UTC date/time.

Retains the verified two-transition reversed encoder and continuous 15-second push reboot.

GC9503 production build passed (67.2% of the 4 MiB application slot). Wi-Fi OTA booted 0.1.59-test.5; device UTC matched current time, UTC+03:00 persisted after reboot, and invalid offsets were rejected without altering settings. The Web clock appears to the left of the device name. Device logs contain no crash, PSRAM, or filesystem fault signatures. ST7701S and touch hardware are unverified.
