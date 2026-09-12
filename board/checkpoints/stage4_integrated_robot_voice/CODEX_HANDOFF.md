# Stage 4 Integrated Robot Voice Echo Handoff

## Baseline

- Board: ESP32-S3-WROOM-1-N16R8, Simple Boot, UART0/NSH.
- Stage 2: `/dev/i2c0`, `/dev/pwm0`, I2S0 MIC RX, I2S1 speaker TX.
- Stage 3A Wi-Fi configuration is restored in the integrated defconfig.
- Branch: `feature/integrated-robot-voice-echo`.

## Integrated Features

- Wi-Fi: ESP32-S3 STA tools `wapi`, `renew`, and `ping`; credentials remain NSH-configured.
- Robot: `robotctl` and POWER_SAFE PWM motion commands are retained.
- OLED: SSD1306-compatible, 128x64, I2C0 GPIO12/GPIO13, test address 0x3c.
- Audio: I2S0 is MIC RX (GPIO16/17/18), I2S1 is speaker TX (GPIO38/39/40).
- Audio profile remains provisional: mono, 16-bit samples in 16-bit slots, 16 kHz RX and 24 kHz TX.

## voice_echo

`voice_echo` is an NSH-only, one-shot command. It records exactly five seconds
(16000 * 5 * sizeof(int16_t) = 160000 bytes), prints min/max/peak statistics,
then plays the recording once through the existing chunked 16 kHz to 24 kHz
3:2 conversion. It does not allocate a complete second playback buffer.

GPIO14 is NC/unused on the current board. The legacy reference push-to-talk
mode and `/dev/buttons` are not used by this application. `voice_echo --once`
is retained as an alias of the same fixed five-second behavior.

OLED updates are best-effort: `REC 5 SEC`, `PLAY`, `DONE`, or an error label.
OLED failure does not abort the audio path.

## Build

```bash
cd ~/vela/openvela
source myenv/bin/activate
contest2026_295_suanliheidong/board/contest_board/scripts/build_with_hal_backport.sh -j8
python -m esptool --chip esp32s3 image-info nuttx/nuttx.bin
```

The wrapper temporarily applies the HAL lock compatibility and NuttX
BREAK/BREAK.N backports, then restores both. No permanent restricted-source
change is part of this stage.

## Hardware Test Pending

Build and image checks do not validate OLED visibility, microphone capture,
speaker playback, Wi-Fi regression, or servo motion. For the first audio test,
run `robotctl off` manually before `voice_echo`.

```text
nsh> help
nsh> ls /dev
nsh> wapi show wlan0
nsh> robotctl help
nsh> robotctl status
nsh> robotctl off
nsh> voice_echo
```

Do not run servo motion and audio playback concurrently during first tests.
