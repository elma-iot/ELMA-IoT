# ESP32-S3 audio playback

The SPK music output uses the hardware I2S peripheral and DMA: 16 descriptors of 512 stereo 16-bit frames (32 KiB internal DMA memory, about 186 ms at 44.1 kHz). Intercom retains its separate 3 x 128-frame low-latency configuration. DMA memory must remain internal; the 1280 KiB compressed input ring resides in PSRAM.

MP3/AAC/FLAC are software decoded; ESP32-S3 has no dedicated compressed-audio decoder. The existing Xtensa decoder optimizations remain enabled. WAV PCM uses the same hardware output without a compressed codec. See https://documentation.espressif.com/esp32-s3_datasheet_en.pdf and https://docs.espressif.com/projects/esp-idf/en/v5.1/esp32s3/api-reference/peripherals/i2s.html.

For PSRAM-equipped boards, local input reads refill below 128 KiB and stop when full. Each read is bounded to 4 KiB. This prevents repeated tiny SD reads while seconds of input are already buffered. Boards without PSRAM retain their existing refill behavior. Disabling the setup access point after station association is deferred during audio playback or OTA, since Wi-Fi mode switching can stall the main service loop. An active external camera integration stream continues to consume camera resources.

Serial diagnostics report the maximum playback service gap, buffered input bytes and sample rate every ten seconds. A larger buffer cannot fix unsupported audio formats, permanent SD errors or CPU overload; high bitrate/sample-rate files require testing on the actual board. Audible testing remains necessary in addition to uninterrupted playback-state checks.

The bundled FLAC decoder supports up to 16-bit samples. Prepare 24-bit FLAC as 16-bit FLAC, WAV, MP3 or AAC on a computer before copying it to the SD card; a larger input buffer does not add 24-bit decoding support.
