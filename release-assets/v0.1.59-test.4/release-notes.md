# Firmware 0.1.59-test.4

Adds a twelve-section round system dashboard. The LCD section icons are generated from the web interface SVG icons. Live status, brightness, Logics group controls, and firmware actions use the existing firmware services. Display settings and the native Circular Menu workspace select the system dashboard or a custom menu.

Holding the rotary push continuously for 15 seconds reboots without erasing settings. A separate FreeRTOS task monitors the button; releasing early cancels the hold. Short press and long-press Back retain their existing behavior.

The optional local display PIN is stored separately from the web PIN. Logs reuse the existing bounded firmware RAM ring. Unavailable hardware readings are reported as unavailable.

The physical MD80E unit uses GC9503, two transitions per encoder detent, and has no demonstrated glass touch capability. ST7701S and actual MD80ET touch remain hardware-unverified. Android's dedicated rotary compiler pack is still undergoing integration.
