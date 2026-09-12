# Stage 3 OLED + Audio Loopback Handoff

## Baseline

- Development branch: `feature/oled-audio-loopback`.
- ESP32-S3-WROOM-1-N16R8, Minimal NSH, UART0, `/dev/buttons`, `/dev/i2c0`, and `/dev/pwm0` remain available.
- Stage 2 I2S allocation is I2S0 MIC RX and I2S1 speaker TX.
- Wi-Fi, Bluetooth, robotctl, and servo application are disabled in this build.

## Stage 3 Changes

- Standard NuttX SSD1306 I2C board adapter in `board/contest_board/src/board_oled.c`.
- I2S board accessors in `board/contest_board/src/board_voice_i2s.c`.
- `app/voice_echo/` NSH builtin for bounded raw PCM record/playback.
- Manifest linkfile and board defconfig updates.

## Hardware/Profile

- OLED: SSD1306-compatible, 128x64, I2C0 SDA12/SCL13, default address `0x3c`, 400 kHz.
- MIC: I2S0 RX, BCLK16/WS17/DIN18, 16 kHz, mono, provisional 16-bit/16-bit slot.
- Speaker: I2S1 TX, WS38/BCLK39/DOUT40, 24 kHz, mono, provisional 16-bit/16-bit slot.
- GPIO14 is push-to-talk. Maximum recording length is 3 seconds.

## Architecture

`voice_echo` obtains the board-owned I2S lower halves, configures them through
the public asynchronous `i2s_dev_s` API, waits on DMA callbacks, and stores
PCM in RAM. A simple 16 kHz to 24 kHz nearest-neighbor conversion feeds TX.
OLED status updates are non-critical; failures print `OLED unavailable` and do
not stop audio.

## Configuration/Build

- `CONFIG_LCD_SSD1306_CUSTOM=y`, `CONFIG_LCD_SSD1306_I2C=y`, address 60, 400000 Hz.
- `CONFIG_ESP32S3_I2S0_SAMPLE_RATE=16000` and `CONFIG_ESP32S3_I2S1_SAMPLE_RATE=24000`.
- `CONFIG_LVX_USE_DEMO_CONTEST2026_295_VOICE_ECHO=y`.
- Wi-Fi and robotctl are disabled.

```bash
cd ~/vela/openvela
source myenv/bin/activate
contest2026_295_suanliheidong/board/contest_board/scripts/build_with_hal_backport.sh -j8
python -m esptool --chip esp32s3 image-info nuttx/nuttx.bin
```

## Verification

- `-j8` build: PASS.
- `nuttx.bin` image-info: PASS; ESP32-S3, 16MB, DIO, 40m, valid checksum, load addresses `0x3fc...` and `0x403...`.
- HAL and temporary NuttX BREAK backports are reverted by the wrapper.
- OLED visible output, MIC capture, speaker playback, and full voice echo are hardware-test pending.

## First Hardware Test

```text
nsh> help
nsh> voice_echo --once
nsh> voice_echo
```

Use conservative speaker power and stop testing if the audio hardware heats,
clips, or behaves unexpectedly. Do not start PWM or flash/erase from the app.
