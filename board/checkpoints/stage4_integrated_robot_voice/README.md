# Stage 4 Integrated Robot Voice

This checkpoint combines the previously validated Wi-Fi/robotctl board work
with the OLED and provisional I2S record/playback application. The user entry
point is `voice_echo`: record five seconds from I2S0, replay once on I2S1, and
return to NSH. GPIO14 is unused on the current board.

Hardware validation remains pending; see `CODEX_HANDOFF.md`.
