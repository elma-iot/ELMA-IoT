# SD recovery and LCD startup memory вЂ” 2026-10-08

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

## LCD setup overlays and gesture follow-up (2026-10-09)

Wi-Fi scanning now opens a modal with a busy indicator and selectable network list, then editable credentials, connection status and an animated success check. MQTT uses a separate credentials/status modal. Both offer a close button; saved settings use the existing settings handler. Other text/number fields remain on their pages with a bottom keyboard that opens on focus/tap and dismisses on a tap elsewhere.

Hardware bars have explicit opaque tracks and indicators, independent of theme defaults. Info includes firmware/build, board/chip, cores/clock, flash/firmware size, heap, uptime, IP/MAC and panel/touch information. Touch polling is 10 ms, LVGL refresh targets 20 ms, and dashboard service no longer waits on the periodic runtime-state block. Vertical momentum scrolling avoids elastic overscroll and defers periodic snapshots while swiping (bounded to three seconds); widget shadows are removed. These intervals are scheduler targets, not a claim of measured 50 FPS. Sunton LCD SPI now tests 80/40/20/10 MHz using pixel readback, after validating readback at a conservative speed. It stays at 20 MHz when readback is unavailable. Periodic pixel checks lower the rate and request a repaint on a mismatch. Info shows the selected LCD SPI clock.

SD SPI uses verified sector reads and a retained runtime fallback ceiling. The pinned Arduino 2.x driver caps SD SPI at 25 MHz, so SD uses 25/20/10/4/1/0.4 MHz rather than claiming a requested 80 MHz is the actual card clock. Health checks wait for active readers/writers; recovery does not interrupt playback to remount. SDMMC remains a separate native bus.

Validation: final sunton_3248s035c build passed (143.85 s), application image flashed and hash verified on COM13. Boot reached the portrait dashboard and connected Wi-Fi at 192.168.1.180. This physical panel did not provide a matching low-speed pixel readback, so it selected the conservative 20 MHz clock; 80 MHz was not validated on this device. Runtime status returned about 45 KB free heap during initial operation. SD remained unmounted, so high-speed card reads/fallback were not physically exercised. SDMMC syntax and storage stack checks passed. New overlay appearance and gesture smoothness require the user's screen check.

## Mounted-card LCD scan panic and snapshot audit (2026-10-09)

The new capture with the card inserted decoded to `lv_obj_add_style` -> `lv_btn_create` -> `PanelDashboard::wifiDialog`, before the scan command ran. The same boot also reported `AsyncTCP ... failed to start task`; a station IP did not imply a functioning HTTP listener.

Shared LCD changes:
- Delay full LVGL construction until network services have initialized. Release the unused monochrome splash canvas when entering LVGL (19,200 bytes at 320x480; 9,600 bytes at 240x320), retaining text-mode fallback.
- Use the basic LVGL theme, paginate settings into four controls, release the underlying page while a setup overlay is active, and limit each network-list page to six entries. Center the scan spinner and delete it with the scanning view.
- Give LCD JSON a separate bounded budget instead of the storage allocator's 32 KB reserve. Retain a previous complete snapshot on allocation failure and never replace a field with a missing JSON value. Snapshot diagnostics report credential presence, never passwords.
- Cache scan results independently of the receiving JSON document, so a low-memory LCD cannot turn a successful scan into a shared empty result list.
- Run LCD directory listings through the storage worker; maintain/eject/remount already use that worker and wait for active readers/writers. Cards with valid filesystems do not trigger formatting.
- Check the actual web listener state and retry failed startup without claiming the web server is ready.

Validation checkpoint (2026-10-09): the final Guition build passed and its application was flashed and hash verified. All three Sunton variants passed shared-source syntax checks; the S3 SDMMC syntax check and seven panel-settings/SD-format JavaScript tests passed. These are not full builds or hardware tests of every board.

