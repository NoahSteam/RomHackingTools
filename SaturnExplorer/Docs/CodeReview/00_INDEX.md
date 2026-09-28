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
| REW-01 | `e1472d1` — bounded; eviction is still not dependency-aware |
| REW-02 | `e1472d1`, `7ec2676` |
| REW-03 | `e1472d1` |
| REW-04 | `91c3925` — bounded in bytes; drops oldest, not by dependency group |
| LIVE-03 | `91c3925` — bounded; writes are not coalesced |
| CPU-01 | `1229b76` (+ #51) — settled as shared, not per-CPU |
| OFF-02 | `a4f8086` |
| OFF-03 | `a4f8086`, redone at the real site in `3b888e7` |
| SNAP-01 | `6c8a46e` |
| SNAP-02 | `6c8a46e`, parsers in `3b888e7`, last two openers in `2d0b06f` |
| VDP2-02 | `6c8a46e` |
| LIVE-01 | `8909094` — payload verbs gated; capabilities still advertised up front |
| LIVE-02 | `8909094` |
| HOOK-02 | `8909094` |
| HOOK-03 | `8909094` |
| ABI-02 | `571a078` (core), `2d0b06f` (drivers) |
| ABI-03 | `571a078` — removed at ABI 6, not implemented |
| OFF-01 | `abc285b` |
| CPU-03 | `0f536f9` |
| MEM-02 | `1f1997d` |
| MEM-03 | `1f1997d` |
| DISC-02 | `c45376a` |
| ROM-05 | `a5e76d1` |
| ROM-04 | `800099a` |
| MEDIA-01 | `1355a75` — labelled; SSCTL/SBCTL are not carried, so it cannot refuse those voices |
| VDP1-03 | `859c9ac` |
| HOOK-01 | `7ee20e7` |
| UI-02 | `d87d384` |
| VDP1-02 | this commit |
| VDP2-01 | this commit (same fix as VDP1-02) |

CPU-01 asked for the semantics to be settled either way. They are settled as **shared**: a PC
breakpoint halts whichever SH-2 reaches the address, so CPU takes no part in execution-breakpoint
matching, conditional-guard lookup, or the Step Over / Step Out transient. The wire descriptor
keeps its CPU bit rather than forcing a protocol bump, reserved and ignored for execution
breakpoints; an installer must arm both cores. Memory watchpoints are untouched and may still be
per-CPU where a backend supports it.

Three rows are annotated because the report asked for more than a bound: REW-01 and REW-04
suggest eviction that understands which frames a delta depends on, and LIVE-03 suggests
coalescing adjacent writes. What landed is the memory bound, which is what made them Medium.
Losing a keyframe still orphans its deltas -- `CanReconstruct` reports that honestly, so the
cost is rewind depth rather than a wrong answer.

VDP1-03 is closed: the 3D view now draws lines and polylines as depth-tested edges, and the hit
test picks them by distance to those edges rather than by containment -- so a click in the empty
middle of a polyline selects what is actually drawn there. The earlier half (`018878e`) had the
hit test agree with a renderer that skipped them.

## Findings still open

Derive it rather than trusting a number in prose -- the table above has 40 rows, one of which
(VDP1-01) is a duplicate of ABI-01, so 39 of the 40 distinct defects are closed:

```
grep -ho '^## [A-Z][A-Z0-9]*-[0-9]*' Docs/CodeReview/*.md | sed 's/^## //' | sort > /tmp/all
awk '/^\| ID \| Fixed in \|/,/^$/' Docs/CodeReview/00_INDEX.md \
  | grep -o '^| [A-Z][A-Z0-9]*-[0-9]*' | sed 's/^| //' | sort > /tmp/closed
comm -13 /tmp/closed /tmp/all
```

which leaves

| ID | Severity | Finding |
|---|---|---|
| UI-01 | **Medium** | `App.cpp` has become an oversized lifecycle coordinator. |

It is taken as its own change rather than as part of a batch: the split is mechanical but touches
everything.

Three of the closed rows are annotated because what landed is narrower than what the report
suggested -- LIVE-01 (the payload verbs are version-gated, but capabilities are still advertised
before negotiation, which would need `se_live_open` to block), MEDIA-01 (the preview says what it
is, but cannot refuse a noise/zero or SBCTL voice, because the emulator's slot record does not
carry those two fields), and ABI-03 (removed rather than implemented). REW-01, REW-04 and LIVE-03
are annotated below for the same reason.

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

All of the original order is done. What remains is the two items above, in either order --
they touch disjoint code.

A note on HOOK-01, the one High that needed a platform nobody here runs: it was verified rather
than reasoned about, by cross-compiling `Integration/Common/se_export.c` with mingw and running
`Integration/Common/tests/SeExportShutdownTests.c` under wine. The pre-fix shutdown fails all
three of its cases; the fix passes them. That route is written down in the test's header comment,
because "Windows-only, so we cannot check it" is how a High stays open.
