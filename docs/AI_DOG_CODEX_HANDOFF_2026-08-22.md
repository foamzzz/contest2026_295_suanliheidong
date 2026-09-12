# ESP32-S3 openvela AI Agent Stabilization Handoff

**Date:** 2026-08-22
**Workspace:** `/home/foam/vela/openvela`
**Contest project:** `/home/foam/vela/openvela/contest2026_295_suanliheidong`

This document records the current confirmed state, preserved artifacts, ruled-out paths, and the next diagnostic boundary. It is a handoff, not a request to apply any further change.

## 1. Hardware and board baseline

- Module: `ESP32-S3-WROOM-1-N16R8`
- Flash: 16 MB
- PSRAM: 8 MB Octal/OPI
- Board-specific GPIO mapping is retained in `contest_board`.
- GPIO14 is NC/unused.
- GPIO0 is BOOT/strap only.
- No application button is required; voice recording is command-driven.
- `contest_board` must remain as a thin PCB adaptation layer. Do not replace it with `esp32s3-devkit`.

The official ESP32-S3/N16R8 foundation is the intended platform layer: SoC support, UART, GPIO, Wi-Fi, PSRAM, Flash, I2C, LEDC/PWM, network, TLS, SMP, and common board APIs. Board code should retain only real pin mapping, registration, bring-up, and proven workarounds.

## 2. Stable functions and protected subsystems

The following have reached hardware-valid operation and must not be changed casually:

- UART0/NSH and USB-TTL flashing
- `robotctl`
- PWM and five servos
- SSD1306 OLED over I2C
- Wi-Fi association/DHCP
- INMP441 microphone / I2S0 RX
- MAX98357 speaker / I2S1 TX
- `voice_echo` 5-second record/playback
- PSRAM use
- `ai_agent` startup, message bus, AgentLoop, TLS, HTTP, JSON, and LLM response on the historical B3.1 path

Voice regression evidence after button cleanup:

```text
RX submitted=159 completed=159 underrun=0
samples=80000 bytes=160000
TX submitted=118 completed=118 underrun=0
playback elapsed=5000 ms
```

Do not modify I2S, `contest_i2s.c`, PWM, OLED, Wi-Fi driver, PSRAM, Flash, SMP, HAL/Xtensa compatibility patches, or boot/image generation while isolating the AI crash.

## 3. Official reuse audit conclusions

The official ESP32-S3 lower-half exists and covers GPIO matrix, clock/reset, I2S0/I2S1, RX/TX DMA, interrupts, workers, data width, and fractional clock configuration. The official common board layer registers I2S devices through `esp32s3_i2sbus_initialize()`.

`board_voice_i2s.c` is thin board glue, but `contest_i2s.c` is a 2121-line contest-local lower-half that directly owns registers, GDMA, IRQs, cache maintenance, raw DMA buffers, ping-pong chaining, INMP441 slot conversion, MAX98357 mono handling, diagnostics, and underrun tracking. It is classified as a proven functional workaround/duplicate lower-half, not ordinary glue. It must remain until an isolated official-driver A/B proves equivalent gapless RX/TX behavior.

Other audit findings:

- `board_oled.c` correctly uses official I2C and SSD1306 APIs.
- `board_buttons.c` previously registered GPIO14 and GPIO0 despite the actual hardware having no application button. This was an invalid board mapping.
- Button cleanup confirmed `/dev/buttons` absent, GPIO14/GPIO0 no longer registered, `robotctl` PASS, `voice_echo` PASS, and AI startup/Wi-Fi association still worked.
- `board_late_initialize()` drops `contest_board_bringup()` errors; this is a recorded review issue, not changed in the AI investigation.
- Several contest I2S `work_queue()` return values are ignored; this remains a review finding.
- Contest I2S `curbyte` bounds checks and DMA/ISR ownership remain review risks, but are outside the current AI task.

The later result that the preserved golden B3.1 image also crashes proves the button cleanup is not the underlying AI crash cause.

## 4. AI Agent stabilization history

