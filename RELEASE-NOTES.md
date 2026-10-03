# Firmware 0.1.57 — 2026-10-03

Compact provisioning stages graph data in flash and releases temporary JSON before validation. Runtime parsing excludes visual layout fields; editor responses stream saved graphs and device definitions. Failed validation preserves the previous configuration. Busy requests return JSON errors.

NeoPixel refresh and release run from the main loop, avoiding ESP8266 network-callback yield panics. The GPIO13 32-pixel rainbow ring at 20% brightness was confirmed by the user with the 17-node sampling graph.

See the Windows CHANGELOG.md and WINDOWS-ANDROID-PARITY.md for the full conversation feature audit, target limitations and Android port requirements.

- LED effects update: ten documented patterns, deterministic 25 ms scheduling, matching Windows/web animated previews, and editable device array controls. Compact HTTP settings are queued for main-loop application with completion polling.
