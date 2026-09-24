# Saturn Explorer Code Review

**Review date:** 2026-09-24  
**Baseline reviewed:** `4b5727b9547584c66f51cd4cb14c4f3697066011` (`master` before these docs were added)

## Highest-priority findings

| ID | Severity | Finding |
|---|---|---|
| ABI-01 | **High** | `ReadBE16` can integer-wrap its bounds check, allowing an out-of-bounds read from caller-supplied addresses. |
| HOOK-01 | **High** | Windows emulator-hook shutdown can leave blocked threads alive after locks/global state are destroyed. |
| ROM-01 | **High** | Generated patch scripts do not verify stored baseline bytes before writing remembered file offsets. |
| DISC-01 | **High** | BIN/CUE building can report success while one or more declared source tracks failed to copy. |
| LIVE-01 | **Medium** | Live capabilities are advertised before protocol negotiation. |
| REW-01 | **Medium** | Lagging rewind-state attachments increase ring memory without immediately re-running eviction. |
| CPU-01 | **Medium** | Conditional execution-breakpoint lookup ignores halted CPU despite CPU-specific wire descriptors. |
| VDP1-02 | **Medium** | VDP1 priority is modeled per sprite instead of per pixel, causing mixed-priority composition errors. |

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

1. ABI-01 and HOOK-01.
2. ROM-01 and DISC-01.
3. LIVE-01/LIVE-02 and REW-01.
4. CPU-01/CPU-02.
5. VDP per-pixel priority accuracy.
6. Lower-severity parser/search/documentation work.
