# Local camera integration

Camera settings offer an optional, disabled-by-default MJPEG stream at
`/api/camera/stream`, with JPEG still images at `/api/camera/frame`.
Add Home Assistant's **MJPEG IP Camera** integration using the URLs shown in
Camera settings. Both HTTP and HTTPS use the existing access policy; trust the
local CA on the Home Assistant host for HTTPS. The stream ends when device PIN
protection locks access. One external client is allowed, at 1–10 FPS (default 5).
Frames reuse PSRAM storage and slow clients receive bounded output without a
frame queue. An active external stream keeps the camera awake across tab changes;
closing its connection allows the camera to suspend after the normal idle delay.
The stream carries JPEG video only. Existing two-way audio uses HTTPS/WSS.

RTSP and ONVIF are not implemented. MJPEG avoids a second RTSP/RTP server and
works directly with Home Assistant:
https://www.home-assistant.io/integrations/mjpeg/

## PTZ Logic node

Selecting a DRV8833 dual-motor driver adds **Camera PTZ (timed pan/tilt)** to the
Windows and Android Logic palette. Enable this node only after wiring camera
pan/tilt motors. Assign distinct channels 0 and 1, direction -1/0/1, optional
reverse flags and configured limit inputs (-1 disables the limit input).
Move runs for 100–2000 ms; Stop stops both assigned channels. Firmware checks the
active driver identity, channel wiring, direction, duration and device lock.
This is timed relative movement, not an absolute angle or optical zoom.
Hardware feedback, other driver adapters and ONVIF PTZ are future work.
