# Full-cache TTS playback variant

Purpose: eliminate drag/ghosting caused by overlapping WLAN/TLS activity and I2S DMA.

## Playback timeline

1. `robot_audio_playback_open()`
   - Allocates a 2 MiB PCM cache.
   - Does **not** initialize/start I2S.
   - Does **not** create the playback worker.

2. `robot_audio_playback_write()`
   - Stores verified MiMo PCM (`pcm_s16le`, 24000 Hz, mono, 16-bit).
   - No DMA is active during TTS transport/decode.

3. `robot_audio_playback_finish()`
   - Marks EOS.
   - Configures I2S only now:
     - physical channels/slots: 2
     - sample rate: 24000 Hz
     - width: 16 bit
   - Creates the high-priority playback worker.
   - Converts each 1024-byte mono block into a 2048-byte duplicated L/R DMA block.
   - Uses two DMA slots in strict alternating order.
   - Waits until all PCM is drained, then stops I2S.

## Expected logs

Before TTS network request / while decoding:

    [RV-PB] cache-only open capacity=2097152 source=24000Hz/mono/16bit; I2S deferred until EOS

After TTS transport + decoding has completely finished:

    [RV-PB] TTS EOS cached_pcm=...; starting isolated I2S playback now
    [RV-PB] I2S START after TTS EOS source=24000Hz/mono/16bit wire=24000Hz/2ch/16bit ...

At completion:

    [RV-PB] isolated playback finish ... underruns=0 result=0 ...

If `I2S START after TTS EOS` appears before the TTS transport/decode-done log, the caller is invoking `robot_audio_playback_finish()` too early and the TTS caller must be fixed.
