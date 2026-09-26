# Saturn Explorer Code Review

**Review date:** 2026-09-24  
**Baseline reviewed:** `4b5727b9547584c66f51cd4cb14c4f3697066011` (`master` before these docs were added)

## Status

The reports below are as written on the review date and are **not** edited when a finding is
fixed; this table is the record of what has been dealt with since. Anything not listed here is
still open. Keep it updated in the same commit that fixes a finding — otherwise the only way to
answer "how many are left" is to re-derive it from the git log, and two readers will get two
answers.

41 IDs are filed across the 12 reports, covering 40 distinct defects: VDP1-01 is ABI-01 seen
from the VDP1 side, and one fix closed both. The rows below are the closed ones; count them
rather than trusting a tally written out in prose, which is one more thing to keep in step.

| ID | Fixed in |
|---|---|
| ABI-01 | `798567f` |
| VDP1-01 | `798567f` (same fix as ABI-01) |
| CPU-02 | `798567f` |
| MEM-01 | `798567f`, follow-up `447a1b7` |
| LIVE-04 | `798567f`, follow-up `447a1b7` |
| DISC-03 | `798567f`, follow-up `447a1b7` |
| ROM-02 | `e9d06a7` |
| ROM-03 | `e9d06a7` |
| ROM-01 | `e9d06a7` |
| DISC-01 | `c7e982f` |
| REW-01 | `e1472d1` |
| REW-02 | `e1472d1`, `7ec2676` |
| REW-03 | `e1472d1` |
| REW-04 | `91c3925` |
| LIVE-03 | `91c3925` |

VDP1-03 is half-closed: the 3D hit test no longer picks primitives the 3D view does not draw
(`018878e`), and `ARCHITECTURE.md` now records that the exploded view is quad-only. Rendering
lines and polylines there remains open.

## Highest-priority findings still open

| ID | Severity | Finding |
|---|---|---|
| HOOK-01 | **High** | Windows emulator-hook shutdown can leave blocked threads alive after locks/global state are destroyed. |
| LIVE-01 | **Medium** | Live capabilities are advertised before protocol negotiation. |
| CPU-01 | **Medium** | Conditional execution-breakpoint lookup ignores halted CPU despite CPU-specific wire descriptors. |
| VDP1-02 | **Medium** | VDP1 priority is modeled per sprite instead of per pixel, causing mixed-priority composition errors. |
| ABI-02 | **Medium** | A C++ exception can unwind across an `extern "C"` boundary. |

## Reports

1. [ABI & lifecycle](01_ABI_LIFECYCLE.md)
2. [Snapshot/state ingestion](02_SNAPSHOT_STATE_INGESTION.md)
3. [VDP1](03_VDP1.md)
4. [VDP2](04_VDP2.md)
5. [Offline inputs](05_OFFLINE_INPUTS.md)
6. [Live transport](06_LIVE_TRANSPORT.md)
7. [Emulator hooks](07_EMULATOR_HOOKS.md)
8. [CPU debugger](08_CPU_DEBUGGER.md)
9. [Memory debugger](09_MEMORY_DEBUGGER.md)
10. [Timeline/rewind](10_TIMELINE_REWIND.md)
11. [Media/disc](11_MEDIA_DISC.md)
12. [ROM-hacking workflow & frontend](12_ROM_HACKING_UI.md)

## Suggested fix order

Steps 1-3 of the original order are done (see the status table); what is left, in order:

1. HOOK-01 — the remaining High. Windows-only, so it needs a Windows machine to exercise.
2. LIVE-01/LIVE-02 and CPU-01. CPU-01 needs a semantics decision first: either execution
   breakpoints are per-CPU throughout, or CPU comes out of their representation.
3. VDP per-pixel priority accuracy (VDP1-02/VDP2-01, one fix). The largest item here: priority
   has to leave the rasterizer per texel and reach the pixel mixer, so the rasterizer's output
   and the compositor change together. Worth its own change rather than a slot in a batch.
4. ABI-02/ABI-03 and the remaining snapshot/offline parser hardening.
5. Lower-severity parser/search/documentation work.
