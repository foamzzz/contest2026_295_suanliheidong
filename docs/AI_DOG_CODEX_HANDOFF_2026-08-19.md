# openvela AI Dog / AI Agent Stabilization Handoff
**Date:** 2026-08-19  
**Purpose:** New Codex conversation handoff. Read this first, then wait for the user's next instruction.

---

## 0. Current executive summary

The project has reached:

- ESP32-S3 boots to NSH.
- `robotctl`, OLED, audio, Wi-Fi baseline remain usable.
- `ai_agent` builds and starts.
- AI Agent CLI appears.
- Wi-Fi can be configured from `vela>` and gets a real IPv4 address.
- `ifconfig`, `ifup`, `ifdown`, `date`, `ping`, `wapi` support are present.
- The previous `wapi show` false-failure was identified as `pclose()/waitpid()` returning `ECHILD`, not Wi-Fi failure.
- Plain HTTP can return `HTTP 200`.
- TLS previously reached successful handshake.
- The remaining blocker is an intermittent / load-sensitive ESP32-S3 SMP crash:
  `CPU1 XTENSA_DOUBLE_EXCEPTION`, usually while CPU1 IDLE is visible.
- A separate reboot-loop problem was proven to be **brownout**, and is avoided during software tests by running `robotctl off`.

Current highest-priority problem:

```text
ai_agent network activity + certain agent_loop memory/state conditions
→ successful HTTP transaction
→ cleanup completes
→ shortly afterward CPU1 double exception
```

Do not jump to LLM/product features yet.

---

# 1. Workspace / project

Workspace:

```text
~/vela/openvela
```

Contest project:

```text
contest2026_295_suanliheidong
```

Board:

```text
contest2026_295_suanliheidong/board/contest_board/
```

Important apps:

```text
contest2026_295_suanliheidong/app/robotctl/
contest2026_295_suanliheidong/app/voice_echo/
```

AI Agent package/reference:

```text
apps/packages/ai_agent/**
apps/packages/demos/ai_chat/**
apps/packages/demos/mini_memo/**
```

Before changing anything:

```bash
cd ~/vela/openvela
git status --short
git branch --show-current
```

---

# 2. Hardware / stable baseline

MCU:

```text
ESP32-S3-WROOM-1-N16R8
16 MB Flash
8 MB Octal PSRAM
```

Known-good functions:

- `robotctl`
- PWM servo control
- OLED
- Wi-Fi
- microphone
- speaker
- `voice_echo`

Preserve existing `POWER_SAFE`.

AI Agent must never directly manipulate raw PWM, LEDC, GPIO timing, or arbitrary servo angles.

---

# 3. Brownout finding — separate from software crash

Repeated reboot loops were proven to be real brownouts:

```text
rst:0xf (BROWNOUT_RST)
[BOOT-DIAG] reset_reason procpu=BROWNOUT(0xf) appcpu=BROWNOUT(0xf)
```

A/B:

```text
servos active/powered normally
→ idle can eventually brown out / reboot

robotctl off
→ board remains stable much longer
```

Therefore every current software test begins with:

```bash
robotctl off
```

Do not:
- disable brownout detection
- lower brownout threshold
- hide it via watchdog/power config

The CPU1 software crash still occurs with `robotctl off`, so it is a separate problem.

---

# 4. Stable audio / robot code — do not touch

MIC:

```text
INMP441
I2S0 RX
16 kHz
mono
S16 logical PCM
```

Speaker:

```text
MAX98357
I2S1 TX
24 kHz
mono
S16
```

Do not modify:
- I2S RX/TX
- DMA
- EOF logic
- resampler
- audio clock
- robotctl movement internals
- PWM safety
- OLED path

during current AI Agent stabilization.

---

# 5. Build integration already solved

AI Agent is enabled.

mbedTLS / ESP HAL compatibility is handled through contest-local reversible patches/backports.

Allowed permanent edits:

```text
contest2026_295_suanliheidong/**
```

If `apps/packages/ai_agent/**` needs changes:

```text
contest-local reversible patch
→ apply
→ build
→ restore
```

Current relevant patch:

```text
contest2026_295_suanliheidong/board/contest_board/patches/0003-ai-agent-startup-and-request-guards.patch
```

Package tree should be clean after build.

---

# 6. NSH diagnostics already fixed

Keep available:

```text
ifconfig
ifup wlan0
ifdown wlan0
date
ping
wapi
```

`/proc` mounting support was added contest-locally.

Do not remove these.

---

