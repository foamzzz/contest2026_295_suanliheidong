# AI-DOG LLM Runtime Snapshot

Date: 2026-08-24
Branch: `feature/llm-runtime-success-20260824`

This snapshot records the first observed successful complete LLM request after
the v2 monotonic watchdog/cache build. It is evidence preservation only. No
firmware was flashed during this snapshot operation.

## Exact build identity

| Item | Value |
| --- | --- |
| NuttX revision | `76354c637858ecb0aa4601629327acb6f44a26bb` |
| ai_agent base revision | `41723c61725c4e845bfee724f3ad2fafc416b6e1` |
| defconfig | `artifacts/ai_agent_stabilization/nuttx-b3.1-tools-heap-gate-v2-monotonic-cache.defconfig` (copied from `board/contest_board/configs/nsh/defconfig`) |
| defconfig SHA256 | `07a483fe0cf471f9a3c247cf5d85d8c9bfa72d9d23f499356deb23e2a9264331` |
| build config SHA256 | `1c08df1aad094a1959443d704dff735392be26195cd92840336d5e684b281138` |
| ELF | `artifacts/ai_agent_stabilization/nuttx-b3.1-tools-heap-gate-v2-monotonic-cache.elf` |
| ELF SHA256 | `2e1b72a7301c2e11eb64d22a6325d85eece5a5810921c239a30520febd4c17f1` |
| BIN | `artifacts/ai_agent_stabilization/nuttx-b3.1-tools-heap-gate-v2-monotonic-cache.bin` |
| BIN SHA256 | `cdd45cb07a07db4e2ce50b18b1829b385ec2a63d97f3fde5838493c85a17993b` |
| MAP | `artifacts/ai_agent_stabilization/maps/nuttx-b3.1-tools-heap-gate-v2-monotonic-cache.map` |
| MAP SHA256 | `2fc5cd0a9d8df51bf66e792b81db1962bd9f58b8e399f11868dab788604ea89a` |
| patch | `board/contest_board/patches/0021-ai-agent-monotonic-llm-cache-v2.patch` |
| patch SHA256 | `10e07f36a72e0e65d891c9bbcaf5fdbb9e836a686f395112393cd2c0b3705971` |
| image validation | ESP32-S3, 2 RAM segments, checksum valid |

The complete build manifest is kept at
`artifacts/ai_agent_stabilization/nuttx-b3.1-tools-heap-gate-v2-monotonic-cache.manifest.txt`.
The recorded stack is `0003, 0004, 0005, 0006, 0014`, followed by patch
`0021`.

## Runtime evidence

The unmodified console capture is stored at
`artifacts/ai_agent_stabilization/logs/llm-success-raw-2026-08-24.txt`
(SHA256 `f30fdbc5cbd227f5865b0ce7cebaaa737202a52eb8fbe63de8b6e018646f78ec`).

Observed outcomes:

- `ask time` completed two LLM iterations: the first response was HTTP 200
  with one `get_current_time` tool call, and the second was HTTP 200 with the
  final text response.
- `ask 一句话阐述惯性定律` completed one LLM iteration with HTTP 200 and a
  final text response.
- TLS began with UNIX time `39`, forced the clock to 2026, and completed a
  TLS 1.2 handshake. Neither request was classified as a watchdog timeout.
- Both requests ended with `llm call return rc=0 http=200`, successful JSON
  parsing, and outbound CLI dispatch.

This log demonstrates successful runtime behavior for these prompts; it does
not constitute a new hardware acceptance run or prove behavior for every key,
prompt, or network condition.

## Repository boundary

Only the files listed in this snapshot are staged by the snapshot commit.
Existing unrelated board, voice, NuttX, and `packages/ai_agent` working-tree
changes remain untouched and are intentionally not included. The
`packages/ai_agent` checkout is detached and has no GitHub remote; its exact
base revision and applied patch identity are recorded above rather than being
silently copied into this contest GitHub repository.
