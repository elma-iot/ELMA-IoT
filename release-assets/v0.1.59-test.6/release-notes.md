Firmware 0.1.59-test.6

Round LCD: centered gauge pages, immediate rotary brightness adjustment, a full circular seconds track, and inline curved Wi-Fi/MQTT status without the device name on the clock. Settings and other board profiles retain their existing behavior.

# 0.1.59-test.6

All twelve LCD section icons fit one ring. A saved Device UTC-offset selection controls the LCD and live Web header clock, with date/time placed before the device name. NTP and Logics schedules retain UTC. The existing Alarm Clock node provides validity, UTC seconds and formatted UTC date/time.

Retains the verified two-transition reversed encoder and continuous 15-second push reboot.


Shared Security: rotary LCD and Web use one PIN, lock state, timeout and retry counter. Existing LCD PINs migrate only when Web has no security record. Web unlock dismisses the LCD lock screen. The locked screen contains one large centered lock icon.

Startup shows matching static artwork first, then an animation started at the first serviced LCD tick. Static and completed animation match at every pixel.

Clock digits occupy fixed cells so changing seconds cannot shift the text. Its outer ring is 460 pixels across with doubled stroke thickness, subsecond movement and contrasting successive minute colors. The previous color remains underneath the sweep, including minute rollover. A locked device keeps displaying the clock; the first knob action shows the lock icon, the next requests the PIN, and ten seconds of inactivity returns to the clock without unlocking.

Conventional full-matrix build output names below are not published in this testing release; only the tested GC9503 board image is uploaded.
- esp32-notifier-v0.1.59-test.6.bin (unpublished)
- esp32-notifier-hacs-v0.1.59-test.6.bin (unpublished)
- esp32-notifier-hacs-slim-v0.1.59-test.6.bin (unpublished)
- esp32-notifier-hacs-legacy-ota-v0.1.59-test.6.bin (unpublished)
- esp32s3-notifier-v0.1.59-test.6.bin (unpublished)
- esp32s3-notifier-hacs-v0.1.59-test.6.bin (unpublished)
- esp32s3-notifier-hacs-slim-v0.1.59-test.6.bin (unpublished)
- esp32c3-notifier-hacs-v0.1.59-test.6.bin (unpublished)
- esp32-ota-bridge-v0.1.59-test.6.bin (unpublished)
- esp32s3-ota-bridge-v0.1.59-test.6.bin (unpublished)
- esp32c3-ota-bridge-v0.1.59-test.6.bin (unpublished)