# 7. `/data` persistence is deferred

Current:

```text
/data = tmpfs
```

A previous raw SPI Flash MTD + LittleFS attempt caused a boot regression before NSH and was reverted.

Do not re-enable now:

```text
LittleFS
raw SPI Flash MTD persistent data
contest_storage
persistent /data
auto-format
```

Persistence waits until runtime is stable.

---

# 8. AI Agent startup status

Offline start works:

```bash
robotctl off
wapi disconnect wlan0
wapi ip wlan0 0.0.0.0
ai_agent
```

Then:

```text
vela>
```

appears.

Startup synchronization now includes:

```text
[AI-DOG-DIAG] core_ready=1
[AI-DOG-DIAG] cli_ready=1
```

A previous race was identified:

```text
nsh_commands_start() returned
before CLI thread had really entered its loop
```

This was changed to wait for real CLI-ready.

Network startup uses a guarded state machine:

```text
STOPPED
→ STARTING
→ STARTED
```

with mutex protection.

Keep this logic.

The already-connected-before-`ai_agent` case still needs final revalidation later.

---

# 9. Wi-Fi `set_wifi` issue identified and corrected

Earlier:

```text
wapi show failed rc=-1 errno=10
WiFi failed
```

was a false failure.

Finding:

```text
errno=10 = ECHILD
```

from `pclose()/waitpid()` child-status handling.

Current intended Wi-Fi success logic:

```text
ifup
→ wapi mode
→ wapi psk
→ wapi essid
→ renew/DHCP
→ poll network_is_connected()
→ require IPv4 != 0.0.0.0
→ success
```

`wapi show` is diagnostic only.

Known-good log:

```text
[WIFI-DIAG] ifup
[WIFI-DIAG] wapi mode
[WIFI-DIAG] wapi psk
[WIFI-DIAG] wapi essid
[WIFI-DIAG] wapi show diagnostic failed rc=-1 errno=10
[WIFI-DIAG] dhcp/renew
[WIFI-DIAG] renew
[WIFI-DIAG] waiting ip
[WIFI-DIAG] ip=172.10.1.100
[WIFI-DIAG] connected=1
WiFi connected: 172.10.1.100
```

Wi-Fi configuration is no longer the main blocker.

---

# 10. TLS / LLM history

Earlier `ask` reached:

```text
prompt
tools
messages
react
OpenAI request
TLS Handshake start
TLS Handshake OK
```

then crashed.

Later testing showed plain HTTP can also crash, so the root cause is not TLS-specific.

LLM/ask testing is deferred until lower-level network/runtime stability is fixed.

---

# 11. Plain HTTP A/B

CLI command:

```text
net_test_http
```

does plain HTTP to `www.baidu.com:80`.

Important result:

```text
net_test_http
→ HTTP 200
→ can still trigger CPU1 exception
```

Therefore the crash is not TLS-only.

---

# 12. Network service bisection

## N3

```text
WS = SKIPPED
AgentLoop = SKIPPED
```

Result:

```text
net_test_http x3 PASS
idle 60s PASS
```

Stable.

## N1

```text
WS = ENABLED
AgentLoop = SKIPPED
```

Result:

```text
net_test_http x3 PASS
idle 60s PASS
```

Stable.

Conclusion:

```text
WebSocket server alone is not sufficient to trigger the crash.
```

## N2

```text
WS = SKIPPED
AgentLoop = ENABLED
```

Result:

```text
plain HTTP can succeed
then CPU1 exception occurs
```

This strongly implicates AgentLoop existence/initialization/memory/scheduling interaction rather than WebSocket.

---

# 13. AgentLoop bisection

AgentLoop pthread:

```text
detached
stack ≈ 32768
priority 60
no explicit affinity
```

Thread identity diagnostics were added:

```text
[THREAD-DIAG] name=agent_loop ...
[THREAD-DIAG] name=net_watch ...
[THREAD-DIAG] name=outbound ...
[THREAD-DIAG] name=agent_cli ...
```

## A1

Creates AgentLoop pthread with original parameters, enters it, then blocks on a private condition variable before real loop initialization.

It does not:
- allocate real loop buffers
- create tool pool
- build tools JSON
- read message bus
- enter LLM

Result:

```text
net_test_http x3 PASS
idle 60s PASS
```

Stable.

Conclusion:

```text
the 32 KB AgentLoop pthread itself is not sufficient to reproduce.
```

---

# 14. A2 / memory bisection

## A2

Performs:

```text
real buffers allocation
real tool pool init
real tools JSON load
then private condition wait
```

