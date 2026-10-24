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
    if text != source.read_text(encoding="utf-8"):
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
    # Upstream 2.0.6 aligns this assignment with spaces; curated copies may
    # already have normalized whitespace. Both have one constructor assignment.
    pattern=r'm_i2s_config\.dma_buf_len\s*=\s*\(psramFound\(\)\s*\?\s*512\s*:\s*256\);'
    text,count=re.subn(pattern,'m_i2s_config.dma_buf_len = elmaAudioDmaLength((psramFound()?512:256),internalDAC);',text)
    if count!=1:raise RuntimeError(f'Unexpected DMA frame length assignments: {count}')
    source.write_text(text,encoding='utf-8')
    print('[audio] Sunton 3248 DAC: native-rate signed PCM with independent channel bias')


# Correct concrete PCM hazards in the pinned decoder without replacing it.
for source in audio_sources:
    text=source.read_text(encoding='utf8')
    if '// ELMA_PCM_INTEGRITY' in text:continue
    import re
    text,count=re.subn(r'\(int16_t\)\s*outSample\[(LEFTCHANNEL|RIGHTCHANNEL)\]',r'elmaSaturateAudioPcm(outSample[\1],m_f_internalDAC)',text)
    if count!=6:raise RuntimeError('Unexpected EQ conversion count')
    for channel in ('LEFTCHANNEL','RIGHTCHANNEL'):
        pattern=r'\(\(sample\['+channel+r'\]\s*& 0xff\) -128\) << 8'
        text,count=re.subn(pattern,'((sample['+channel+'] & 0xff) -128) * 256',text)
        if count!=1:raise RuntimeError('Unexpected 8-bit PCM upconversion')
    old='return (v[LEFTCHANNEL] << 16) | (v[RIGHTCHANNEL] & 0xffff);'
    if text.count(old)!=1:raise RuntimeError('Unexpected signed PCM packing')
    text=text.replace(old,'return (uint32_t(uint16_t(v[LEFTCHANNEL])) << 16) | uint16_t(v[RIGHTCHANNEL]);',1)
    start=text.index('bool Audio::playChunk()');end=text.index('\nvoid Audio::loop()',start)
    chunk=text[start:end]
    # Resume the second byte of a mono 8-bit word if its DMA write fails.
    old='''                sample[LEFTCHANNEL]  = x;
                sample[RIGHTCHANNEL] = x;
                while(1) {
                    if(playSample(sample)) break;
                } // Can't send?
                sample[LEFTCHANNEL]  = y;
                sample[RIGHTCHANNEL] = y;
                while(1) {
                    if(playSample(sample)) break;
                } // Can't send?'''
    new='''#if APP_SUNTON_PANEL == 3
                if(!m_elmaPcm8HalfPending){
                    sample[LEFTCHANNEL]=sample[RIGHTCHANNEL]=x;
                    if(!playSample(sample))return false;
                    m_elmaPcm8HalfPending=true;
                }
                sample[LEFTCHANNEL]=sample[RIGHTCHANNEL]=y;
                if(!playSample(sample))return false;
                m_elmaPcm8HalfPending=false;
#else
'''+old+'\n#endif'
    if chunk.count(old)!=1:raise RuntimeError('Unexpected 8-bit mono writer')
    chunk=chunk.replace(old,new,1)
    stereo=chunk.index('        if(getChannels() == 2)')
    old='''                while(1) {
                    if(playSample(sample)) break;
                } // Can't send?'''
    position=chunk.index(old,stereo)
    chunk=chunk[:position]+'''#if APP_SUNTON_PANEL == 3
                if(!playSample(sample))return false;
#else
'''+old+'\n#endif'+chunk[position+len(old):]
    # 16-bit stereo previously discarded failed samples and reset its offset.
    position=chunk.index('    if(getBitsPerSample() == 16)')
    prefix,tail=chunk[:position],chunk[position:]
    old='''        if(getChannels() == 2) {
            m_curSample = 0;'''
    new='''        if(getChannels() == 2) {
#if APP_SUNTON_PANEL != 3
            m_curSample = 0;
#endif'''
    if tail.count(old)!=1:raise RuntimeError('Unexpected stereo frame offset')
    tail=tail.replace(old,new,1)
    old='                playSample(sample);'
    if tail.count(old)!=1:raise RuntimeError('Unexpected stereo sample writer')
    tail=tail.replace(old,'''#if APP_SUNTON_PANEL == 3
                if(!playSample(sample))return false;
#else
                playSample(sample);
#endif''',1)
    text=text[:start]+prefix+tail+text[end:]
    text=text.replace('void Audio::setDefaults() {','''void Audio::setDefaults() {
#if APP_SUNTON_PANEL == 3
    m_elmaPcm8HalfPending=false;
#endif''',1)

    # A failed frame must not pass through volume/EQ/DSP a second time.
    a=text.index('bool Audio::playSample(int16_t sample[2]) {');b=text.index('\nvoid Audio::setTone(',a)
    sample=text[a:b]
    sample=sample.replace('bool Audio::playSample(int16_t sample[2]) {','''bool Audio::playSample(int16_t sample[2]) {
#if APP_SUNTON_PANEL == 3
    uint32_t s32=m_elmaPendingPcm;
    if(!m_elmaI2sPending){
#endif''',1)
    sample=sample.replace('    uint32_t s32 = Gain(sample); // vosample2lume;','''#if APP_SUNTON_PANEL == 3
    s32=Gain(sample);
#else
    uint32_t s32=Gain(sample);
#endif''',1)
    sample=sample.replace('    m_i2s_bytesWritten = 0;','''#if APP_SUNTON_PANEL == 3
    m_elmaPendingPcm=s32;
    }
#endif
    m_i2s_bytesWritten = 0;''',1)
    sample=sample.replace('        return false;','''#if APP_SUNTON_PANEL == 3
        m_elmaI2sPending=true;
#endif
        return false;''')
    # The final successful return, not the optional external-DSP early return.
    sample=sample.replace('    return true;\n}', '''#if APP_SUNTON_PANEL == 3
    m_elmaI2sPending=false;
#endif
    return true;
}''',1)
    text=text[:a]+sample+text[b:]
    old='''    while(m_validSamples) {
        playChunk();
    }'''
    new='''#if APP_SUNTON_PANEL == 3
    uint8_t writeFailures=0;
    while(m_validSamples){
        if(playChunk())writeFailures=0;
        else if(++writeFailures>=3){
            m_validSamples=0;m_curSample=0;m_f_running=false;
            m_elmaI2sPending=false;m_elmaPcm8HalfPending=false;
            if(audio_info)audio_info("I2S output stalled after retries");
            break;
        }else vTaskDelay(1);
    }
#else
'''+old+'\n#endif'
    if text.count(old)!=2:raise RuntimeError('Unexpected decoded/DMA cleanup drains')
    text=text.replace(old,new)
    text=text.replace('    m_elmaPcm8HalfPending=false;\n#endif','    m_elmaPcm8HalfPending=false;m_elmaI2sPending=false;\n#endif',1)
    text='// ELMA_PCM_INTEGRITY\n'+text
    header=source.with_name('Audio.h');h=header.read_text(encoding='utf8')
    marker='    int16_t         m_curSample = 0;'
    if h.count(marker)!=1:raise RuntimeError('Unexpected Audio frame state')
    h=h.replace(marker,marker+'\n#if APP_SUNTON_PANEL == 3\n    bool m_elmaPcm8HalfPending=false,m_elmaI2sPending=false;\n    uint32_t m_elmaPendingPcm=0;\n#endif',1)
    header.write_text(h,encoding='utf8');source.write_text(text,encoding='utf8')
    print('[audio] Saturation-safe EQ, defined PCM packing and resumable DMA writes')

# Cleanup emits zero frames, never a stale partially written decoded block.
for source in audio_sources:
    text=source.read_text(encoding='utf8')
    marker='// ELMA_DMA_DRAIN_CURSOR'
    if marker not in text:
        needle='    m_validSamples = m_i2s_config.dma_buf_len * m_i2s_config.dma_buf_count;'
        if text.count(needle)!=1:raise RuntimeError('Unexpected DMA drain size')
        text=text.replace(needle,marker+'\n#if APP_SUNTON_PANEL == 3\n    m_curSample=0;m_elmaI2sPending=false;m_elmaPcm8HalfPending=false;\n#endif\n'+needle,1)
        source.write_text(text,encoding='utf8')
