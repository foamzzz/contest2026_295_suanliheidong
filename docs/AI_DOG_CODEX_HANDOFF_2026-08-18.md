# AI Dog / openvela Codex Handoff — 2026-08-18

> Purpose: context handoff for a **new Codex conversation** after the previous conversation became unusable.
>
> **Read this file first and treat it as the current project truth. Do not implement anything solely from this file. After reading it, wait for the user's next prompt, which will continue the current `AI Agent stabilization` task.**

---

## 1. Project and workspace

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

Board config:

```text
contest2026_295_suanliheidong/board/contest_board/configs/nsh/
```

Important existing apps:

```text
contest2026_295_suanliheidong/app/robotctl/
contest2026_295_suanliheidong/app/voice_echo/
```

AI Agent sources/reference:

```text
apps/packages/ai_agent/**
apps/packages/demos/ai_chat/**
apps/packages/demos/mini_memo/**
```

Do not assume current git branch. Always inspect:

```bash
git status --short
git branch --show-current
```

before changing anything.

---

## 2. Hardware / stable robot baseline

MCU:

```text
ESP32-S3-WROOM-1-N16R8
16 MB flash
8 MB Octal PSRAM
```

Robot functions already working before AI integration:

- `robotctl`
- OLED
- Wi-Fi
- `voice_echo`
- servo PWM
- microphone
- speaker

Servo safety is important:

- current board power supply can brown out if multiple servos move aggressively;
- preserve existing `POWER_SAFE` movement semantics;
- AI Agent must never directly control PWM, GPIO, LEDC, or arbitrary servo angles.

Desired later architecture:

```text
ai_agent
   ↓
safe dog tools
   ↓
dog_service / safe movement layer
   ↓
robotctl movement primitives
   ↓
PWM
```

Do **not** touch robot motion while stabilizing AI Agent.

---

## 3. Stable audio baseline — do not regress

Current contest audio path is already working and should be considered frozen unless a new proven issue requires changes.

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

Existing contest-local I2S lower halves and `voice_echo` are known-good.

Do not change:

- ESP32-S3 I2S/DMA internals
- RX EOF path
- TX gapless path
- resampler
- audio clocking

during current AI Agent stabilization.

---

## 4. AI Agent enablement / build history

`Vela AI Agent` is enabled in the contest board configuration.

A previous build blocker came from ESP HAL mbedTLS headers conflicting with NuttX mbedTLS.

This is handled through a **contest-local reversible build backport**, not permanent upstream edits.

Relevant scripts are under:

```text
contest2026_295_suanliheidong/board/contest_board/scripts/
```

The wrapper applies temporary compatibility changes, builds, then restores upstream package/HAL files.

Another build blocker was:

```text
undefined reference to system
undefined reference to popen
undefined reference to pclose
```

The contest configuration enabled the existing NuttX functionality:

```text
CONFIG_PIPES=y
CONFIG_SYSTEM_POPEN=y
CONFIG_SYSTEM_SYSTEM=y
```

After that, `-j8` builds passed.

Do not replace these with custom implementations.

---

## 5. Current NSH diagnostic support

The project intentionally needs these commands available:

```text
ifconfig
ifup wlan0
ifdown wlan0
date
ping
wapi
```

Codex previously found the real dependencies:

- `ifconfig`:
  - NSH ifconfig enabled
  - `CONFIG_FS_PROCFS=y`
  - `CONFIG_FS_PROCFS_EXCLUDE_NET` must not exclude network data
- `ifup/ifdown`:
  - NSH ifup/down enabled
  - procfs/network support
- `date`:
  - NSH date enabled
- `ping`:
  - `CONFIG_SYSTEM_PING=y`
- `wapi`:
  - `CONFIG_WIRELESS_WAPI=y`
  - `CONFIG_WIRELESS_WAPI_CMDTOOL=y`

`/proc` was not originally mounted, so a minimal contest-local procfs mount was added in:

```text
board/contest_board/src/board_bringup.c
```

Current runtime has confirmed:

```text
ifup wlan0...OK
```