No message processing / LLM.

Observed intermittent crash after repeated HTTP.

---

## A2a

Performs:

```text
real buffers allocation only
tool pool skipped
tools JSON skipped
then wait
```

Reported stable:

```text
net_test_http x3 PASS
idle 60s PASS
```

---

## A2b

Performs:

```text
real buffers
real tool pool
tools JSON skipped
then wait
```

Some runs passed x3 + idle.
Other runs were timing-sensitive/intermittent.

Tool pool alone was not proven deterministic root cause.

---

## A2c-memory

Performs:

```text
real buffers
real tool pool
tools JSON skipped
extra retained dummy buffer ≈ 9946 bytes
```

Result:

```text
rapid net_test_http could crash as early as second call
```

Important implication:

```text
real tools JSON content is NOT required
```

A ~10 KB retained allocation can make reproduction easier.

This points toward:
- heap layout sensitivity
- latent memory corruption
- allocator interaction
- or network/internal deferred allocation pressure

It does not prove simple OOM.

---

# 15. Allocation order / canary tests

A2d variants changed retained-dummy ordering.

Example:

```text
post:
ctx → hist → tool → pool0 → pool1 → dummy

pre:
ctx → hist → tool → dummy → pool0 → pool1
```

Canary checks:

```text
[HEAP-DIAG] canary dummy PRE ok=1
[HEAP-DIAG] canary dummy POST ok=1
```

The canary stayed intact before/after successful HTTP calls, yet CPU1 exception still happened later.

So no direct dummy-buffer overwrite was observed.

---

# 16. A2e / retained heap-size sweep

A single-image helper exists:

```text
heap_hold <bytes>
```

Candidate sizes:

```text
0
2048
4096
6144
8192
9946
```

Crashes still occurred after several rapid HTTP calls.

No clean retained-size threshold is established.

Do not reduce this to a simple "free heap below N" explanation.

---

# 17. Resource / leak audit

Plaintext HTTP source audit found matched pairs:

```text
socket() → close()
getaddrinfo() → freeaddrinfo()
raw malloc() → free()
```

No obvious user-space:
- work queue
- timer callback
- AIO
- async callback
- deferred pointer holder

was found.

Detailed runtime logging was added.

Latest key sample:

```text
[RESOURCE-DIAG] seq=1 phase=PRE heap_free=8402136 arena=8649196

[HTTP-RES] seq=1 dns alloc
[HTTP-RES] seq=1 socket fd=3 open
[HTTP-RES] seq=1 dns free
[HTTP-RES] seq=1 raw alloc size=8192
[HTTP-RES] seq=1 raw free
[HTTP-RES] seq=1 socket fd=3 close rc=0

[RESOURCE-DIAG] seq=1 phase=POST heap_free=8401928 arena=8649196

SUCCESS! HTTP Status: 200

→ shortly after:
CPU1 XTENSA_DOUBLE_EXCEPTION
```

Heap delta:

```text
8401928 - 8402136 = -208 bytes
```

Do not yet call this a proven leak.

Possible explanations:
- small allocator/cache retention
- deferred TCP/IOB/Wi-Fi cleanup
- kernel/network bookkeeping
- genuine cumulative leak

More repeated measurements are needed.

---

# 18. Current strongest evidence

Current evidence:

```text
1. N3 stable: no WS + no AgentLoop

2. N1 stable: WS only

3. A1 stable: AgentLoop pthread exists but real initialization skipped

4. failures appear when real AgentLoop memory/state is introduced

5. real tools JSON is not required

6. extra retained heap makes reproduction easier

7. plaintext HTTP returns 200 and user-space cleanup completes

8. dummy canary remains intact

9. crash occurs shortly after cleanup

10. visible panic is CPU1 double exception / IDLE path
```

Therefore current best investigation direction is:

```text
memory layout / memory corruption
+
NuttX network deferred cleanup / TCP / IOB
+
SMP / inter-CPU interaction
+
allocator / internal RAM / PSRAM placement interaction
```

This is still a hypothesis, not the final root cause.

---

# 19. CPU crash pattern

Repeated crashes show:

```text
CPU1 XTENSA_DOUBLE_EXCEPTION
task: CPU1 IDLE
```

Visible symbols in prior exact-ELF symbolizations often included:

```text
up_saveusercontext
nx_vsyslog
nx_start / idle trampoline
up_idle
xtensa_appcpu_start
esp32s3_fromcpu0_interrupt
```

These likely represent secondary/recursive exception context.

