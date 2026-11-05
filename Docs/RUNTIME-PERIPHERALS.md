# Runtime peripheral configuration

Firmware `0.1.59-test.69` builds its active peripheral bindings from saved device settings. A supported peripheral no longer needs to appear in the project used to compile the image before it can be added through the device web interface.

The image reports its compiled drivers, slot limits, singleton limits and remaining PWM capacity. The web interface filters new selections using those capabilities. Unsupported saved entries remain visible as unavailable. The API also validates availability, GPIO ownership and PWM capacity before saving. Firmware size fitting can exclude audio, display or SD support; an excluded driver cannot be newly selected in that image.

The configuration stores a persistent identity in each peripheral helper binding. Reordering or removing rows preserves remaining identities; a newly compiled project updates imported identities to match its newly compiled graph. Removing a peripheral releases its outputs and pauses graph nodes that refer to it. A paused graph needs its missing references corrected before resuming.

Windows and Android use the same generated driver contracts and board pin metadata. Compact firmware supports its smaller set of portable inputs and controls, with web controls for adding them. Additional drivers require a new image when they were omitted or are unsupported by the board SDK. The Windows and Android interface stays in the installed app; compilation tools and SDK data are delivered on demand.

## ESP32-C3 device verification

The application image was flashed into an existing 4 MB ESP32-C3 factory partition without replacing the partition table, NVS or filesystem. The compiled project had no selected external peripherals. Existing saved settings selected a 32-pixel NeoPixel ring on GPIO3 with Chase.

Passed over the device API:

- The saved ring creates a runtime controller and reports an advancing Chase animation.
- Removing the ring releases the controller; adding it again creates a working controller.
- Unassigned required pins, reserved flash GPIOs, duplicate peripheral GPIOs, an excluded servo and excluded I2S audio are rejected without changing the working configuration.
- Peripheral identities survive reboot.
- A newly added ring can be driven by a saved Logics graph, including after reboot.
- Removing a graph-owned peripheral pauses its graph.
- The original empty graph and Chase configuration were restored after testing.

Host tests exercise the actual shared C++ driver factory and NeoPixel runtime with a recording output shim in full, compact and feature-excluded builds. This verifies frame generation and capability admission. Physical LED illumination was not visually observed, and these checks do not constitute physical testing of every supported board and peripheral combination.
