# Startup and wired built-in LED values (test.64)

The stored group mode was restored correctly. The apparent startup failure was
an LED action holding its first RGB sample, often black before Wi-Fi settled or
at the beginning of a Breathe effect. Later value changes did not update the
physical LED unless a Set color pulse or Stop/Start was sent.

The active built-in LED now follows wired RGB, brightness, effect and speed
inputs from its last writing node. Unchanged control values do not allocate an
argument document or send another device write. Implicit value refreshes do not
emit Done pulses. A startup On waiting for an unavailable wired value retries
when the source becomes available and completes Done once. Explicit Off,
Pause, group Stop, global Stop and last-writer ownership remain authoritative.

Execution remains in the device scheduler, independent of browser tabs and
LCD/web presentation ownership. The existing three automatic abnormal-reset
retries and subsequent confirmation guard are retained. Normal boots preserve
the saved playing/paused/stopped modes. Small ELMA_LOGICS_STATUS serial
diagnostics inspect execution and LED output without switching presentation or
allocating a full graph/settings snapshot on a constrained LCD board.

Regression checks cover a fresh runtime with initially false or unavailable
Wi-Fi, later state changes without any browser control, unchanged frames,
explicit Off, Stop/Start and initially stopped/paused groups. Existing LED
mixing, Stop/cache cleanup and three-retry recovery checks also pass.