Do not claim `up_saveusercontext` is the root cause.

Always symbolize with the ELF matching the flashed image.

Saved bisection ELFs live under:

```text
contest2026_295_suanliheidong/artifacts/ai_agent_stabilization/
```

Examples:

```text
nuttx-a1.elf
nuttx-a2.elf
nuttx-a2a.elf
nuttx-a2b.elf
nuttx-a2c-memory.elf
nuttx-a2d-post.elf
nuttx-a2d-pre.elf
```

---

# 20. What is not solved yet

Still unresolved:

1. original exception preceding CPU1 recursive double exception
2. whether POST heap delta accumulates monotonically
3. whether NuttX TCP close / IOB / Wi-Fi deferred cleanup is involved
4. whether heap layout changes a latent overwrite
5. whether internal RAM vs PSRAM placement matters
6. final already-connected `ai_agent` startup validation
7. repeated TLS stability
8. LLM `ask`
9. persistence
10. dog tools
11. voice integration

---

# 21. Current test discipline

Every software test starts with:

```bash
robotctl off
wapi disconnect wlan0
wapi ip wlan0 0.0.0.0
ai_agent
set_wifi <SSID> <password>
```

Do not mix servo, TLS, LLM, persistence into heap/network bisection builds.

---

# 22. Recommended next debugging direction

Do not return to full LLM yet.

### A. Determine whether the -208 byte POST delta accumulates

Instrument:

```text
seq
PRE heap_free
POST heap_free
delta
```

Need answer:

```text
-208 once then stable?
-208 every request?
variable?
returns after delayed cleanup?
```

If practical, sample after:

```text
+100 ms
+500 ms
+1 s
```

to test deferred-free theory.

### B. Inspect existing NuttX network resource counters if available

Prefer existing diagnostics/counters for:
- TCP connections
- socket refs
- IOB usage
- packet buffers
- network lock state
- Wi-Fi RX/TX buffers

### C. Memory placement A/B

Logs show buffers/pools can land in different address regions (`0x3f...`, `0x3c...`).

Determine which is:
- internal DRAM
- PSRAM/external RAM

Then test moving only AgentLoop retained buffers/pool to one allocator region.

Do not globally change memory policy.

### D. SMP A/B only if evidence still points there

Keep SMP enabled.

CPU affinity may be tested diagnostically, but disabling SMP is not an acceptable final fix.

---

# 23. Change policy

Permanent edits only in:

```text
contest2026_295_suanliheidong/**
```

AI Agent package changes continue through:

```text
0003-ai-agent-startup-and-request-guards.patch
```

Expected build cycle:

```text
apply patch
build -j8
restore package
verify package clean
verify patch applies
```

No commit unless explicitly requested.

---

# 24. Do not do now

Do not:

```text
re-enable LittleFS/MTD persistent /data
modify robotctl
modify servo POWER_SAFE
modify I2S/audio
disable brownout
lower brownout threshold
disable watchdog
disable SMP as final fix
rewrite NuttX TCP stack speculatively
jump to dog tools
jump to Feishu
jump to voice
```

Do not claim "memory leak fixed" without repeated measurements.

---

# 25. Current priority

```text
P1 reproduce CPU1 crash with minimal image
P2 quantify heap/resource behavior across repeated plain HTTP
P3 identify user-space vs network deferred cleanup vs memory-layout interaction
P4 fix root cause
P5 validate already-connected ai_agent startup repeatedly
P6 net_test TLS repeatedly
P7 set_llm + ask
P8 persistence
P9 dog tools
P10 voice
```

---

# 26. Suggested first response from the new Codex

After reading this file, do not edit yet.

Reply briefly:

```text
1. Brownout and CPU1 exception are separate issues.
2. Wi-Fi/set_wifi works; ECHILD from wapi show is diagnostic-only.
3. Plain HTTP can return 200 and still trigger CPU1 double exception.
4. N3/N1/A1 are stable; failures appear when real AgentLoop memory/state is introduced.
5. Real tools JSON is not required; retained heap/layout changes reproduction.
6. Current next target is resource lifetime / deferred network cleanup / memory placement / SMP interaction.
7. Waiting for the next stabilization instruction.
```

---

# 27. One-line current state

```text
The system boots AI Agent and connects Wi-Fi, but plain HTTP under a partially initialized real AgentLoop can complete successfully and clean up, then shortly trigger a CPU1 double exception; the fault is memory/layout/network-deferred/SMP-sensitive and not yet root-caused.
```
