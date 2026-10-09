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

## Empty slot boot-loop repair (2026-10-09)

Serial capture of the previous image (`c716c146f3354721`) with an empty Guition SD slot identified a `storage-summary` task stack overflow. `spiCardNeedsFormat` placed a FatFs `FATFS` structure (including the SDK's up-to-4096-byte sector window) on the worker's 4096-byte stack. SDMMC probing and formatting had similar large local buffers.

- FAT filesystem probe objects now use checked heap allocations, preferring PSRAM. The SDMMC formatting workspace is also checked and heap allocated. Objects are unmounted/unregistered before memory is released.
- Unmounted SPI cards first receive a bounded CMD0 response check at 400 kHz. An empty/nonresponding slot skips the seven-frequency mount sequence and the filesystem-format probe. It reports its state once and checks again every five seconds for insertion.
- A nonresponding slot is never classified as an unformatted card. Without a dedicated card-detect signal, empty and electrically nonresponding cards cannot be distinguished.
- Card data, LCD functionality, and audio decoder buffers are unchanged.

## Portrait LCD follow-up (2026-10-09)

- LCD snapshots serialize only the settings section needed by the displayed page. Desktop wiring geometry and recorded melodies are excluded, and non-Logics pages no longer request the full editor graph. Existing full settings API serialization is unchanged.
- The CPU governor holds its performance clock while the interactive LCD is awake; audio retains the same performance priority. Unchanged status indicators avoid redraws, and display-loop callbacks are passed by reference.
- Wi-Fi adds asynchronous scanning, a network selector, explicit Connect, and masked saved credentials with a local show/hide control. Commands use the web settings validation/save path. Credentials are available only to the unlocked local Wi-Fi page. Scan results remain cached for both web and LCD consumers until a new scan starts.
- Hardware Monitor adds live CPU/core, RAM, PSRAM, internal-storage, SD-storage and temperature bars. Unavailable readings remain labelled unavailable; classic-ESP32 temperature remains labelled estimated.
- A mounted, recognized card deliberately does not produce a format prompt. Automatic format confirmation is reserved for a responding card with `FR_NO_FILESYSTEM`.

Validation (2026-10-09): the final Guition firmware build passed and was flashed on COM13 with application-only flashing, preserving settings. Startup reached the web server and portrait dashboard; the captured startup contained no backtrace, stack overflow or SD begin failure. Wi-Fi connected successfully, asynchronous scanning returned eight networks, and a second consumer received the cached results. Four subsequent status samples retained about 51 KB free heap and connected Wi-Fi. The user confirmed that the LCD interface is responsive and controls work. ESP32-S3 SDMMC syntax checking passed; compiler stack analysis reports a 48-byte local frame for the SPI filesystem probe. The current SD state is unmounted; actual formatting and removal/reinsertion were not tested, and no card was erased.
