# INMP441 I2S0 RX Tasks

- [x] Add named physical-format and fixed LEFT-slot constants to board-local
  headers.
- [x] Extend only I2S0 RX transaction state with raw DMA storage and logical
  byte accounting.
- [x] Configure I2S0 Philips RX for two 32-bit slots and 24 valid bits.
- [x] Calculate 1.024 MHz BCLK and program physical EOF sizing.
- [x] Convert raw LEFT-slot words in HPWORK and report logical APB length.
- [x] Emit first-transaction raw-word diagnostics.
- [x] Add per-recording logical PCM min/max/mean/RMS/peak/clip statistics.
- [x] Add active/pending/done RX transactions with independent raw buffers.
- [x] Keep RX running across normal EOF and count chained/underrun events.
- [x] Add application RX double buffering and exact final 256-byte remainder.
- [x] Add per-chunk and cross-boundary continuity diagnostics.
- [x] Document the schematic-confirmed `MIC_L/R` LOW selection.
- [ ] Run checkpatch, the controlled `-j8` build, image inspection, and
  restricted-repository status checks.
