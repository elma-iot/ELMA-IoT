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

# Keep large music DMA on PSRAM boards; save 24 KiB internally without PSRAM.
# Voice retains its independent 3 x 128 low-latency configuration.
if audio_sources:
    source=audio_sources[0]
    text=source.read_text(encoding="utf-8")
    import re
    text=re.sub(r"(m_i2s_config\.dma_buf_count\s*= )(?:4|8|16);", r"\g<1>(psramFound() ? 16 : 8);", text)
    dma_policy="""#if APP_SUNTON_PANEL
#define ELMA_AUDIO_RAM_DMA_COUNT 4
#else
#define ELMA_AUDIO_RAM_DMA_COUNT 8
#endif
"""
    if '#define ELMA_AUDIO_RAM_DMA_COUNT' not in text:text=dma_policy+text
    text=text.replace('(psramFound() ? 16 : 8);','(psramFound() ? 16 : ELMA_AUDIO_RAM_DMA_COUNT);')
    text=re.sub(r"(m_i2s_config\.dma_buf_len\s*= )(?:256|512);", r"\g<1>(psramFound() ? 512 : 256);", text)
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
    read_line="    int32_t bytesAddedToBuffer = availableBytes ? audiofile.read(InBuff.getWritePtr(), availableBytes) : 0;"
    if "Local file read failed; card removed or storage unavailable" not in text:
        if text.count(read_line)!=1:raise RuntimeError("Unexpected local audio read guard location")
        text=text.replace(read_line,read_line+"""
    // A synchronous local read must make progress when bytes are requested.
    // Close the file on I/O loss so the application can release its SD lease
    // before the hotplug worker unmounts or retries the card.
    if(availableBytes && bytesAddedToBuffer <= 0) {
        stopSong();
        if(audio_info)audio_info("Local file read failed; card removed or storage unavailable");
        return;
    }""",1)
    source.write_text(text,encoding="utf-8")

# Helix's four large integer-only workspaces can use the classic ESP32's
# otherwise unused word-accessible IRAM heap. Byte streams and PCM stay DRAM.
for source in libdeps.glob("ESP32-audioI2S*/src/mp3_decoder/mp3_decoder.cpp"):
    text=source.read_text(encoding="utf-8")
    marker="// ELMA MP3 word-only workspace policy"
    if marker not in text:
        helper='''
// ELMA MP3 word-only workspace policy
static void elmaClearMp3Words(void* pointer,size_t bytes) {
#if APP_SUNTON_PANEL && defined(CONFIG_IDF_TARGET_ESP32)
    // Volatile prevents the compiler from replacing this with byte memset.
    auto* words=static_cast<volatile uint32_t*>(pointer);
    for(size_t i=0;i<bytes/sizeof(uint32_t);++i)words[i]=0;
#else
    memset(pointer,0,bytes);
#endif
}
'''
        text=text.replace('#include "mp3_decoder.h"','#include "mp3_decoder.h"\n'+helper,1)
        allocation='''static void* elmaAllocateMp3Words(size_t bytes) {
#if APP_SUNTON_PANEL && defined(CONFIG_IDF_TARGET_ESP32)
    if(void* p=heap_caps_malloc(bytes,MALLOC_CAP_INTERNAL|MALLOC_CAP_32BIT))return p;
#endif
    return __malloc_heap_psram(bytes);
}

'''
        text=text.replace('bool MP3Decoder_AllocateBuffers(void) {',allocation+'bool MP3Decoder_AllocateBuffers(void) {',1)
        for name in ('HuffmanInfo','DequantInfo','IMDCTInfo','SubbandInfo'):
            # All members and every decoder access are aligned 32-bit ints.
            old='__malloc_heap_psram(sizeof('+name+'_t)'
            if text.count(old)!=1:raise RuntimeError('Unexpected MP3 workspace allocation: '+name)
            text=text.replace(old,'elmaAllocateMp3Words(sizeof('+name+'_t)',1)
            import re
            pattern=r'memset\(\s*m_'+name+r',\s*0,\s*sizeof\('+name+r'_t\)\);'
            text,count=re.subn(pattern,'elmaClearMp3Words(m_'+name+',sizeof('+name+'_t));',text)
            if count!=1:raise RuntimeError('Unexpected MP3 workspace clearing: '+name)
        source.write_text(text,encoding="utf-8")
        print('[audio] MP3 integer workspaces prefer word-only IRAM on no-PSRAM Sunton ESP32')

