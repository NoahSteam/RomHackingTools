# Saturn Explorer codebase knowledge base

Inspected source revision: `ff6a3b1fb1c54e4c0344613094ea65292b0023c2` (master), 2026-10-20.
Scope: the `SaturnExplorer/` application in the larger RomHackingTools repository.
Paths in prose are relative to `SaturnExplorer/` unless explicitly labelled repository-root.
This is a living implementation map, not a review ledger or a replacement for source inspection.

Saturn Explorer reconstructs Sega Saturn graphics from captured hardware memory, exposes the
objects and memory behind that image, and provides live debugging and ROM-hacking tools. It
reads savestates and dumps or connects to a patched emulator; the desktop frontend adds
execution control, SH-2 pseudocode, sound inspection, recording/rewind and disc patch workflows.
The AI investigation module currently supplies contracts and validation infrastructure rather
than a connected in-app agent workflow; see its subsystem page before assuming UI availability.

## Start with the task

1. Search [FEATURE_MAP](FEATURE_MAP.md) for the requested behavior or developer task.
2. Read only the linked subsystem pages, then inspect their source entry points and call sites.
3. Use [ARCHITECTURE](ARCHITECTURE.md) for ownership and boundaries and [DATA_FLOW](DATA_FLOW.md)
   for cross-system sequencing. Consult [CONVENTIONS](CONVENTIONS.md) before implementing.
4. Check [DEPENDENCIES](DEPENDENCIES.md) before changing a seam, emulator integration, generated
   artifact or vendored component. Read [KNOWN_LIMITATIONS](KNOWN_LIMITATIONS.md) for scope gaps.
5. Find the relevant test family in the subsystem page and confirm its current CMake registration.
   Code takes precedence over these notes; investigate discrepancies rather than following stale prose.

## Subsystem index

| System | Owns | Map |
|---|---|---|
| Application and platforms | lifecycle, command routing, settings, OS/GPU bridge, demo, sound preview | [Frontend](Systems/Frontend.md) |
| Graphics analysis | snapshots, VDP1 parsing/rasterizing, VDP2 compositing, geometry, references | [Graphics](Systems/Graphics.md) |
| State acquisition | savestate formats, live transport, tap integration, session/restore identities | [State sources](Systems/StateSources.md) |
| Execution debugger | stepping, stops, breakpoints, tracepoints, disassembly, stack | [Debugger](Systems/Debugger.md) |
| Memory and time | memory backend, watch/search/compare, recorded frames and staged edits | [Memory and history](Systems/MemoryAndHistory.md) |
| Embedded Ghidra | capture, function finding, worker, pseudocode model and trust | [Decompiler](Systems/Decompiler.md) |
| Game files | disc parsing/building, file search, PRS, accepted patch locations | [Disc and patching](Systems/DiscAndPatching.md) |
| Investigation contracts | evidence, identity, strict JSON, response/action validation | [AI investigation](Systems/AIInvestigation.md) |

[Investigation report](INVESTIGATION_REPORT.md) records coverage and unresolved questions.
Existing authoritative subject guides remain linked from the subsystem pages; build and hardware
explanations are deliberately not copied here.

## Evidence and freshness

Implementation statements are **verified from source** at the revision above unless explicitly
labelled **inferred** or **not investigated**. File links and symbols are navigation evidence;
they do not imply exhaustive verification of every branch or hardware-accurate emulation.
Tests listed are existing coverage, not a claim that they were run during this documentation task.

After a code change, update affected behavior, ownership, APIs, flows and constraints in the same
change. Add significant new features to FEATURE_MAP and their subsystem page. Preserve useful
information; remove it only when demonstrably obsolete. Keep links/diagrams aligned with source.
Trivial local implementation details need no documentation change. Update a page's inspected
revision when rechecking it; avoid changing the baseline globally unless the whole map was rechecked.
Historical review results belong in existing review records, not in this navigation map.
