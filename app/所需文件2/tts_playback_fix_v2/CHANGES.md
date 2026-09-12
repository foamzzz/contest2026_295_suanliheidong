# TTS playback fix v2

This revision targets the new hardware log where MiMo TTS transport and WAV
parsing completed successfully, audio played with heavy ghosting/drag, and the
system crashed at the end of the I2S transfer (`tx eof=113 ...`) with
`EXCCAUSE=0x1c`.

## What the log proves

The failure is no longer in MiMo HTTP/TLS or Base64/WAV parsing:

- `response status=200`
- `WAV fmt pcm_s16le 24000Hz 1ch 16bit`
- `WAV data begin bytes=115200 rate=24000`
- `decode done ... result=0`
- `worker done rc=0`

The panic happens while/after the playback worker drains the last I2S DMA
transfers.  Therefore the fix is isolated to the contest-local playback side.

## Playback changes

1. **Explicit 16 KiB playback pthread stack**
   - The previous worker used the default pthread stack while simultaneously
     holding two 1024-byte source arrays and a 2048-byte wire array in a nested
     call.  That is unsafe on NuttX and can corrupt memory non-deterministically.
   - The new worker uses an explicit 16 KiB stack.

2. **Remove the 2048-byte temporary wire buffer from the stack**
   - Mono PCM is duplicated directly into the allocated APB sample storage.

3. **Use the proven APB ownership model**
   - `I2S_SEND()` takes its own reference in `contest_i2s_send()`.
   - The producer drops its reference immediately after a successful submit.
   - The lower half drops its reference after the callback.
   - Logical slots no longer retain APB pointers after submission.

4. **Callback owns slot completion state**
   - The callback sets `active=false`, stores the result, and only then posts
     the slot semaphore.  The worker never reuses a logical slot before that
     slot's own callback completed.

5. **Restore decode/playback isolation from the known-good implementation**
   - Ring size is 1 MiB.
   - Normal utterances start playback only after EOS (TTS decode complete).
   - If a very long utterance fills the 1 MiB ring before EOS, playback starts
     at the full-ring threshold to avoid producer deadlock.
   - With the current synchronous HTTP transport this does not add network
     latency: the complete HTTP response was already received before decoding
     begins.  It only prevents Base64 decode and real-time I2S from competing.

6. **Strict two-slot cadence**
   - Submit order is `slot0, slot1, wait0/refill0, wait1/refill1, ...`, matching
     the previously successful `packages_修改/audio_playback.c` behavior.

## Expected healthy log

For the 115200-byte PCM example, the important tail should become similar to:

```text
[RV-TTS] decode done ... result=0
[RV-TTS] worker done rc=0
[RV-PB] PREBUFFER -> PLAYING buffered=115200 eos=1
...
[C-I2S] tx eof=113 ...
[RV-PB] worker exit rc=0 slots=57/57,56/56
```

There should be no `EXCCAUSE=0x1c` after the I2S EOF summary.
