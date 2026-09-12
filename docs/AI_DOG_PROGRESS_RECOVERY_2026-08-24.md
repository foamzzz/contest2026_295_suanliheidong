# AI-DOG Progress Recovery Handoff

**Generated:** 2026-08-24
**Workspace:** `/home/foam/vela/openvela`
**Scope:** recovery summary for the broken Codex task `01a019eb-4485-7d33-b8eb-c6f86033613d`.

This is a continuation handoff, not an authorization for broad cleanup. It
combines the last complete project handoff (2026-08-22), the locally preserved
Codex session log, and a read-only current-worktree check on 2026-08-24.

## Read Order for a New Task

1. Read this file completely.
2. Read `docs/AI_DOG_CODEX_HANDOFF_2026-08-22.md` completely for the full
   evidence trail and preserved-artifact inventory.
3. Inspect, but do not reset, `packages/ai_agent` with:

   ```bash
   git -C packages/ai_agent status --short
   ```

4. Before any build or patch application, record the package and board Git
   baselines. Preserve all existing user changes.

The original local record remains available at:

```text
/home/foam/.codex/sessions/2026/08/19/
rollout-2026-08-19T20-07-24-01a019eb-4485-7d33-b8eb-c6f86033613d.jsonl
```

Do not try to repair the Codex pagination lineage by editing local Codex state.
Use this handoff and the preserved JSONL as read-only evidence.

## Platform and Protected Boundaries

- Board: ESP32-S3-WROOM-1-N16R8, 16 MB flash, 8 MB Octal/OPI PSRAM.
- Keep hardware-specific adaptation under
  `contest2026_295_suanliheidong/board/contest_board/`.
- The following are hardware-validated and out of scope for the AI crash
  investigation: UART0/NSH, robotctl, five-servo PWM, SSD1306 OLED, Wi-Fi
  association/DHCP, microphone/I2S0, speaker/I2S1, and `voice_echo` loopback.
- Do not modify I2S, `contest_i2s.c`, PWM, Wi-Fi driver, PSRAM/flash topology,
  SMP, scheduler, brownout handling, HAL/Xtensa compatibility patches, or
  boot/image generation while investigating the AI heap failure.
- WebSocket remains `SKIPPED`; it is not required for the tested `ask` path.

## Confirmed AI-Agent Progress

### B1/B2 memory-placement experiment

The decisive A/B has already been completed on hardware.

| Variant | Large-buffer placement | Result |
| --- | --- | --- |
| B1 | Original allocation policy | Reproduced the CPU1 failure during HTTP pressure. Internal free about 29 KB; largest block about 16 KB. |
| B2 | Five Agent buffers in PSRAM | Passed plaintext HTTP pressure to sequence 20, canaries, cleanup checks, and 120 s idle. Internal free about 56 KB; largest block about 53 KB. |

The B2 buffers and sizes are fixed for this investigation:

```text
ctx   8192 bytes
hist  8192 bytes
tool  8192 bytes
pool0 16384 bytes
pool1 16384 bytes
```

They were observed in PSRAM in the validated path. The contest-local B2
allocation approach was deliberately carried through B3/B3.1. Do not refactor
or replace it while isolating the crash.

### Functional restoration

B3 restored the real message bus, AgentLoop, tool JSON, LLM router, LLM
request/response, response parsing, and outbound path while retaining B2 PSRAM
placement.

The historical B3.1 path proved all of the following on hardware:

```text
producer push rc=0
consumer pop rc=0
[ASK-DIAG] inbound
router/DNS/TCP/TLS handshake/HTTP 200/response JSON parse PASS
valid 169-byte MiMo response PASS
```

The earlier B3.1 watchdog report was a false timeout caused by a wall-clock
correction during TLS. This establishes functional reachability, not that every
subsequent image is crash-free.

### Non-baselines

- B3.2-M (monotonic-only) and B3.2-W (WLAN-only) both crashed. They are not the
  development baseline because they also changed timing/layout/diagnostics.
- M1 watchdog-local also crashed. It must not be used to disprove the B3.1
  request-construction path.
- Button cleanup is not the AI crash cause: the preserved B3.1 image also
  crashes.

