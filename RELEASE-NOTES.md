# Firmware 0.1.58 � 2026-10-04

# Unreleased — WLED workspace and full web parity, 2026-10-04

- LED restart cleanup now sends a black frame to every configured array, including paused/stopped Logics, before orderly software/OTA/web/MQTT/recovery restarts. Audio targets wait for the LED worker to transmit the final frame; discrete status LEDs release their output/PWM drive. Arrays clear latched colors at initialization before boot indication after abrupt resets. Hardware watchdog/panic/power loss cannot guarantee pre-reset cleanup.


Embedded web bundles now carry a source fingerprint. Portable builds rebuild outdated UI assets, and Windows packaging validates freshness instead of silently shipping old controls.

- Full and compact firmware expose the WLED workspace for existing LED functionality. Full firmware includes array layout/count, effect, brightness, RGB and speed controls with automatic settings saves.
- The full wiring diagram renders the configured strip, ring or panel directly as an animated SVG. Connected animation uses runtime LED state/phase through `/api/status`; it does not create a second independent preview or trigger a Logics action.
- Full status includes compiled effect choices and configured output length limits. Logics Effect uses a dropdown filtered by the authoritative peripheral specification.
- Board pin labels share the exported SVG pad positions used by wires. Automatic labels stay centered when the board is near a viewport edge; manually placed labels are preserved.
- LED effect selection is a generated compile-time mask. Existing projects retain all ten effects; disabling an effect used by a default or graph is rejected. No upstream WLED source was imported; ELMA remains MIT.
- Added LED control vocabulary in all 20 supported UI languages. Near-full WLED features remain separate pending work; these changes do not claim RGBW, segments, palettes, presets/playlists, 2D or audio-reactive support.
- Nine targeted JavaScript checks and browser checks of 32-pixel ring rendering and the Logics selector passed. Firmware build matrix validation is recorded in the Windows integration report; device installation is a separate step.

# Unreleased — Logics tab memory fix, 2026-10-04

- Reproduced ESP8266 `Unhandled C++ exception: OOM` while independent status and Logics requests overlapped. The reboot reran On Start, replacing the live ring effect with its graph startup effect.
- Compact Configuration/status and Logics now use one API queue, held until the complete response is consumed. A failed request releases the queue so later reads and writes can proceed. Opening the editor remains read-only and does not restart the runtime.
- Firmware also tracks JSON response lifetime and rejects overlapping status/settings/Logics reads with HTTP 503 rather than allocating multiple response trees.
- Regression covers overlapping requests, body-read completion and recovery after HTTP failure. Built and flashed ESP8266. Final API verification loaded 17 nodes and polled live Logics twelve times: uptime advanced from 36.278 to 45.586 seconds, Chase remained active and phase advanced from 899 to 10210 ms. Twenty concurrent read attempts returned ten valid responses and ten HTTP 503 responses without reboot. Final browser retest was limited by its post-flash timeout page.

# Unreleased — live LED scheme corrections, 2026-10-03

- Compact device Configuration places the wiring scheme under the board name. The array inside that scheme is the only animated preview.
- Array selections apply automatically after a short debounce; numeric edits commit on leaving the field. Changes are serialized and the UI waits for firmware validation/persistence. The separate array apply button is removed.
- The scheme follows device-reported effect, RGB, brightness, speed, on/off and phase, including Logics overrides. Browser animation uses elapsed time, not frame count. This is runtime telemetry, not optical feedback from the LEDs; physical orientation and display colour reproduction can differ.
- Preserve peripheral identity in compact runtime nodes so live configuration rebinds running actions. Deduplicate array definitions during updates. Advance firmware effect phase using wall time, guard against a newly started frame timestamp, and freeze phase while paused.
- Verified on the GPIO13 32-pixel Wemos ring: browser effect selection and brightness changes confirmed in running telemetry, original solid/20% defaults restored, single scheme/no array apply button. ESP8266 and ESP32-S2 builds, compact runtime rebinding regression, Delay regression and six JavaScript checks pass. These source/device changes are newer than the published Windows 0.1.75 executable.

# Firmware 0.1.57 — 2026-10-03

Compact provisioning stages graph data in flash and releases temporary JSON before validation. Runtime parsing excludes visual layout fields; editor responses stream saved graphs and device definitions. Failed validation preserves the previous configuration. Busy requests return JSON errors.

NeoPixel refresh and release run from the main loop, avoiding ESP8266 network-callback yield panics. The GPIO13 32-pixel rainbow ring at 20% brightness was confirmed by the user with the 17-node sampling graph.

See the Windows CHANGELOG.md and WINDOWS-ANDROID-PARITY.md for the full conversation feature audit, target limitations and Android port requirements.

- LED effects update: ten documented patterns, deterministic 25 ms scheduling, matching Windows/web animated previews, and editable device array controls. Compact HTTP settings are queued for main-loop application with completion polling.

### LED arrays and audio visuals — 2026-10-04

- Configuration now uses compact inline Array / PIXEL / Effect rows. Brightness, RGB and speed remain in WLED; both edit the same saved values. WLED is an icon with a tooltip and appears only for configured WS-family arrays.
- One GPIO can drive up to eight chained arrays with independent geometry, count and defaults. The total count must fit the target output limit. The diagram draws each member and its DOUT-to-DIN link. Sync defaults to off; enabling it uses Array 1 settings without deleting the independent settings.
- Boot indication for configured arrays: blue breathing in AP mode at 20% for five seconds, or solid green at 20% for 1.5 seconds after STA connection, then return to the stored/running effects. This indication does not require a Logics LED action.
- Added stream and MIC spectrum, VU and pulse visual modes. Availability depends on configured audio peripherals and compiled modules. MIC capture currently supports standard I2S microphones (generic I2S, INMP441 and SPH0645/ICS43434); PDM and analog microphone capture are not implemented. Single-I2S targets cannot capture MIC and play output simultaneously.
- Playback copies PCM into a bounded lock-free mailbox without modifying it. Spectrum analysis and ESP32 LED transmission run in low-priority tasks; a full visual queue drops frames, never waits in playback. Physical audio quality and microphone wiring still require hardware acceptance tests.
- Chain and boot behavior are covered by a host test against the production renderer. Synthetic audio tests cover a 1 kHz spectrum, silence timeout and full-mailbox dropping. Browser tests verify independent effects, sync/unsync restoration and persistence through reload. The native mirror check also passes.
