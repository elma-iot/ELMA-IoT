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

# SPK voice/camera coexistence: 16 x 512 stereo frames consume 32 KiB of
# DMA-capable SRAM. Four x 256 frames need 4 KiB; network prebuffer stays in PSRAM.
if audio_sources:
    source=audio_sources[0]
    text=source.read_text(encoding="utf-8")
    if "ELMA_SPK_DMA_BOUNDS" not in text:
        for field,old_value,new_value in [("dma_buf_count",16,4),("dma_buf_len",512,256)]:
            old=f"    m_i2s_config.{field}        = {old_value};" if field=="dma_buf_count" else f"    m_i2s_config.{field}          = {old_value};"
            import re
            pattern=r"    m_i2s_config\."+field+r"\s*= "+str(old_value)+r";"
            text,count=re.subn(pattern,"#if defined(APP_SPK_BOARD) // ELMA_SPK_DMA_BOUNDS\n    m_i2s_config."+field+" = "+str(new_value)+";\n#else\n    m_i2s_config."+field+" = "+str(old_value)+";\n#endif",text)
            if count!=1:raise RuntimeError("Unexpected audio DMA configuration: "+field)
        source.write_text(text,encoding="utf-8")
        print("[audio] SPK DMA reduced to 4 KiB; stream input remains in PSRAM")
