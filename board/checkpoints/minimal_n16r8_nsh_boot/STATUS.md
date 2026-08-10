# ESP32-S3-WROOM-1-N16R8 Minimal NSH Boot

Stage: ESP32-S3-WROOM-1-N16R8 Minimal NSH Boot

Date: 2026-08-10

Result:

- BUILD PASS
- IMAGE PASS
- HARDWARE BOOT PASS
- NSH PASS

Base HAL: `9fc713a95b1ff150dd0b0647e465d3c624056bb1`

Backport: `454d82c70ed` (`Change lock constant defines with SP macro`)

Build command:

```text
contest2026_295_suanliheidong/board/contest_board/scripts/build_with_hal_backport.sh -j8
```

Flash address: `0x000000`

UART: 115200, TX GPIO43, RX GPIO44

Disabled: Wi-Fi / BT / LCD / Camera / Audio

PSRAM: 8MB Octal/OPI configured; runtime stress test not yet verified.

Revisions:

- contest: `2909acd725cf4e8ad62a3f4baf734bf259ba97c2`
- nuttx: `76354c637858ecb0aa4601629327acb6f44a26bb`
- esp-hal-3rdparty: `9fc713a95b1ff150dd0b0647e465d3c624056bb1`
- vendor: `432dfd23a01d9f9ac772bd2c12668400fedaac6b`
- packages: `b2d8f323be31c58699a2cec9f2eea858fe0501bd`

Tracked-source audit at freeze:

- HAL diff: empty
- vendor diff: empty
- packages diff: empty
- nuttx: 22 pre-existing tracked changes; not modified by this stage freeze
