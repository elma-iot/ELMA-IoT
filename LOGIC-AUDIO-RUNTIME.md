# Logic audio runtime

The Windows graph editor can resolve an audio source for explicit preview. Saved Blueprint graphs are not generated/executed by firmware yet. Android has not been modified.

## Source endpoint

Authenticated `POST /api/audio/source` accepts one of `path` or `text` and queues playback on the main task. It returns HTTP 202 (`{"queued":true}`); subsequent storage/decoder errors appear in normal device status. There is a bounded two-entry queue. OTA transfers reject requests and cancel ongoing speech via the existing audio resource release path.

```json
{
  "path": "sd:/media/low-battery.wav",
  "volume": 55,
  "speed": 1.0,
  "pitch": -2,
  "equalizer": {"lowDb": 2, "presenceDb": 0, "highDb": -1}
}
```

```json
{"text":"Battery voltage low. 11.8","language":"en","volume":60}
```

Existing `POST /api/play` also accepts `{"text":"Battery low"}` or `{"type":"tts-offline","url":"Battery low"}`. Existing URL-based Home Assistant TTS remains available. MQTT `<base>/tts` accepts a plain English text payload or `{"text":"Battery low"}`. HTTP(S) URL payloads retain the existing remote-audio behavior.

## Implemented processing

- MP3, AAC, WAV, M4A, FLAC local files use the existing decoder/storage backend.
- Source EQ reuses `Audio::setTone` and the current three bands (−6 to +6 dB).
- Volume is 0–100%; source gain/EQ restore saved global defaults on stop or ordinary playback.
- Speed 0.25–4× changes the hardware clock without changing the decoder sample rate (preserving seek/duration metadata).
- Pitch −24…+24 semitones uses two cross-faded delay taps, compensating the speed clock. This is basic granular processing with possible artifacts, not studio time stretching.
- Offline English speech uses the new original rule-based formant synthesizer. ASCII text at most 256 characters; simple pronunciation, robotic voice, numbers pronounced digit by digit. Other languages are rejected. Synthesis writes at most 512 PCM samples per loop iteration instead of monopolizing the main task.
- SD scratch storage is preferred; internal flash is the fallback. At least the estimated WAV size plus 64 KiB must be free. `/.elma-speech.wav` is reserved runtime scratch, closed/deleted on completion, stop or update. Reusing this reserved name prevents orphan files accumulating after power loss. Frequent speech is best served from SD to avoid repeated internal-flash writes.
- Builds with `APP_DISABLE_AUDIO` reject source requests; no speech engine/audio DSP is linked into those profiles.

## Image size

Link-time optimization keeps the full runtime within current 4 MB OTA partitions. During Windows compilation `ELMA_ACTIVE_PERIPHERAL_PROFILES` specifies active canonical `group:profile` entries. `asset_embed.py` turns these into `APP_PERIPHERAL_SVG_MASK`; the prebundled C++ assets choose detailed SVGs for active devices and compressed blocks for unused ones. Asset URLs and all electrical connector labels remain intact. Standard builds without this setting conservatively retain every illustration. Selected-board asset guards continue to omit other board images. No peripheral functions are removed.

The manifest is exported from the same native/shared electrical catalog. Existing Configuration and the Windows SVG assets are unchanged. Placeholder SVGs apply only to flashed firmware assets, and a newly configured peripheral whose picture was omitted still exposes its normal ports/settings.

## Validation

`tests/logic_audio_dsp_test.cpp` is a host test for bounded speech synthesis, output energy, invalid text and independent pitch shift frequency/frame-count/stereo preservation. PlatformIO ESP32-S3 and no-audio builds verify integration/partition size. These checks do not establish speech intelligibility, glitch-free playback, flash longevity or real DAC audio output; hardware listening remains required.

Engine audit: the pinned ESP32-audioI2S speech method is online; existing application TTS played externally generated URLs. The [Flite Arduino port](https://github.com/pschatzmann/arduino-flite#memory-requirements) lists a 2.34 MB minimum voice, exceeding our current OTA slot alone. [Espressif TTS](https://docs.espressif.com/projects/esp-sr/en/latest/esp32/speech_synthesis/readme.html) supports Chinese only. The new minimal English engine avoids adding a voice database or changing existing peripheral features.

## Recorded Piano runtime

`POST /api/audio/source` also accepts:

```json
{"melody":{"instrument":"piano","notes":[{"note":60,"start":0,"duration":0.5,"velocity":0.75},{"note":64,"start":0,"duration":0.5,"velocity":0.75}]}}
```

Notes are MIDI integers 12–108; timing is in seconds; velocity is greater than zero and at most one. Maximum 128 events / 60 seconds. Instruments: piano, bell, guitar, organ. The compact oscillator synth supports simultaneous notes and writes a temporary 16 kHz mono PCM WAV cooperatively to mounted SD or flash before using the existing player/DSP. Scratch space must cover the full WAV plus 64 KiB. These are basic synthesized timbres, not sampled instruments.

A compiled Piano asset can be played with `{"melodyId":"<node UUID>"}`. Windows compilation embeds independent assets in `ui.recordedMelodies` together with current project defaults. Existing saved device settings keep their normal precedence; melody assets come from the compiled binary. Windows now validates and embeds supported event graphs through a separate bounded Logics runtime.

For a configured buzzer, append `"output":{"kind":"passive","slot":"audio:0"}` (or a configured control slot). `kind:"active"` reproduces timing as fixed-pitch beeps. A buzzer requires a valid configured SIG binding, rejects overlapping notes, and has no EQ/volume DSP. Playback is cooperative, uses LEDC channel 7 for passive output, and stops during OTA transfer. DAC requests omit `output` and retain per-source DSP.

The authenticated endpoint accepts at most 16 KiB, with a two-item queue of heap buffers (PSRAM preferred). Invalid types, unsupported instruments, out-of-range notes/times and incompatible buzzer recordings are rejected before queuing. The separate `logic_runtime` engine evaluates compiled graphs and dispatches playback through this same queue.

Compiled defaults are generated locally through `scripts/project_defaults.py`. The generated header and temporary JSON are ignored by Git because a project may include network credentials; packaging uses empty template defaults. Windows compile jobs preserve the editor graph in the project and embed a separate compact `ELMA_COMPILED_LOGICS` program.


## Compiled event graphs

`logic_runtime.h/.cpp` is independent of Arduino drivers. It evaluates typed data sources and bounded execution queues, with nonblocking Delay, Timer, Repeat, Debounce and Cooldown scheduling. `logic_device.h/.cpp` samples current device status and bridges supported relay, input, battery, DAC, MQTT, update-check and restart actions. Unsupported adapters are rejected by the Windows compiler.

Repeat produces Count total pulses (first immediately); new starts during an active repeat are ignored. Edge events require a previously available baseline, so an already-high temperature at boot does not trigger a rising edge. Missing telemetry remains unavailable. OTA cancels timers and rebaselines events. Controlled shutdown supports immediate actions, not deferred playback completion.

The host execution test uses the actual C++ engine to exercise a >50°C temperature crossing, three TTS Play requests at 5-second intervals, rearming, unavailable samples, OTA cancellation and millis wraparound. ESP32, ESP32-S3 and ESP32-C3 builds check integration and image size. Physical DAC output still requires testing on a flashed device.
