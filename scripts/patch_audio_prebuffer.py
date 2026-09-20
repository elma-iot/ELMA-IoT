"""Raise ESP32-audioI2S's stream start watermark before it is compiled.

The upstream 2.0.6 release begins decoding after only one compressed frame.
That is too shallow for Wi-Fi radio streams and makes the first seconds
audibly underrun while TLS, metadata and redirects settle.  Keep the pinned
library, but apply this small, verified source patch in each PlatformIO env.
"""

from pathlib import Path

Import("env")

libdeps = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV")
audio_sources = list(libdeps.glob("ESP32-audioI2S*/src/Audio.cpp"))
old = "InBuff.bufferFilled() > maxFrameSize && !f_stream"
new = (
    "InBuff.bufferFilled() >= (InBuff.havePSRAM() ? 65536U : "
    "static_cast<size_t>(maxFrameSize) * 3U) && !f_stream"
)

if audio_sources:
    source = audio_sources[0]
    text = source.read_text(encoding="utf-8")
    if new not in text:
        count = text.count(old)
        if count != 4:
            raise RuntimeError(
                f"ESP32-audioI2S prebuffer patch expected 4 stream guards, found {count}"
            )
        source.write_text(text.replace(old, new), encoding="utf-8")
        print("[audio] patched ESP32-audioI2S stream prebuffer: 64 KiB PSRAM / 3 frames RAM")