**Unresolved; investigation stopped at the user's request:** with the SD inserted, the card mounts at 25 MHz and Wi-Fi associates at 192.168.1.180, but AsyncTCP still cannot allocate its task. The portrait dashboard renders, then reports deferred snapshots; opening Audio produced a C++ allocation abort in `PanelDashboard::syncMenu`. Hardware bars, scan overlays, HTTP reachability and physical SD hot-plug/eject remain unverified on this candidate. Do not describe this checkpoint as a working LCD firmware release. No SD formatting was performed.

Next investigation: capture actual byte-addressable internal heap and largest block (the generic free-heap number is insufficient), inspect audio/DMA and network task allocations, then re-test scan with/without SD, saved credentials, monitor bars, and physical removal/reinsertion. Local diagnostic logs are excluded from Git because they may contain device/network details.

## LCD styling regression (2026-10-10)

The basic-theme switch removed borders/padding and left dropdown popup text inheriting incompatible colours. `panel_theme.cpp` now adds reusable dark frames, input/button/keyboard styles, focus/disabled states, and explicit dropdown-list selection colours on top of the basic theme. All LCD profiles use it. The tab selector no longer overrides its text with the dark background colour. Host LVGL renders at 240Г—320 and 320Г—480 verify frames and contrast (at least 8.3:1 for tested text). This appearance fix does not resolve the startup-memory issues above and was not flashed during this task.

## VIEWE S3 reader audit (2026-10-10)

The connected VIEWE 3.5-inch S3 reported `sd.enabled=false`, so the storage worker could not detect insertion. Its Windows catalog and firmware now enforce the built-in four-bit reader for both VIEWE sizes. Updating only SD settings on the running board mounted a 31,914,983,424-byte card with a valid filesystem. Software eject/remount also passed; no formatting was performed. This is a different board from the no-PSRAM Guition checkpoint above.

SDMMC uses a five-second retry deadline, checked by the two-second storage poll, rather than backing off to a minute. A probe logs its FatFs result, and removal clears a dismissed format prompt. The LCD now shows the SD icon in portrait mode and labels the current Wi-Fi IP. During the user-assisted removal/reinsertion, the status changed to unmounted and then back to mounted at the same capacity. A genuinely unformatted card remains a separate hardware validation case.

VIEWE 3.5-inch S3 validation: the full selected-board build passed (2,536,912-byte application), OTA upload completed, and the device returned at its saved Wi-Fi address. Serial reported the 320×480 panel with 8 MB PSRAM and successful LVGL drawing. SD mounted automatically; the observed startup contained only the expected software restart and no panic. Firmware binary SHA-256: `783a4bb458845caa0146b16b7513c6d482e430acbc8ba2afb16df2a6c79b6995`. The private device-specific binary is not a distributable release asset.

## 2026-10-10: manual format, brightness and web styling

Manual formatting is shared by LCD and web through device-owned prompt state. FAT32 is the supported format. A positive card presence/mount result, idle storage and explicit erase confirmation are required. The background worker unmounts, creates FAT32, remounts and reports completion/failure. Activity overlays disappear after completion; no invented percentage is shown.

COM11 reproduced a loopTask stack-canary panic on a brightness save. The decoded stack passed through processDeferredActions -> SettingsManager::load/defaults/updateFromJson/sanitize. Deferred apply now uses its already-validated settings; display-only saves avoid restarting unrelated services and retain LVGL for brightness/idle changes.

The storage dark-mode regression also affected other descendant pseudo-class rules: asset_embed.py stripped the space in `:root[...] :is(...)`. The minifier now retains that space and has regression coverage.


### 0.1.59-test.1 device check

VIEWE board 25 booted with SD mounted. Four brightness changes completed without the prior loopTask stack-canary panic. Format confirmation and cancellation were checked through HTTP and the browser without erasing the card. The device discovered its exact public release from `elma-iot/ELMA-IoT-Firmware`; public download SHA-256 was verified. Older images with credentials supplied only as compiled defaults require exporting/reapplying configuration when moving to this clean image; this device was restored over USB and reconnected. Actual erase/remount and physical brightness/animation appearance remain user acceptance checks.
