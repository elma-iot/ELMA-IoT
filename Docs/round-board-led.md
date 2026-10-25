# VIEWE round-board LED and external GPIO

For UEDX48480021-MD80ET, including the GC9503 and ST7701S display profiles,
the correct default status LED setting is **Disabled** (`-1` in the settings API,
`255` in firmware's unsigned pin field).

The manufacturer's schematic shows D1 connected to 3.3 V through R11. This
power indicator cannot be switched, dimmed or used by a Logics LED node.
GPIO4, labelled ADC-GPIO4 on J1/J2, is an exposed output and can control an
external LED with suitable wiring. It is offered as an explicit choice, not
assigned automatically. Other pins are occupied by the LCD, touch, encoder,
boot, USB or UART. The separate MD50 adapter's physical pad positions have
not been verified, so its illustration must not invent GPIO anchors.

Source: [manufacturer schematic](https://github.com/VIEWESMART/UEDX48480021-MD80ESP32-2.1inch-Touch-Knob-Display/blob/main/Schematic/UEDX48480021-MD80ET-V1.0%20SCH_00.png).

The same pin capability table and default are used by the web interface,
Windows designer and Android designer. Choosing Disabled must remain a valid
selection, rather than being displayed as an invalid GPIO-1.
