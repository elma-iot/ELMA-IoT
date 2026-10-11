# Local mDNS and HTTPS

The existing ESPAsyncWebServer on HTTP port 80 remains the fallback and owns
all authentication, UI, storage, audio, WebSocket and OTA routes. An optional
mbedTLS listener terminates TLS and streams bytes to that server over the
device's loopback interface. It does not load a second UI, decompress gzip,
buffer whole requests, or send audio through a cloud service.

The Configuration page has a separate mDNS/HTTPS section. Both features default
to off. Leave hostname empty to use `deviceName.local`, or enter a unique local
label such as `elma-bedroom`. The TLS certificate must cover the chosen name.
HTTP at `http://elma-bedroom.local` is not localhost and cannot provide a secure
browser microphone context.

## Certificates and trust

The Windows application's Wi-Fi/network settings can generate an offline local
CA and an ECDSA P-256 server certificate, then provision the server certificate,
private key and HTTPS settings over USB. USB provisioning preserves the other
board configuration. Keep the CA private key on the PC, backed up privately;
never upload it to the board or distribute it with the app. Certificates last
one year; reuse the same CA when renewing them. Optional IP SANs allow verified
HTTPS at a fixed IP too. Regenerate the certificate if its name or IP changes.

Install **only the public CA certificate** on each client you own:

- Windows: import it into Trusted Root Certification Authorities for the
  current user, then restart Chrome. Verify the CA fingerprint first.
- Android: use Settings → Security/privacy → More security settings →
  Encryption & credentials → Install a certificate → CA certificate. Menu names
  vary by manufacturer. Android may require a screen lock. Restart Chrome.
- The Android application trusts system and user-installed CAs. It requests
  microphone permission only for the current HTTPS device origin and also
  requires Android's microphone permission. It never bypasses TLS errors.

Certificate installation is manual: neither application silently changes the
OS trust store. A warning-page exception is not a substitute for a trusted
certificate when enabling browser microphones. No certificate authority or
cloud service must be contacted during operation.

The Configuration page also accepts PEM server certificate/key files. Prefer
USB provisioning; sending a private key through HTTP exposes it to anyone who
can observe that network. The imported pair must match and fit the 8 KiB
certificate / 4 KiB key limits. The pair is stored together in NVS, independently
of OTA application partitions. It is not exported with settings, written to SD,
or returned by the diagnostics API. Standard NVS is **not encrypted at rest**;
physical flash access can recover a device key unless flash/NVS encryption is
configured separately.

## Resources and diagnostics

The SPK 16 MiB build uses about 2.60 MiB of a 4 MiB OTA slot. Its existing dual
4 MiB OTA partition layout remains unchanged. TLS uses internal RAM with the
current SDK configuration. The listener installs a task-scoped allocator that
moves its allocations of at least 1 KiB to PSRAM when available; outbound TLS
clients retain the SDK policy. Task stacks and networking still need internal RAM.
The listener allows three sessions with PSRAM, or one without it, and serializes
handshakes. PSRAM boards require 48 KiB free internal RAM and a 16 KiB contiguous
block; other boards require 96 KiB free and a 32 KiB contiguous block. It
limits each handshake to eight seconds. Each connection uses two 2 KiB staging
buffers and an 8 KiB task stack, plus mbedTLS allocations. TLS 1.2 session tickets
last one hour, live only in RAM and rotate when the server certificate changes.
Resumption avoids a full public-key handshake for each new media request.
Performance depends
on camera, audio, SD, Wi-Fi and other simultaneous allocations.

`GET /api/web-access` reports the resolved hostname, HTTPS listener state,
certificate fingerprint, internal free RAM/largest block, session limit,
handshake duration/counters and OTA pause state. When resources are busy,
new connections wait in the TCP backlog; HTTP/IP remains available. A failed
connection counter includes abnormal client disconnects, not only bad TLS
handshakes. Chunked OTA may resume or open TLS connections within these limits
while OTA is busy. Credentials remain after an ordinary OTA update.

All browser assets/API/media URLs must stay relative to the page origin.
WebSocket URLs select `wss:` when the page uses HTTPS. Gzip is passed through
unchanged, with its original MIME type and encoded length. An HTTP URL returned
for internal speaker playback is consumed by the board, not fetched as browser
mixed content.

## Validation limits

Host tests validate asset negotiation, bounded streaming, certificates and
browser rendering. Physical testing must additionally confirm trusted HTTPS,
WSS upgrades, microphone permission, camera/audio concurrency, memory headroom,
SD removal and OTA behavior on the target board. An APK build alone does not
verify microphone permission on a real Android phone. Keep HTTP enabled while
testing and use `/system/ui` for built-in recovery from a broken custom SD UI.

The connected SPK was tested with certificate-verified HTTPS OTA, WSS PCM,
fragmented speaker frames and SD WAV recording. Chrome rendering and microphone
permission initially used an isolated SPKI allowlist; after installing the public
CA with user approval, Chrome passed again with normal certificate validation
and synthetic microphone input. Each client still needs the public CA installed.
A 2.2-second 32 kHz mono recording contained about 146 KiB. Physical SD gzip-only
HTML/CSS/JS passed over HTTP and HTTPS, including HEAD, lengths, original MIME,
ETags, 406 without gzip and Chrome rendering. UXGA capture returned 1600×1200.
The Android APK has build/unit/lint validation, but no connected Android phone
was available. Physical SD removal remains a hardware check. The connected
BNO055 was configured with the user's wiring (SDA GPIO15, SCL GPIO14), detected
at 0x28 and returned live NDOF quaternion/Euler readings. Its original failure
included an unconfigured profile and a JSON proxy retaining a temporary binding
key; validation and driver lookups now resolve that key to a JsonVariantConst.

Windows device access falls back to scoped mDNS discovery when Windows cannot
resolve `.local`, retaining the original hostname for TLS verification. Browsers
may use the fixed IP included in the certificate SAN if local discovery fails.
Certificates are stored as a compact DER leaf/key pair in NVS; use certificates
signed directly by the local CA. CA private keys remain on the PC.
Legacy 4 MiB boards may not fit this full firmware in their smaller OTA slots;
the verified SPK has two 4 MiB application slots in its 16 MiB flash.

Camera capture follows the [Espressif camera driver guidance](https://github.com/espressif/esp32-camera):
JPEG, PSRAM frame buffers, two buffers and latest-frame capture. The current
Arduino 2.0.17 SDK does not expose the newer runtime PSRAM-DMA switch described
upstream; enabling that requires a separately validated SDK migration. More
CPU/PSRAM does not remove the sensor, Wi-Fi and SD throughput limits. AVI uses
MJPEG plus native PCM instead of encoding a compressed audio/video codec.

Trusted Windows Chrome also verified GLB file selection, upload to SD `/3d`,
download, model selection and persisted axis adjustments with the live BNO055.
The test restored the default cube and zero alignment afterward. Windows opens
the device browser for live media and orientation; Android uses its WebView.
Android APK builds, unit tests and lint pass, but physical Android microphone
and certificate installation still require testing on a connected phone.