These diagnostic commands are intentional and should remain enabled.

---

## 6. Persistent `/data` attempt — REVERTED / DEFERRED

AI Agent stores config/memory under:

```text
/data/ai_agent/
```

Important files include:

```text
/data/ai_agent/config/config.json
/data/ai_agent/config/SOUL.md
/data/ai_agent/config/USER.md
/data/ai_agent/memory/MEMORY.md
/data/ai_agent/config/cron.json
/data/ai_agent/skills/
```

Current `/data` is `tmpfs`, so Wi-Fi credentials, LLM keys, memory, cron, etc. are lost across board reboot.

A persistence experiment tried:

- raw SPI Flash MTD
- LittleFS
- a 4 MiB region around `0x00c00000..0x01000000`
- automatic first-boot detection/format
- persistent `/data`

That experiment caused a **boot regression before NSH**.

It was fully reverted and the recovery build again reached:

```text
*** Booting NuttX ***
[C-I2S] ...
NuttShell (NSH)
nsh>
```

Therefore:

### CURRENT RULE

Do **not** re-enable any of the following during AI Agent stabilization:

```text
LittleFS
raw SPI Flash MTD persistent data
contest_storage
persistent /data
automatic flash format
persistent-storage PSRAM/flash support
```

Current `/data` stays `tmpfs`.

Persistence will be redesigned later after the AI Agent main path is stable.

---

## 7. AI Agent architecture audit already completed

Important verified source facts:

### Agent lifecycle

Main entry:

```text
apps/packages/ai_agent/src/agent_main.c
ai_agent_main()
```

Agent loop:

```text
src/core/agent_loop.c
agent_loop_task()
```

Core flow:

```text
message_bus inbound
→ LLM/ReAct
→ tool_registry_execute()
→ message_bus outbound
```

### Local client

```text
include/velaclaw/client.h
velaclaw_client_open()
velaclaw_client_close()
velaclaw_ask()
```

`mini_memo` uses the VelaClaw local client.

`ai_chat` is a separate Volc realtime conversation demo and is not the AI Agent/VelaClaw path.

### Tool registry

`agent_tool_t` includes:

```text
name
description
input_schema_json
execute
```

There is no stable independent runtime IPC ABI for an external contest app to register arbitrary tools.

Later dog tools will likely need a contest-local provider hook / reversible AI Agent patch.

### Skills

AI Agent scans:

```text
/data/ai_agent/skills/*.md
```

### Cron

Cron jobs can carry:

```text
action
action_args
```

and invoke registered tools.

### Voice

AI Agent voice expects approximately:

```text
ASR input: 16 kHz mono S16LE
TTS output: 24 kHz mono S16LE
```

Default agent audio devices do not automatically bridge to the contest I2S accessors.

Voice integration is postponed until text Agent stabilization passes.

---

## 8. Current AI Agent runtime status

### AI Agent offline start

This sequence is stable enough to reach the Vela CLI:

```bash
nsh> wapi disconnect wlan0
nsh> wapi ip wlan0 0.0.0.0
nsh> ai_agent
```

Then:

```text
vela>
```

appears and CLI commands work.

### Wi-Fi after Agent starts

From `vela>`:

```text
set_wifi <ssid> <password>
```

can connect successfully.

Current runtime has shown:

```text
ifup wlan0...OK
Network connected: 172.10.1.x
WebSocket server started on port 28789
All network services started!
Agent loop started
```

### Remaining Wi-Fi issue

If the board is **already connected** before:

```bash
nsh> ai_agent
```

the device can still crash.

A previous stabilization patch added:

- `core_ready`
- mutex-protected network-service startup
- `start_network_services_once()`
- shared startup path for already-connected and later-connected network states

but the already-connected startup crash is **not yet proven fixed**.

The current patch lives as a reversible contest patch:

```text
board/contest_board/patches/0003-ai-agent-startup-and-request-guards.patch
```

It is applied for build and reverted from `apps/packages/ai_agent` afterward.

Do not assume that patch solved the startup race.

---

## 9. Current `set_wifi` oddity

