# Robot Voice Playback Fix

This version aligns the contest-local playback path with the previously proven `audio_playback.c` implementation.

Key settings:

- MiMo source PCM: 24000 Hz, mono, signed 16-bit little-endian.
- Physical I2S TX: 24000 Hz, 2 slots/channels, 16-bit.
- Mono samples are explicitly duplicated to L/R slots.
- Source block: 1024 bytes = 512 PCM16 frames = about 21.33 ms.
- DMA/APB wire block: 2048 bytes.
- Maximum in-flight DMA slots: 2.
- Submit cadence: slot0, slot1, wait0/refill0, wait1/refill1.
- Playback worker priority: 120.
- Playback worker stack: 16 KiB.
- Ring: 1 MiB.
- Normal utterances start at EOS; long utterances start when the ring fills.
- Rebuffer threshold: 64 KiB.

Expected startup log:

    [RV-PB] I2S configured source=24000Hz/mono/16bit wire=24000Hz/2ch/16bit source_block=1024 wire_dma=2048 frame_ms=21.33

For a 115200-byte PCM utterance, expected DMA accounting is:

    slots=57/57,56/56

with:

    underruns=0
    result=0

Replace the contest-local `robot_audio_playback.c`; do not modify `packages/ai_agent`.
