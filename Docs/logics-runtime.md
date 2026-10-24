# Logics runtime and live editor

Windows compilation embeds the graph, its node positions/parameters, supported
hardware definitions, group membership/rectangles and initial control modes.
The device Logics tab recreates this graph and provides node editing, typed links,
inline defaults, searchable palette, undo/redo, grouping, edge/corner resize,
frame dragging, live scalar values and event/action animation. Save & apply validates
the entire graph and stores it atomically in internal flash before activating it.
Changes to physical peripherals/pins require recompilation; browser editing cannot
invent GPIO bindings or enable an uncompiled driver.

The configured built-in LED is exposed under Hardware even when an empty factory
project has no peripheral catalog. On ESP32-3248S035C it is the active-low RGB LED
on GPIO4/16/17, with color and brightness inputs; automatic network indication
remains disabled. Windows calls this node Built-in LED; the web and Android
palettes identify the fitted RGB as Built-in RGB LED.

The restart guard stops nonempty automations after three failed automatic retries.
An empty graph consumes no retries and clears a stale guard without starting any
automation or changing its stopped/paused mode.

For the full ESP32 firmware, LED controls provides separate LED Brightness
(0-100%) and LED Effect nodes. Connect Effect and Speed outputs to the built-in
LED's Effect and Effect speed inputs. Solid, Blink, Breathe and Color loop run
cooperatively with no extra task or frame buffer. An ON or Set color pulse applies
the inputs; use a change event for updates after On started. RGB channels use
0-255, while Boolean to Number produces 0 or 1. Condition branches can select
separate LED nodes with explicit colors for different Wi-Fi states.

Context-menu search includes canvas actions such as Group when nodes are selected.
The popup repositions when expanded and stays inside the viewport with scrolling
when needed.

Each group has Play, Pause/Resume and Stop. Master controls affect all automations.
Pause freezes scheduled execution while monitoring continues; already accepted
hardware operations can finish. Stop cancels scheduled work and stops audio owned
by that automation. A shared DAC plays one source at a time; independent groups do
not create independent DAC outputs. Play after Stop rearms On Start. Repeat's count
is the total number of pulses, with the first immediate and later pulses separated
by its interval. Play.Out reports acceptance, not the end of playback.

Graph and control modes survive reboot/browser reload. Pending timer progress is
not checkpointed across reboot; an automation that is playing starts afresh.
A changed compiled graph invalidates an old flash override using a source hash.
Storage/validation failures leave the existing automation untouched. Editing is
rejected during OTA. The illustrated editor fits the normal 4 MB OTA partition
profiles; legacy 0x190000 OTA compatibility uses the existing recovery frontend,
while retaining the compiled graph and persistent control APIs; live graph editing is excluded there.

Runtime-backed peripherals currently include primary battery voltage, digital
inputs, joystick/potentiometer readings, relay outputs, primary I2S DAC audio,
file paths from configured storage and active/passive buzzer melodies. Secondary
DAC channels and catalogue-only device helpers need dedicated drivers and are
explicitly rejected rather than silently treated as relays or sensors. Offline TTS
supports printable basic English (256 characters). Piano allows 128 notes / 60 s;
DAC permits chords, passive buzzer is monophonic, active buzzer has fixed pitch.
Audio-disabled builds exclude DAC-only effects/speech from the device palette.

### Builder graph and device canvas

Windows compilation retains `windowsLogic` and stable peripheral IDs until backend validation/lowering creates `compiledLogic`. Firmware defaults then embed that program separately from settings, including node parameters, connector contracts, positions and automation frame bounds/modes. A nonempty editor graph without a lowered program is rejected.

The live editor uses palette dropdowns above the full-width canvas. Right-click provides Add Node, grouping, properties, Save & apply, Fit all and undo/redo, plus all-automation controls. Automation frames retain compact Play/Pause/Resume/Stop controls. Ctrl+S saves. Nodes use the Windows editor dimensions and port spacing; the initial view fits nodes and frames. Older firmware compiled with an empty graph must be recompiled from its saved Windows project.
