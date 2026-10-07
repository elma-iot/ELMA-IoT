# SD recovery and LCD startup memory — 2026-10-08

Shared by LCD and headless ESP32 firmware with enabled SD storage. Sunton/Guition LCD boards use SPI; VIEWE LCD boards use their existing four-bit SDMMC pin map.

After a failed mount, a read-only FAT probe distinguishes `FR_NO_FILESYSTEM` from missing cards, communication errors and allocation errors. Only the former offers formatting. Nothing is erased automatically on boot. The LCD and web share one device-owned confirmation state. Cancel dismisses both prompts for this boot. **Erase and format** authorizes one attempt in the storage worker, never in a display or network callback. Both interfaces show indeterminate progress, remove the popup at completion, and report success/failure. Keep power connected while formatting; the LCD stays awake.

Mount/Eject controls run in the storage worker and reject busy cards: stop playback and transfers first. An ejected card stays unavailable until Mount or reboot. Idle hotplug checks issue an actual sector read to avoid mistaking a cached root directory for an inserted card. Unexpected removal marks the filesystem unavailable and schedules retries with backoff. Physical removal during writes can still damage files; use Eject before removing a card.

## Client protocol

- `/api/status.sdFormat`: `prompt`, `mounted`, `ejected`, and `state` (0 idle, 1 pending, 2 formatting, 3 complete, 4 failed).
- Authorized POST `/api/storage/format?action=confirm&erase=yes`: explicit erasure confirmation. `action=cancel` dismisses the boot prompt on both interfaces.
- POST `/api/storage/eject`: queue safe eject. Existing `/api/storage/remount` now queues mounting; its accepted response does not imply mounting has completed. Poll status before accessing files.
- Existing PIN/authentication checks apply. Android should consume this shared state rather than keep its own independent prompt state.

## Reboot diagnosis

The Guition serial log showed a successfully mounted card, followed by an allocation failure registering web middleware. The runtime retained a duplicate Logics device catalog. The fix compacts the executable graph after catalog removal, preserves the catalog in editor responses, and transfers edited graphs by move instead of serialization. ArduinoJson `remove` and `shrinkToFit` alone retain freed slots, so explicit compaction is necessary.

Validation: all four web confirmation/synchronization tests in `tests/sd-format.test.mjs` pass. The Guition 3.5-inch firmware build passes, and the SDMMC backend passes the ESP32-S3 compiler syntax check. Physical startup, actual formatting and removal/reinsertion still require device testing; no user card was formatted during development. Boards without PSRAM use eight LCD draw-buffer rows instead of twenty to leave more internal heap for startup, with unchanged resolution and content.

## Further startup heap reduction (2026-10-08)

The Guition ESP32-3248S035C serial trace still failed allocating a web route after SD mounted successfully. Additional buffers now allocate on first use instead of at boot:

- Pitch correction: 8,204 bytes, allocated before starting a source that needs pitch correction, never in the PCM callback.
- Speech synthesis: 2,152 bytes, allocated only for a speech source.
- Melody synthesis: 5,136 bytes, allocated only for a melody source.
- Plot history: 7,168 bytes, allocated on the first published sample. Empty plot requests no longer allocate a full snapshot; nonempty snapshots allocate only the populated count.

Sizes are measured with the ESP32 compiler. After four replacement pointers, baseline memory savings are 22,644 bytes (about 22.1 KiB), before allocator overhead. Optional allocations prefer PSRAM and preserve the existing internal-memory reserve; failure returns an audio error or drops the plot sample instead of using a null buffer. Plot allocation happens outside the critical section and competing publishers discard redundant allocations.

Radio/SD decoding buffers, sample rates, and audio DSP algorithms are unchanged. Deferred buffers remain available after first use, so these are startup savings, not a reduction of peak memory when all features are active. Serial audio startup reports the deferred DSP bytes. A successful firmware build does not establish that the hardware reboot is fixed; verify a boot reaching `[web] ready` and the LCD dashboard after flashing.

Validation: the full `sunton_3248s035c` build passed on 2026-10-08. Automatic flashing on COM13 was attempted with normal and extended reset timing; both returned normal boot mode `0x13` before any write. This heap candidate has therefore not yet been tested on hardware. Enter the ROM bootloader manually (hold BOOT, tap RESET, release BOOT) and flash from the updated app, then verify startup and Wi-Fi. No SD format or configuration erase was performed.