### B1/B2 memory placement experiment

Original B1 allocation policy reproduced CPU1 faults during HTTP pressure:

```text
internal free=29128
largest=16776
psram free=8372200
```

B2 placed the five large Agent buffers in PSRAM and passed plaintext HTTP pressure, 20 sequences, canary checks, cleanup checks, and 120-second idle:

```text
internal free approximately 56 KB
internal largest approximately 53 KB
psram free approximately 8.38 MB
```

The five buffers are:

```text
ctx   8192   -> PSRAM
hist  8192   -> PSRAM
tool  8192   -> PSRAM
pool0 16384  -> PSRAM
pool1 16384  -> PSRAM
```

Observed pointers in the validated placement path:

```text
ctx   0x3c130010
hist  0x3c132018
tool  0x3c134020
pool0 0x3c136028
pool1 0x3c13a030
```

The B2 contest-local allocation method was intentionally preserved through B3/B3.1. It must not be refactored as part of the current crash investigation.

### B3 functional restoration

B3 restored the real message bus, AgentLoop, tool JSON, router, LLM request/response, response parsing, and outbound path while keeping B2 PSRAM placement. WebSocket remains `SKIPPED`.

### B3.1 message-bus gate

B3.1 added the build identity and minimal bus diagnostics. Hardware proved:

```text
producer push rc=0
consumer pop rc=0
[ASK-DIAG] inbound
```

The same B3.1 path also proved:

```text
router PASS
DNS PASS
TCP PASS
TLS handshake PASS
HTTP 200 PASS
response JSON parse PASS
valid 169-byte MiMo text response PASS
```

The historical B3.1 issue was a false watchdog timeout caused by wall-clock adjustment during TLS:

```text
start RTC before correction
TLS forced RTC to 2026
end RTC after correction
watchdog reported approximately 2748588748 ms
```

This proves B3.1 reached the real response path; it does not prove that every current B3.1 image is crash-free.

### B3.2 regression

B3.2 combined monotonic timing changes, local `uint64_t` timing/layout changes, WLAN startup sanitize, and additional diagnostics. Both minimal variants crashed:

```text
B3.2-M monotonic-only: CRASH
B3.2-W wlan-only:      CRASH
```

Both stopped after request construction/instrumentation in some runs, so B3.2 is not the development baseline. `0x42098601` was decoded as `up_saveusercontext()` panic-context code, not the original request-builder fault.

### M1 watchdog-local attempt

`nuttx-b3.2M1-watchdog-local.bin` was tested and still produced an agent-loop primary exception followed by CPU1 secondary panic. Do not treat this as evidence against B3.1 request construction; the baseline and artifact history are separate.

## 5. Exception and heap evidence

### Raw exception frame previously decoded

One exact ELF decode located:

```text
PC        = 0x42077d9b
EXCCAUSE  = 0x1d (StoreProhibited)
EXCVADDR  = 0x00020524
A3        = 0x0002051c
```

The fault instruction was:

```asm
s32i.n a9, a3, 8
```

in:

```text
mm_malloc()
nuttx/mm/mm_heap/mm_malloc.c:260
node->blink->flink = node->flink;
```

`EXCVADDR == A3 + 8` exactly. The observed bad `node->blink` value demonstrated USR_HEAP free-list metadata corruption/free-list inconsistency. This is not a reason to add allocator tolerance or modify `mm_malloc`.

### Other primary/secondary distinction

CPU1 IDLE `Exception 2` is generally a secondary recursive panic after a CPU0 `agent_loop` primary exception. Do not use the CPU1 idle panic PC as the root cause without a complete first exception frame.

The recovered B3.1 call chain included:

```text
agent_loop_task
 -> run_react_loop
 -> agent_loop_llm_chat_tools
 -> llm_chat_tools
 -> build_openai_tools_array (llm_parse.c)
 -> cJSON_Duplicate
 -> cJSON_New_Item
 -> malloc
```

