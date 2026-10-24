# Logics Stop, LED speed and memory handling (test.63)

Stopping an automation now blocks value evaluation as well as execution pulses.
The scheduler skips its GPIO/peripheral sampling and, when no nodes are running,
does not allocate the application/status snapshot at all. Stop clears cached
values, edge history and pending pulses. The saved graph and small scheduler
records remain available so it can be edited and started again.

Outputs track their last writing node. Stopping a group releases only outputs
owned by that group: built-in LED effects, GPIO/PWM, LED arrays and portable
outputs. Audio owned by Logics is stopped through the existing audio action;
playback started outside Logics and other running groups remain independent.
Stop takes effect before persistence, so an allocation/storage failure cannot
leave the stopped chain executing. Recovery attribution is cleared for that
group, rather than blaming a later unrelated crash on a stopped automation.

All JSON responses on ESP32 boards without PSRAM use 512-byte segments, with a
bounded TCP window and acknowledged send handling. They no longer require one
large contiguous allocation. The shared Logics browser request path serializes
writes, suppresses polling while writes are pending, reconciles controls that
already applied, and retries busy saves only when firmware advertises request
deduplication. A bounded client/sequence table prevents duplicate action replay
and stale writes. Test/Toggle actions are never retried automatically.

Host checks cover stopped evaluation/action cleanup, an independent running
group, restart, LED color/effect mixing, retry/reconciliation/write ordering,
sequence conflicts/stale requests, segmented allocation failures, and actual
HTTP partial writes/ACKs/HEAD requests. Device verification results are recorded
separately after flashing.

LED speed accepts 0–1000 throughout graph validation, linked controls, device
drivers and the web/Windows/Android editors. Values 0–100 retain their old
timings. Above 100, the interval is divided by speed/100: 1000 gives ten times
the speed of 100. Brightness remains 0–100 and RGB channels remain 0–255.
Rendering stays capped at 50 frames per second. Compact ESP8266/C2 profiles
use the same effect calculation without allocating a JSON document per frame.
Their Stop path also clears owned LED/GPIO/array/portable outputs and skips
background status snapshots when no automation nodes are running.

Android general, Sunton and rotary firmware templates share compatible runtime,
storage and web changes. Rotary display SDKs and audio/SD capability exclusions
remain specific to their board profiles. Compiler plans include all application
objects and the ESPmDNS library dependency. Display/DAC/pin fixes remain scoped
to the hardware they describe; they must not be applied to unrelated boards.