With the new NSH config:

```text
ifup wlan0...OK
```

is now real.

However, `set_wifi` still prints the entire `wapi` Usage page.

That strongly suggests at least one `system("wapi ...")` command generated by `network_wifi_connect()` does not match the board's actual `wapi` CLI syntax.

Despite the Usage output, Wi-Fi eventually gets an IP.

This should be investigated, but it may or may not be the crash root cause.

Do not silently ignore command return codes.

---

## 10. Current LLM / `ask` crash — highest-value evidence

LLM setup succeeds:

```text
set_llm <host> <model> <key>
```

Example backend in testing was an OpenAI-compatible endpoint.

**Do not reuse or print the previously exposed token. The user should rotate it.**

Current `ask` path has progressed much farther than earlier attempts.

Example:

```text
vela> ask openvela一句话介绍
```

Latest diagnostic output:

```text
Processing message from cli:console
Skills summary: 814 bytes
System prompt built: 3289 bytes

[AI-DOG-DIAG] ask stage=prompt
[AI-DOG-DIAG] ask stage=tools bytes=9946
[AI-DOG-DIAG] ask stage=messages
[AI-DOG-DIAG] ask stage=react

OpenAI API with tools (... 14882 bytes)

[vela_tls] Handshake start: Host=...
[vela_tls] Clock too old, forcing to 2026

[Agent]: 让我查一下...

[vela_tls] Handshake OK: TLSv1.2 / TLS-ECDHE-RSA-WITH-CHACHA20-POLY1305-SHA256

→ immediately followed by CPU1 XTENSA_DOUBLE_EXCEPTION
```

This is critical.

### What is now known

The crash is **after**:

- prompt construction
- tools JSON
- messages JSON
- ReAct entry
- LLM request construction
- DNS/connect far enough to start TLS
- TLS handshake successfully completes

Therefore do **not** keep treating prompt/cJSON allocation as the main root cause unless new evidence proves it.

The next useful boundary is:

```text
TLS handshake OK
→ HTTP send
→ HTTP recv
→ HTTP response parsing
→ LLM response handling
```

---

## 11. Latest crash characteristics

Latest runtime crash again becomes:

```text
CPU1 XTENSA_DOUBLE_EXCEPTION
task: CPU1 IDLE
```

The visible panic PC is likely the recursive/double-exception path and may hide the original fault.

The latest dump also showed:

```text
wifi task: Running
```

around the crash.

This makes the Wi-Fi/TLS/socket/SMP interaction worth isolating.

Do not modify Xtensa panic/assert code to hide or “fix” this.

Do not disable SMP as the final fix.

If CPU affinity is tested, treat it only as an A/B diagnostic experiment.

---

## 12. New severe symptom: reboot loop after crash

After an `ask` crash, the board can later enter a state where it repeatedly reboots and may reboot again **before reaching NSH**.

This is now a separate high-priority stabilization issue.

Need to determine reset reason using existing ESP32-S3/NuttX support if possible.

Desired early diagnostic:

```text
[BOOT-DIAG] reset_reason=...
```

before Wi-Fi/I2S/AI Agent startup.

Need distinguish:

- power-on reset
- software reset
- task watchdog
- interrupt watchdog
- panic reset
- brownout
- other reset source

Do not disable watchdogs.
Do not change watchdog thresholds just to hide the issue.
Do not disable brownout detection.

---

## 13. Important experiment still needed: `net_test` vs `ask`

We still need to separate:

```text
generic TLS/network traffic crash
```

from:

```text
LLM HTTP/client/response path crash
```

Use the existing:

```text
vela> net_test
```

on the current stabilization image.

Need diagnostic boundaries around:

```text
handshake
send
recv
close
```

Target matrix:

```text
network idle      PASS/FAIL
net_test x5       PASS/FAIL
ask 你好          PASS/FAIL
post-panic reboot PASS/FAIL
reset_reason      value
```

If `net_test` crashes after TLS handshake, prioritize TLS/socket/Wi-Fi/SMP.