The `cJSON_Delete()` free-list crash is treated as cleanup after an earlier cJSON allocation/duplication failure or heap corruption, not automatically as the root cause.

## 6. cJSON/source audit conclusions

`build_openai_tools_array()` parses `tools_json` into `tools_spec`. Each `schema` is a borrowed child of `tools_spec`; `cJSON_Duplicate(schema, 1)` creates an independent recursive copy. `tools_spec` is later owned and deleted by the builder after the converted array is complete.

Actual `cJSON_Duplicate()` failure paths in the checked source are:

- null input item
- `cJSON_New_Item()` allocation failure
- `valuestring` duplicate allocation failure
- item-name/string duplicate allocation failure
- recursive child duplication failure

Recursive failure enters `fail`, calls `cJSON_Delete(newitem)`, then eventually `free()`/`mm_free()`.

The cJSON allocator hooks resolve to the project allocator path (`malloc`/`free`) unless explicitly initialized otherwise; no API-key-specific local validation exists. The nonempty fake-key experiment confirms that authentication is not required to reach the crash.

The existing builder has unchecked cJSON return values, but no source proof of a specific double-free/UAF has been established. Do not change cJSON, shallow-copy tools, skip cleanup, or disable tools as a speculative fix.

## 7. Heap checker work

Existing `mm_checkcorruption()`/`umm_checkcorruption()` were not directly usable because `CONFIG_DEBUG_MM` is disabled and the experiment forbids ASSERT-style checker behavior.

A contest-local, allocation-free heap gate was added around the parsed tools boundary:

```text
tools_spec parsed and verified as an array
[HEAP-GATE] TOOLS_PARSED
cJSON_CreateArray()
tool iteration and recursive duplication
```

The first checker version reported:

```text
[HEAP-GATE] TOOLS_PARSED FAIL
reason=4 region=0 node=0x3fcaf658 flink=0 blink=0 size=32
```

Correctness audit found that version to be a false-positive candidate because it unconditionally compared `node->preceding` against the previous size. Current NuttX semantics are:

```text
MM_ALLOC_BIT    = 0x1
MM_PREVFREE_BIT = 0x2
MM_MASK_BIT     = 0x3
MM_SIZEOF_NODE(node) = node->size & ~MM_MASK_BIT
```

`node->preceding` is meaningful only when `MM_PREVNODE_IS_FREE(node)` is true. The v2 checker correction maintains `prev_node`, `prev_size`, and `prev_is_free`; checks `PREVFREE_FLAG` first; checks `PRECEDING_SIZE` only when the previous-free flag is set; validates ranges before dereference; holds the allocator lock; does not allocate/free or print while locked; bounds traversal; and stops at the first failure.

No runtime validation result proving `TOOLS_PARSED PASS` or a newly identified corruption point has been recorded for v2 yet.

## 8. URL and API-key experiment

Latest real-device observation:

```text
correct URL + real key -> ask crashes
correct URL + fake key -> ask still crashes
wrong URL -> ask exits early without the crash
```

Source control flow:

- `cmd_set_llm()` accepts a syntactically formed URL, extracts host/port/path, copies model/key into `llm_backend_t`, and applies it. It does not perform DNS, TCP, TLS, or key validation.
- Router selection checks enabled/nonempty host and backoff state only. It does not resolve DNS or validate credentials.
- `llm_chat_tools()` rejects only an empty key. A nonempty fake key proceeds.
- `llm_http_direct()` snapshots the key as a normal string and formats `Authorization: Bearer <key>`.
- `vela_https_post_json()` then enters DNS/TCP/TLS/HTTP. A syntactically valid non-resolving host should fail at DNS or TCP before TLS/HTTP.

The most likely interpretation is path avoidance: a wrong host/port exits before later transport allocation, connection-pool, TLS teardown, or response activity that is involved in triggering the existing heap corruption. It does not indicate that the wrong URL fixes the heap or that the API key is causal.

Existing `LLM-GATE`, `TLS-PROBE`, and `ASK-DIAG` logs are sufficient; no new instrumentation is currently required.