## Primary Fault Evidence

The meaningful primary evidence is CPU0 `agent_loop`, not the later CPU1-IDLE
recursive panic.

```text
PC        0x42077d9b (a later report rounded this to 0x42077d97)
EXCCAUSE  0x1d StoreProhibited
EXCVADDR  0x00020524
A3        0x0002051c
```

The exact ELF decoded this as `mm_malloc.c:260`:

```c
node->blink->flink = node->flink;
```

`EXCVADDR == A3 + 8`, consistent with a corrupt/inconsistent USR_HEAP
free-list link. The cJSON call is the first observed allocator trigger, not
proof that cJSON itself created the corruption.

Recovered path:

```text
agent_loop_task
  -> run_react_loop
  -> agent_loop_llm_chat_tools
  -> llm_chat_tools
  -> build_openai_tools_array()
  -> cJSON_CreateArray()
  -> cJSON_New_Item()
  -> malloc()
  -> mm_malloc()
```

The v2/v3 allocation-free heap gate is positioned after the tools JSON was
parsed and before `cJSON_CreateArray()`. A `TOOLS_PARSED PASS` proves only that
the locked check instant was coherent; after unlock another task can change the
heap before the next allocation.

## Ruled Out or Explicitly Deprioritized

- API-key validity is not causal: a syntactically nonempty fake key still
  reaches the crash path; a wrong URL avoids later transport work and exits
  earlier.
- TLS/LLM should not be treated as the sole root cause. Plain HTTP can return
  HTTP 200, clean up DNS/socket/raw buffers, then later trigger the secondary
  CPU1 panic.
- Do not resume ordinary HTTP body malloc/free leak hunting, A2e allocation
  size sweeps, WebSocket work, MiMo/API-key theory, or button/I2S branches.
- Do not modify cJSON, allocator behavior, heap topology, Wi-Fi, TCP/lwIP,
  TLS, HTTP, SMP, interrupt handling, CPU affinity, or brownout as a
  speculative repair.

## Current 2026-08-24 Worktree Snapshot

`packages/ai_agent` is intentionally dirty. The observed modified files are:

```text
include/llm/llm_cache.h
src/core/agent_loop.c
src/core/agent_trace.c
src/core/agent_trace.h
src/llm/llm_cache.c
src/llm/llm_parse.c
src/llm/llm_proxy.c
```

Do not reset, checkout, or overwrite them. Establish ownership and compare
their content with the local contest patches before applying anything else.

### Patch 0018: created, not applied or built

Path:

```text
contest2026_295_suanliheidong/board/contest_board/patches/
0018-ai-agent-tools-array-alloc-gate.patch
```

SHA256 on 2026-08-24:

```text
9da0aad77cec221c2f8c03dcbce5b290b1864f5a7406c4c3148df293e6396b39
```

It changes only `packages/ai_agent/src/llm/llm_parse.c`, adding one line of
diagnostic output immediately after `agent_heap_gate_tools_parsed()` and before
`cJSON_CreateArray()`:

```text
[ALLOC-DIAG] tools_array before heap=<ptr> size=<n> ra=<ptr>
```

Read-only verification on 2026-08-24 established:

```text
git -C packages/ai_agent apply --check <absolute-0018-path>: PASS
git -C packages/ai_agent diff --check: PASS
current llm_parse.c does not contain [ALLOC-DIAG]
```

No build, image-info validation, ELF/BIN capture, flash, or hardware run has
been performed for 0018. This is a diagnostic probe, not a fix.

### Patch 0019: designed only, not implemented

Target name:

```text
0019-ai-agent-oled-response-scroll.patch
```

No 0019 patch file exists yet. The previous task ended before implementation.

Confirmed OLED path:

```text
SSD1306, I2C0, GPIO12 SDA, GPIO13 SCL, address 0x3c,
128x64, 8 pages, 400 kHz
```

Relevant board API is `board_oled_initialize()` / `board_oled_getdev()` in the
contest board. `voice_echo` currently uses the OLED synchronously with an
ASCII-only font; AI Agent has no OLED path.

The final Agent response flow is:

```text
agent_loop.c -> dispatch_response() -> message_bus_push_outbound()
agent_main.c -> outbound_dispatch_task() -> CLI printf("[Agent]: %s")
```

Required 0019 design boundary:

- Enqueue only a completed LLM reply from outbound dispatch; do not add token
  streaming or touch request/response transport.
- Use static or fixed-size bounded storage and an independent worker/queue.
  The response-dispatch path must not allocate for OLED rendering or block on
  I2C.
- Wrap for the 128-pixel display, refresh all eight pages, and scroll long
  text upward.
- Render supported ASCII only. Convert non-ASCII input to spaces and emit one
  diagnostic, not a log per character.
- OLED initialization/render failures disable OLED only; they must not affect
  CLI, AgentLoop, LLM, or the message bus.
- Permit only concise diagnostics: `[OLED-DIAG] enqueue len=<n>` and
  `[OLED-DIAG] render page=<n>`.
- Do not expose API keys, request bodies, or other sensitive configuration.
- Do not reuse the LVGL UI channel, which has a different dynamic-payload,
  asynchronous LVGL model.

Expected 0019 file scope, subject to source confirmation:

```text
packages/ai_agent/src/ui/agent_oled.c
packages/ai_agent/src/ui/agent_oled.h
packages/ai_agent/src/agent_main.c
packages/ai_agent/CMakeLists.txt
```

## Last Explicit User Request (Unfinished)

Complete these phases in order; do not flash.

### Phase 1: validate 0018

1. Re-run `git apply --check` using the correct absolute patch path.
2. Confirm the patch changes only `llm_parse.c`.
3. Apply 0018. Do not modify `mm_malloc`, cJSON, TLS, or HTTP.
4. Build with the established ESP32-S3/NuttX wrapper using `-j8`.
5. Validate the exact produced image with `esptool image_info` before calling
   it flashable; record exact ELF/BIN paths and SHA256.
6. Do not flash. Report the patch diff, build result, artifact checksums, and
   that the probe does not repair the confirmed pre-transport allocator fault.

### Phase 2: implement and validate independent 0019

1. Reconfirm the OLED hardware/API/font/task assumptions against current
   source before editing.
2. Implement only the completed-response OLED feature described above.
3. Produce `0019-ai-agent-oled-response-scroll.patch` as an independent
   contest-local patch.
4. Run `git apply --check`, build with `-j8`, validate exact ELF/BIN and
   SHA256, and do not flash.
5. Report modified files, actual call chain, buffer capacity, task/queue
   design, ASCII/Chinese behavior, complete patch diff, and build evidence.

## Build and Artifact Rules

- Use the existing reversible contest wrapper:
  `contest2026_295_suanliheidong/board/contest_board/scripts/build_with_hal_backport.sh`.
- Preserve existing artifacts in
  `contest2026_295_suanliheidong/artifacts/ai_agent_stabilization/`; never
  overwrite B1, B2, B3, B3.1, or B3.2 evidence.
- Package source must be restored to the agreed clean state after a wrapper
  build, while contest-local patches and artifacts remain preserved.
- Use installed esptool underscore syntax: `image_info`, not `image-info`.
- A successful compilation is not hardware validation and does not establish
  that the heap issue is solved.

## Existing High-Value Artifacts

The artifact directory includes the B1/B2/B3/B3.1/B3.2 ELF/BIN pairs and
associated selected manifests/checksums. Important examples:

```text
nuttx-b1-heap-topology.{bin,elf}
nuttx-b2-psram-diagnostic.{bin,elf}
nuttx-b3-psram-functional.{bin,elf}
nuttx-b3.1-tools-heap-gate-v2.{bin,elf}
nuttx-b3.2M1-watchdog-local.{bin,elf}
```

Always decode a new exception using the exact matching ELF. Do not substitute
the CPU1 recursive panic frame for the original CPU0 exception frame.

## Completion Criteria for the Next Task

The immediate target is not a root-cause claim. It is a reproducible 0018
diagnostic build and a separately reviewable 0019 OLED patch, each with an
exact artifact identity and no flash. Any new hardware evidence must be logged
as PASS/FAIL with the image checksum, full first exception frame if present,
and the last meaningful diagnostic line.