If `net_test` passes repeatedly but `ask` crashes, prioritize LLM HTTP client, response parser, buffer ownership, async/lifetime bugs.

---

## 14. Current reversible AI Agent patch

Current contest patch:

```text
board/contest_board/patches/0003-ai-agent-startup-and-request-guards.patch
```

It currently includes at least:

- network service startup guard / mutex / once behavior
- diagnostic `[AI-DOG-DIAG]` logs
- request construction/allocation guards

The build process applies this to `apps/packages/ai_agent`, builds, then restores upstream package sources.

Permanent upstream package edits are not allowed.

If further AI Agent source changes are needed, extend the contest-local reversible patch instead of leaving `apps/packages/ai_agent` dirty.

---

## 15. Latest known build

Latest stabilization build reported:

```text
-j8: PASS
nuttx/nuttx.bin: 1,118,664 bytes
ESP32-S3 image checksum valid
```

Build timestamp in runtime:

```text
2026-08-18 08:24:11
```

Always use the ELF matching the exact flashed binary when symbolizing crash addresses.

Do not symbolize a crash using an older ELF; address layout changes between builds.

---

## 16. Current source modification policy

Allowed permanent edits:

```text
contest2026_295_suanliheidong/**
```

Generally forbidden permanent edits:

```text
nuttx/**
vendor/**
HAL/**
apps/packages/**
```

When package/HAL compatibility changes are unavoidable:

```text
contest-local reversible patch/backport
→ apply
→ build
→ restore
```

Never commit upstream/common changes just to get this contest board working.

---

## 17. Files that must stay stable

Do not regress:

- robotctl
- `POWER_SAFE` servo policy
- voice_echo
- contest I2S RX/TX
- PWM
- OLED
- Wi-Fi baseline
- boot layout / Simple Boot unless explicitly investigated later
- brownout behavior
- SMP merely to make crashes disappear

---

## 18. Current priority order

Do not jump to dog tools, voice, persistence, Feishu, MCP, music, or proactive features yet.

Current stabilization order:

```text
1. keep NSH diagnostics working
2. fix already-connected `nsh> ai_agent` startup crash
3. isolate net_test vs ask
4. symbolize current crash using exact current ELF
5. find post-TLS-handshake crash location
6. identify reboot-loop reset reason
7. stabilize repeated network + LLM runs
8. only then return to persistence
9. after text Agent is stable, proceed to dog tools
10. voice comes after safe text/tool integration
```

---

## 19. What the next Codex task should focus on

The user will send a separate detailed prompt titled approximately:

```text
继续 AI Agent stabilization
```

When that prompt arrives:

- use this handoff as background;
- do not repeat architecture discovery already completed;
- do not reintroduce persistent storage;
- do not redo solved `system/popen/pclose` work;
- do not redo basic AI Agent enablement;
- focus on the current runtime crashes and exact experiment matrix.

---

## 20. Security note

A real LLM API token was pasted into a previous terminal/chat log.

Do not store it in source, patches, summaries, or logs.

The user should revoke/rotate that credential before further testing.

Never print Authorization headers or full API keys in diagnostic output.

---

## 21. First actions in the new Codex conversation

Before editing:

```bash
cd ~/vela/openvela
git status --short
git branch --show-current
```

Then inspect the current contest modifications and reversible patch:

```text
contest2026_295_suanliheidong/board/contest_board/src/board_bringup.c
contest2026_295_suanliheidong/board/contest_board/configs/nsh/defconfig
contest2026_295_suanliheidong/board/contest_board/patches/0003-ai-agent-startup-and-request-guards.patch
contest2026_295_suanliheidong/board/contest_board/scripts/
```

Do not modify anything until the user sends the next stabilization instruction.

---

# End state to remember

The project is **not blocked on enabling AI Agent anymore**.

AI Agent boots, registers tools/skills, enters CLI, connects Wi-Fi, configures an LLM backend, and reaches a successful TLS handshake.

The main blockers are now runtime stability:

```text
already-connected startup → may crash
ask → TLS handshake OK → crash
panic → may lead to repeated boot loop
```

That is the current problem.