## 9. Artifact inventory and patch boundaries

Important preserved artifacts under `artifacts/ai_agent_stabilization/` include:

```text
nuttx-b1-heap-topology.{bin,elf}
nuttx-b2-psram-diagnostic.{bin,elf}
nuttx-b3-psram-functional.{bin,elf}
nuttx-b3.1-message-bus-gate.{bin,elf}
nuttx-b3.1-rx-rawexc.{bin,elf}
nuttx-b3.1-tools-heap-gate.{bin,elf}
nuttx-b3.1-tools-heap-gate-v2.{bin,elf}
nuttx-b3.2-monotonic-wlan-init.{bin,elf}
nuttx-b3.2M-monotonic-only.{bin,elf}
nuttx-b3.2W-wlan-only.{bin,elf}
nuttx-b3.2W-rawexc1.{bin,elf}
nuttx-b3.2M1-watchdog-local.{bin,elf}
nuttx-llm-r1-b31-functional.{bin,elf}
```

Important contest-local patches include:

```text
0003-ai-agent-startup-and-request-guards.patch
0004-ai-agent-psram-diagnostics.patch
0005-ai-agent-b3-functional-restoration.patch
0006-ai-agent-b3.1-message-bus-gate.patch
0007-ai-agent-b3.2-monotonic-wlan-init.patch
0009-b3.2M-monotonic-only.patch
0010-b3.2W-wlan-only.patch
0011-nuttx-xtensa-raw-exception-frame.patch
0012-b3.2M1-watchdog-local.patch
0013-ai-agent-tools-heap-gate.patch
0014-ai-agent-tools-heap-gate-v2.patch
```

The wrapper applies contest-local AI/NuttX diagnostics for a build and restores package/NuttX temporary state afterward. Do not use destructive Git restore operations because the board files contain pre-existing user changes.

## 10. Current constraints

Do not currently:

- modify cJSON logic or allocator behavior
- add allocator tolerance, `CONFIG_DEBUG_MM`, KASAN, or broad heap logging
- modify TLS, HTTP, DNS, TCP/lwIP, Wi-Fi driver, message bus, AgentLoop architecture, request builder, buttons, I2S, SMP, scheduler, stack, priority, brownout, or WebSocket
- attribute the current crash to API-key validity
- use the CPU1 recursive panic context as the primary fault
- start I2S migration or broad board cleanup
- rebuild unless a new diagnostic step is explicitly authorized

## 11. Smallest next diagnostic boundary

Use the exact matching artifact for the chosen test. For the URL experiment, run identical cold-start tests with the same nonempty fake key and preserve only the existing gate sequence:

```text
[LLM-GATE] 1 router_enter
[LLM-GATE] 2 backend_selected idx=<n>
[ASK-DIAG] llm_begin
[MEM-DIAG] phase=LLM_PRE ...
[LLM-GATE] 4 proxy_enter
[LLM-GATE] 5 request_build_begin
[LLM-GATE] 6 request_build_done bytes=<n>
[LLM-GATE] 7 transport_enter
[ASK-DIAG] http send begin bytes=<n>
[TLS-PROBE] begin host=<host> port=<port>
[TLS-PROBE] dns begin
[TLS-PROBE] dns done or fail stage=dns
[TLS-PROBE] socket begin/fd=<n>
[TLS-PROBE] tcp connect ret=<n>
[TLS-PROBE] handshake ret=<n>
[ASK-DIAG] http send ret=<n>
[HTTP-DIAG] response complete status=<n>
[ASK-DIAG] llm_done rc=<n> http=<n>
```

Interpretation:

- If the wrong URL stops at DNS/TCP while the correct URL reaches TLS/HTTP and crashes, the experiment demonstrates an avoided transport path, not an API-key dependency.
- If both URLs reach the same transport gate and only one crashes, then compare host/port/path-specific allocator or connection-pool behavior without broad changes.
- If a complete first exception frame is available, decode it with the exact ELF before adding any instrumentation.

No code change is presently required for this URL/key comparison.
