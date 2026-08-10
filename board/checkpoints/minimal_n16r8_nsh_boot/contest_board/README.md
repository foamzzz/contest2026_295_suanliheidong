# ESP32-S3-WROOM-1-N16R8 Minimal Board

## 1. Current Stage Status

- N16R8: PASS
- Flash 16MB: PASS
- PSRAM 8MB Octal: CONFIGURED
- UART0 + NSH: PASS
- Wi-Fi: DISABLED
- Bluetooth: DISABLED
- LCD/Camera/Audio: DISABLED
- `-j8` build: PASS
- `nuttx.bin`: PASS
- Hardware boot: PASS
- `nsh>`: PASS

PSRAM runtime stress test: NOT YET VERIFIED.

This checkpoint is intentionally a minimal NSH board. Do not add optional
peripherals or application features until the next stage.

## 2. Hardware Parameters

- Module: ESP32-S3-WROOM-1-N16R8
- Flash: 16 MB
- PSRAM: 8 MB Octal/OPI
- UART0 TX: GPIO43
- UART0 RX: GPIO44
- UART baud: 115200

## 3. Environment Preparation

Run from the openvela workspace root:

```bash
cd ~/vela/openvela

if [ ! -d myenv ]; then
  python3 -m venv myenv
fi

source myenv/bin/activate
python --version
python -m esptool version
```

The build wrapper also activates `myenv`, but activating it explicitly keeps
all manual image inspection and flashing commands on esptool 5.3.1.

## 4. Correct Build Method

Do not use the ordinary `build.sh` command directly for this board. Use:

```bash
cd ~/vela/openvela
source myenv/bin/activate

contest2026_295_suanliheidong/board/contest_board/scripts/build_with_hal_backport.sh -j8
```

The wrapper verifies the HAL base, temporarily applies the upstream backport,
builds this board, and reverses the patch on both success and failure. HAL
must be clean after it exits.

To retain a build log:

```bash
set -o pipefail
contest2026_295_suanliheidong/board/contest_board/scripts/build_with_hal_backport.sh \
  -j8 2>&1 | tee ~/contest-board-build.log
```

## 5. Build Artifacts

- `nuttx/nuttx`: ELF for debugging.
- `nuttx/nuttx.bin`: image to flash.
- `nuttx/nuttx.hex`: Intel HEX image.

## 6. Image Verification

Before every flash, verify the generated image:

```bash
python -m esptool \
  --chip esp32s3 \
  image-info \
  nuttx/nuttx.bin
```

The result must show ESP32-S3, Flash size 16MB, DIO, 40m, and a valid
checksum. ROM-visible load segments must use valid DRAM/IRAM addresses such as
`0x3fc...` and `0x403...`; they must not be `0x00000020`, `0x00010000`, or
`0x00020000`.

Do not add `--use_segments` or `--use-segments` to `elf2image`.

## 7. Flashing

This board uses a NuttX Simple Boot single image. Do not add ESP-IDF
`bootloader.bin` or `partition-table.bin`.

```bash
cd ~/vela/openvela
source myenv/bin/activate

python -m esptool \
  --chip esp32s3 \
  --port /dev/ttyUSB0 \
  --baud 460800 \
  write-flash \
  --flash-size 16MB \
  --flash-mode dio \
  --flash-freq 40m \
  0x000000 \
  nuttx/nuttx.bin
```

## 8. USB-TTL Wiring

- USB-TTL RXD -> GPIO43
- USB-TTL TXD -> GPIO44
- USB-TTL GND -> Board GND
- USB-TTL 3V3/5V -> do not connect
- Board -> independent stable 5V supply

## 9. Serial Boot

```bash
picocom -b 115200 --flow n /dev/ttyUSB0
```

After power cycling the board, expect output similar to:

```text
ESP-ROM...
*** Booting NuttX ***
...
NuttShell (NSH)
nsh>
```

## 10. Minimal NSH Verification

Run:

```text
help
uname -a
uptime
dmesg
ls /
ls /dev
echo hello_openvela
```

`free` and `ps` are absent in this minimal configuration by design.

## 11. Disabled Features

- Wi-Fi disabled
- Bluetooth disabled
- LCD disabled
- Camera disabled
- Audio disabled

These features have not been ported or validated for this board.

## 12. Important Special Changes

### 12.1 Important Compatibility Patch / Upstream Backport

The NuttX integration pins `esp-hal-3rdparty` to base revision
`9fc713a95b1ff150dd0b0647e465d3c624056bb1`. That revision defines
`LOCK_INITIALIZER_UNLOCKED` as `0` in these files:

- `components/esp_hw_support/clk_ctrl_os.c`
- `components/esp_hw_support/modem_clock.c`

Current NuttX `spinlock_t` is incompatible with that integer initializer. The
board backports upstream commit `454d82c70ed` (`Change lock constant defines
with SP macro`) through:

- `patches/0001-esp-hal-nuttx-lock-initializer.patch`
- `scripts/esp_hal_lock_backport.sh`
- `scripts/build_with_hal_backport.sh`

The patch is applied only for the build and automatically reversed afterward.
Do not directly modify HAL tracked source. Do not upgrade HAL blindly: newer
HAL revisions change directory layout and do not match this NuttX `hal.mk`.
After the wrapper exits, HAL must return to the pinned base revision with an
empty tracked diff.

### 12.2 Important Image Generation Fix / Do Not Reintroduce `--use_segments`

The former board `scripts/esptool.py` injected `--use_segments` into
`elf2image`. That made esptool interpret ELF Program Header PhysAddr values
such as `0x20`, `0x10000`, and `0x20000` as ROM load addresses. The resulting
image booted with `load:0x00000020`, then failed with `StoreProhibited` and
`EXCVADDR:0x00000020`.

The wrapper now uses the active virtualenv's `python -m esptool` and leaves
esptool in its default section mode while retaining `--ram-only-header` from
the NuttX build command. Reintroducing `--use_segments` or `--use-segments`
will create a non-bootable `nuttx.bin`.

### 12.3 `board_bringup` Return Value

`src/board_bringup.c` returns `0` on success. Do not change it back to
`return OK;` unless the correct header provides `OK` and its necessity has
been verified.

## 13. Repository Modification Boundary

Allowed long-term board maintenance:

```text
contest2026_295_suanliheidong/board/contest_board/**
```

Do not directly maintain changes in HAL, NuttX tracked source, vendor tracked
source, or packages tracked source.

## 14. Current Non-Blocking Messages

The current build may report:

- `No valid Rust crates found to build`
- `noreturn function does return`
- `-Wno-atomic-alignment` is not recognized by the compiler

These messages did not block the frozen minimal NSH build.

## 15. Next Stage

Only the next stage may add:

- PSRAM runtime verification
- GPIO
- Wi-Fi
- Bluetooth
- Peripherals
- Application features

Do not mix next-stage code into this checkpoint.
