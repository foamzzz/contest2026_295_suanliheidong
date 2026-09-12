# TTS to Playback Fixes

This package only changes the contest-local TTS/playback files. It does not require modifying `packages/ai_agent`.

## 1. TTS is isolated from the ai_agent outbound thread

`mimo_tts_speak_stream()` now runs the JSON/TLS/TTS decode path in a dedicated 32 KiB pthread and joins it synchronously. The API key and text are copied before the worker starts, avoiding use-after-free of the outbound message and avoiding TLS/cJSON stack pressure on the ai_agent dispatch thread.

## 2. Large TTS responses no longer go through cJSON

The request JSON still uses cJSON because it is small. The response is scanned in place for `audio.data`, so a multi-megabyte base64 string is not duplicated by cJSON. A dedicated 2 MiB TTS response capacity is used while ordinary ASR/JSON responses keep the original 256 KiB limit.

This remains a synchronous HTTP compatibility path. For the lowest first-audio latency, the next optimization should be a contest-local HTTP streaming transport that feeds base64 incrementally.

## 3. WAV parsing is now chunk-aware

The old decoder assumed a fixed 44-byte WAV header. The new parser walks RIFF chunks, validates `fmt ` as PCM signed 16-bit little-endian, 24 kHz, mono, and emits only the `data` chunk. Extra `LIST`, `JUNK`, or extended chunks are skipped safely.

PCM is emitted in 4096-byte batches instead of 1-3 bytes per base64 quartet.

## 4. Playback ring buffer now provides backpressure

When the decoder is faster than real-time playback, `robot_audio_playback_write()` waits for ring-buffer space instead of returning `-EAGAIN`. This prevents long TTS responses from being truncated after the first 256 KiB.

## 5. I2S keeps two DMA transfers outstanding

The previous worker submitted one block and waited for its callback before submitting the next block. That creates scheduling gaps between blocks. The new worker keeps two DMA slots in flight so `contest_i2s` can chain active/pending descriptors continuously.

Playback starts after a 16 KiB prebuffer and uses the already validated board format:

- source: PCM s16le, 24000 Hz, mono
- I2S wire: 16-bit, two slots
- mono sample duplicated into left/right slots
- source block: 1024 bytes
- wire block: 2048 bytes

## Expected key logs

```text
[RV-TTS] worker start stack=32768
[RV-TTS] request begin body=... response_cap=2097152
[RV-TTS] transport done response=...
[RV-TTS] WAV fmt pcm_s16le 24000Hz 1ch 16bit
[RV-TTS] WAV data begin bytes=... rate=24000
[RV-PB] PREBUFFER -> PLAYING buffered=...
[RV-TTS] decode done b64=... result=0
[RV-TTS] worker done rc=0
[RV-PB] worker exit rc=0
```

If the response exceeds 2 MiB, the code now reports `response too large` instead of parsing truncated JSON. That is the point where true HTTP streaming should be added rather than increasing buffers indefinitely.
