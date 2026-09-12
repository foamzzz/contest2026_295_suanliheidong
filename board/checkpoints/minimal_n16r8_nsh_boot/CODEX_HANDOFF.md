# Project

Workspace: `~/vela/openvela`

Contest repo: `~/vela/openvela/contest2026_295_suanliheidong`

GitHub repository: `https://github.com/foamzzz/contest2026_295_suanliheidong.git`

Current branch and checkpoint branch: `checkpoint/minimal-n16r8-nsh`

Contest HEAD: `d31d5e6c9897b639d8b78d271149a7b6bec41f34`

Remotes: `origin` is the GitHub repository; `openvela` is the Gitee remote
`git@gitee.com:open-vela/contest2026_295_suanliheidong`.

The manifest maps `board/contest_board` to the symlinked
`vendor/openvela/boards/contest2026_295_board`; do not maintain a second copy.

# Hardware

Module: ESP32-S3-WROOM-1-N16R8

- Flash: 16MB
- PSRAM: 8MB Octal / OPI
- UART0 TX: GPIO43
- UART0 RX: GPIO44
- UART0 baud: 115200

Hardware is verified through ROM boot → NuttX boot → NSH:
`*** Booting NuttX ***`, `NuttShell (NSH)`, `nsh>`.

# Minimal Baseline Status

- N16R8: PASS
- Flash 16MB: PASS
- PSRAM 8MB Octal: CONFIGURED; runtime stress test NOT FULLY RUNTIME VERIFIED
- UART0 + NSH: PASS
- `-j8` build: PASS
- `nuttx.bin`: PASS
- Hardware boot: PASS
- Wi-Fi / Bluetooth / LCD / Camera / Audio: DISABLED

# Current NSH Runtime

Verified on hardware: `help`, `uname -a`, `ls /dev`, `echo`, and UART0 I/O.
`/dev` includes `console`, `ttyS0`, `null`, and `zero`.

`free` and `ps` are NOT ENABLED. Their `command not found` result is expected
for this minimal configuration, not a boot failure.

# Python Environment

```bash
cd ~/vela/openvela
if [ ! -d myenv ]; then python3 -m venv myenv; fi
source myenv/bin/activate
python --version
python -m esptool version
```

Validated esptool: `v5.3.1`. Do not use `/home/foam/.local/bin/esptool.py`.

# Correct Build Procedure

Use the wrapper, not ordinary `build.sh` directly:

```bash
cd ~/vela/openvela
source myenv/bin/activate
contest2026_295_suanliheidong/board/contest_board/scripts/build_with_hal_backport.sh -j8
```

For a log:

```bash
set -o pipefail
contest2026_295_suanliheidong/board/contest_board/scripts/build_with_hal_backport.sh \
  -j8 2>&1 | tee ~/contest-board-build.log
```

The wrapper activates `myenv`, verifies/applies the HAL backport, builds the
custom board, and reverts the patch on success or failure.

Artifacts: `nuttx/nuttx` (ELF), `nuttx/nuttx.bin` (flash image),
`nuttx/nuttx.hex` (Intel HEX).

# HAL Compatibility Backport

HAL base: `9fc713a95b1ff150dd0b0647e465d3c624056bb1`

Upstream fix: `454d82c70ed`, `Change lock constant defines with SP macro`.

Affected files: `components/esp_hw_support/clk_ctrl_os.c` and
`components/esp_hw_support/modem_clock.c`. They define
`LOCK_INITIALIZER_UNLOCKED 0`, incompatible with the current NuttX
`spinlock_t`; the backport changes it to `SP_UNLOCKED`.

Board-local mechanism:

- `board/contest_board/patches/0001-esp-hal-nuttx-lock-initializer.patch`
- `board/contest_board/scripts/esp_hal_lock_backport.sh`
- `board/contest_board/scripts/build_with_hal_backport.sh`

Flow: verify base → temporary apply → build → automatic revert → HAL clean.
Do not upgrade the whole HAL: newer layouts do not match current NuttX
`hal.mk`. Do not directly modify HAL tracked source.

**DO NOT REMOVE WITHOUT A VALID UPSTREAM-COMPATIBLE SOLUTION.**

# Critical esptool Fix

The former wrapper injected `--use_segments` / `--use-segments` into
`elf2image`. Esptool then treated ELF Program Header PhysAddr values
`0x20`, `0x10000`, and `0x20000` as ROM load addresses, causing
`StoreProhibited` with `EXCVADDR=0x00000020`.

Current `scripts/esptool.py` injects neither flag. It runs
`sys.executable -m esptool` from the active virtualenv and preserves
`--ram-only-header`. **NEVER REINTRODUCE `--use_segments` FOR THIS IMAGE FLOW.**

# Boot Image Model

NuttX Simple Boot, one image: `nuttx.bin`; flash offset `0x000000`;
Flash 16MB, DIO, 40m. Do not add ESP-IDF `bootloader.bin`, partition table,
or OTA data.

# Image Validation