for source in audio_sources:
    text=source.read_text(encoding="utf-8")
    if '#include "sunton_dac.h"' not in text:
        import re
        text=text.replace('#include "Audio.h"','#include "Audio.h"\n#include "sunton_dac.h"',1)
        pattern=r'i2s_zero_dma_buffer\(\(i2s_port_t\)\s*m_i2s_num\);'
        text,count=re.subn(pattern,'elmaClearAudioDma((i2s_port_t)m_i2s_num,m_f_internalDAC,m_i2s_config.dma_buf_count,m_i2s_config.dma_buf_len);',text)
        if count<4:raise RuntimeError('Unexpected audio DMA silence implementation')
        text,count=re.subn(r'm_i2s_config\.tx_desc_auto_clear\s*=\s*true;',
            'm_i2s_config.tx_desc_auto_clear = elmaAudioAutoClear(internalDAC);',text)
        if count!=1:raise RuntimeError('Unexpected audio DMA auto-clear implementation')
        old='i2s_set_dac_mode((i2s_dac_mode_t)m_f_channelEnabled);'
        if text.count(old)!=1:raise RuntimeError('Unexpected DAC output initialization')
        text=text.replace(old,'elmaStartDacOutput((i2s_port_t)m_i2s_num,(i2s_dac_mode_t)m_f_channelEnabled,m_i2s_config.dma_buf_count,m_i2s_config.dma_buf_len);',1)
        source.write_text(text,encoding="utf-8")
        print('[audio] Sunton DAC midpoint silence and startup bias ramp installed')

for source in audio_sources:
    text=source.read_text(encoding='utf-8')
    if 'elmaWriteAudioSample(' in text:continue
    import re
    text,count=re.subn(r'    if\(m_f_internalDAC\) \{\s*s32 \+= 0x80008000;\s*\}', '', text)
    if count!=1:raise RuntimeError('Unexpected DAC sample bias conversion')
    old='i2s_write((i2s_port_t) m_i2s_num, (const char*) &s32, sizeof(uint32_t), &m_i2s_bytesWritten, 100)'
    if text.count(old)!=1:raise RuntimeError('Unexpected single-frame I2S writer')
    text=text.replace(old,'elmaWriteAudioSample((i2s_port_t)m_i2s_num,s32,m_f_internalDAC,&m_i2s_bytesWritten,100)',1)
    text=text.replace('if(m_i2s_bytesWritten < 4)', 'if(m_i2s_bytesWritten < elmaAudioFrameBytes(m_f_internalDAC))',1)
    text=text.replace('m_i2s_config.sample_rate          = 16000;', 'm_i2s_config.sample_rate = elmaDacSampleRate(16000,internalDAC);',1)
    old='i2s_set_sample_rates((i2s_port_t)m_i2s_num, sampRate);'
    if text.count(old)!=1:raise RuntimeError('Unexpected I2S rate setter')
    text=text.replace(old,'i2s_set_sample_rates((i2s_port_t)m_i2s_num,elmaDacSampleRate(sampRate,m_f_internalDAC));',1)
    old='m_i2s_config.dma_buf_len = (psramFound() ? 512 : 256);'
    if text.count(old)!=2:raise RuntimeError('Unexpected DMA frame length')
    text=text.replace(old,'m_i2s_config.dma_buf_len = elmaAudioDmaLength((psramFound()?512:256),internalDAC);')
    source.write_text(text,encoding='utf-8')
    print('[audio] Sunton 3248 DAC: 2x interpolation and shaped/dithered eight-bit conversion')
