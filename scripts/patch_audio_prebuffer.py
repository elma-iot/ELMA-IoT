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

# Hardware music DMA covers about 186 ms at 44.1 kHz; voice retains 3 x 128.
if audio_sources:
    source=audio_sources[0]
    text=source.read_text(encoding="utf-8")
    import re
    text=re.sub(r"(m_i2s_config\.dma_buf_count\s*= )(?:4|8);", r"\g<1>16;", text)
    text=re.sub(r"(m_i2s_config\.dma_buf_len\s*= )256;", r"\g<1>512;", text)
    start=text.index("void Audio::processLocalFile()")
    end=text.index("void Audio::processWebStream()",start)
    local=text[start:end]
    if "refillLocalBuffer" not in local:
        local=local.replace("    static uint32_t byteCounter;", "    static bool refillLocalBuffer;\n    static uint32_t byteCounter;",1)
        local=local.replace("        byteCounter = 0;", "        byteCounter = 0;\n        refillLocalBuffer = true;",1)
        old_read="    int32_t bytesAddedToBuffer = audiofile.read(InBuff.getWritePtr(), availableBytes);"
        new_read="""    // Refill the large PSRAM ring in bounded batches instead of tiny SD reads.
    if(psramFound()) {
        if(InBuff.bufferFilled() < 128 * 1024) refillLocalBuffer = true;
        if(InBuff.freeSpace() < maxFrameSize) refillLocalBuffer = false;
        if(!refillLocalBuffer && f_stream) availableBytes = 0;
        availableBytes = min(availableBytes, uint32_t(4096));
    }
    int32_t bytesAddedToBuffer = availableBytes ? audiofile.read(InBuff.getWritePtr(), availableBytes) : 0;"""
        if local.count(old_read)!=1:raise RuntimeError("Unexpected local audio refill implementation")
        local=local.replace(old_read,new_read)
        text=text[:start]+local+text[end:]
    source.write_text(text,encoding="utf-8")