```bash
cd ~/vela/openvela
source myenv/bin/activate
python -m esptool --chip esp32s3 image-info nuttx/nuttx.bin
```

Validated: Entry `0x40374f2c`; DRAM segment `0x3fc8b840`; IRAM segment
`0x40374000`; Flash 16MB/DIO/40m; checksum valid. Valid RAM segments must be
`0x3fc...` or `0x403...`; reject `0x00000020` as a ROM load segment.

# Flash Procedure

Only after image-info passes:

```bash
cd ~/vela/openvela
source myenv/bin/activate
python -m esptool --chip esp32s3 --port /dev/ttyUSB0 --baud 460800 \
  write-flash --flash-size 16MB --flash-mode dio --flash-freq 40m \
  0x000000 nuttx/nuttx.bin
```

# UART Wiring

- USB-TTL RXD → GPIO43
- USB-TTL TXD → GPIO44
- USB-TTL GND → board GND
- Do not connect USB-TTL 3V3/5V to the board.
- Use an independent stable 5V supply.

USB-TTL board power previously caused `BROWNOUT_RST`; do not restore it.

# Known Non-blocking Boot Message

Simple Boot may print `SHA-256 comparison failed` and `Attempting to boot
anyway...`; hardware then reaches NuttX and `nsh>`. Treat this as non-blocking
for this baseline; do not add an ESP-IDF bootloader to suppress it.

# Modification Boundaries

Long-term board maintenance is limited to:
`contest2026_295_suanliheidong/board/contest_board/**`.

Do not directly maintain tracked changes in `nuttx/**`, `vendor/**`,
`packages/**`, or HAL. Read them for reference only; do not patch arch/HAL for
quick compilation.

# Important Board-specific Change

`src/board_bringup.c` returns `0` on success. Do not change it to `return OK;`
unless a verified header supplies `OK` and the change is necessary.

# Minimal Baseline Files

- `board/contest_board/README.md` — build and validation guide
- `board/contest_board/configs/nsh/defconfig` — minimal configuration
- `board/contest_board/src/board_boot.c` — empty board hooks
- `board/contest_board/src/board_bringup.c` — successful no-op bring-up
- `board/contest_board/src/board_appinit.c` — optional boardctl hook
- `board/contest_board/src/contest_board.h` — bring-up prototype
- `board/contest_board/patches/0001-esp-hal-nuttx-lock-initializer.patch` — backport
- `board/contest_board/scripts/esp_hal_lock_backport.sh` — apply/revert guard
- `board/contest_board/scripts/build_with_hal_backport.sh` — official build entry
- `board/contest_board/scripts/esptool.py` — active-venv esptool launcher
- `board/contest_board/scripts/Make.defs` — linker/toolchain settings
- `board/contest_board/include/board.h` — board include surface

# Next Stage

Stage 2 - Peripheral Bring-up. Read `docs/hardware_work.md` before changes.
Resources to investigate, not implement in this checkpoint:

- GPIO4 LED; GPIO14 touch/button; GPIO0 boot button
- I2C0: SDA GPIO12, SCL GPIO13, 400kHz
- Servo PWM/LEDC: GPIO9, GPIO10, GPIO21, GPIO47, GPIO48 at 50Hz
- MIC I2S: BCLK GPIO16, WS GPIO17, DIN GPIO18
- Speaker I2S: WS GPIO38, BCLK GPIO39, DOUT GPIO40

Reference boards: `nuttx/boards/xtensa/esp32s3/esp32s3-devkit`,
`esp32s3-box`, `esp32s3-eye`, and `esp32s3-korvo-2`.

Prioritize driver/resource/register adaptation. Do not immediately implement
OLED graphics, servo motion, recording, playback, Wi-Fi, Bluetooth, or app
logic.

# Unknown Hardware Details

`hardware_work.md` does not establish the OLED controller/address, exact
microphone chip, or speaker amplifier/codec. Do not guess SSD1306, SH1106,
INMP441, MAX98357, ES8311, or another part number. Start only with verified
GPIO, I2C, PWM, or I2S resources.

# New Session Rules

1. Read this file first, then `docs/hardware_work.md`.
2. Check contest branch and Git status.
3. Treat Minimal NSH as known-good; do not re-investigate HAL, esptool, boot,
   or NSH history unless live facts disagree.
4. Create a new peripheral branch before Stage 2; do not develop directly on
   `checkpoint/minimal-n16r8-nsh`.
5. Do not add features to this checkpoint.

# Git / Checkpoint

- Checkpoint: `board/checkpoints/minimal_n16r8_nsh_boot`
- Branch: `checkpoint/minimal-n16r8-nsh`
- Contest HEAD: `d31d5e6c9897b639d8b78d271149a7b6bec41f34`
- NuttX HEAD: `76354c637858ecb0aa4601629327acb6f44a26bb`
- HAL HEAD: `9fc713a95b1ff150dd0b0647e465d3c624056bb1`
- Future branch suggestion: `feature/peripheral-bringup` (do not create here)
