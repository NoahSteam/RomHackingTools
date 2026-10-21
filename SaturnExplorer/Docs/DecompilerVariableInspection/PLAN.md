# Decompiled-code inspection: low-effort implementation plan

Status: P0–P3 implemented on `codex/decompiler-variable-inspection`; see [validation and review record](VALIDATION.md).
Revision: 2, incorporating source-verification findings.
Date: 2026-10-20.
Source inspected: `9984714e9403faa40ac6031bc6e4a0f193e815d8`; rechecked at
`374f9ae979b88847e4d96696528680a269605afe` on master (production sources unchanged).
Scope: SaturnExplorer inside NoahSteam/RomHackingTools.
Paths below are relative to SaturnExplorer unless stated otherwise.

## 1. Goal and scope decision

Deliver useful inspection directly beside and within pseudocode without first implementing
optimized-variable lifetime recovery. The first release answers:

- What does this absolute global location contain in the displayed capture?
- What bytes or printable ASCII are at a known pointer target?
- What are this CPU's captured registers while I read the pseudocode?
- How do I open the object's storage or pointer target in Memory?
- How do I add a supported fixed RAM address to the existing Watch panel?

The earlier feature ranking described register-backed variable and stack-local inspection as
medium effort. Source inspection confirms that they require a new correctness analysis.
They are follow-on work, not prerequisites or hidden deliverables of this plan.

| Earlier feature | Decision here | Boundary |
|---|---|---|
| Global variable hover | Implement | Absolute, unambiguous object storage; scalar widths 1/2/4 bytes |
| Pointer inspection | Implement | Typed pointer constants and pointer-valued absolute globals; register previews if the optional embedding is implemented |
| Registers beside pseudocode | Reuse existing Registers window; optional embedding | Share the existing register drawing implementation if embedded |
| Open in Memory / copy address | Implement | Explicitly distinguish object storage from the pointer target |
| Pin values | Small final increment | Add a fixed WRAM location to existing Watches |
| Changed-value highlighting | Reuse existing Watches | No new history/highlight engine in the pseudocode panel |
| Register-backed pseudocode locals/parameters | Defer | A Varnode storage address is not a proof of current register contents |
| Stack locals / stack parameters | Defer | No entry-SP reconstruction or frame analysis in this release |
| General dereferences, arrays, fields, expressions | Defer | No arbitrary SSA traversal, C-expression parser, or operator-output shortcut |
| Overwritten values and caller-frame values | Defer | No historical memory reconstruction or unwinder |

The implementation should be split into the milestones in section 7. Relative effort is an
architectural estimate, not a promised schedule. Global metadata extraction is the largest
piece; register access and fixed-address navigation are the first independently useful delivery.
The existing Registers window already provides register display and several navigation actions.
Embedding it inside pseudocode is optional presentation work, not a prerequisite for global hovers.

## 2. Verified implementation starting points

The investigation followed the codebase knowledge workflow in
[CLAUDE.md](../../CLAUDE.md), beginning with
[Codebase/README](../Codebase/README.md), [FEATURE_MAP](../Codebase/FEATURE_MAP.md),
[architecture](../Codebase/ARCHITECTURE.md), [data flow](../Codebase/DATA_FLOW.md),
[conventions](../Codebase/CONVENTIONS.md), [dependencies](../Codebase/DEPENDENCIES.md),
and [known limits](../Codebase/KNOWN_LIMITATIONS.md). Relevant subsystem pages were
[Decompiler](../Codebase/Systems/Decompiler.md),
[Debugger](../Codebase/Systems/Debugger.md),
[MemoryAndHistory](../Codebase/Systems/MemoryAndHistory.md),
[StateSources](../Codebase/Systems/StateSources.md), and
[Frontend](../Codebase/Systems/Frontend.md). Source, not the map, determines the design below.

| Existing implementation | Verified behavior / implication |
|---|---|
| [App.cpp](../../FrontEnd/src/App.cpp), `BuildDecompilerView`, `UpdateDecompiler` | Supplies the displayed live/static/scrub context, both CPUs' registers, normalized capture identity and optional BIOS. Handles panel `Request::viewHex` already. |
| [DecompilerPanel.h](../../FrontEnd/src/DecompilerPanel.h), [implementation](../../FrontEnd/src/DecompilerPanel.cpp) | Owns held `mSnap`, shown-result identity, eight-entry LRU, refresh/acceptance policy, CPU selection, line rendering and navigation requests. |
| Same panel, `HaltHint` | Demonstrates stale/running and execution-mirror checks, but also contains indirect-call-specific logic that must not become the general inspection gate. |
| [MemorySnapshot.h](../../FrontEnd/src/Decompiler/MemorySnapshot.h), [implementation](../../FrontEnd/src/Decompiler/MemorySnapshot.cpp) | Owns immutable complete WRAM regions, optional BIOS bytes and captured registers. `Read` folds documented aliases and refuses unavailable bytes. |
| [Sh2Decompiler.h](../../FrontEnd/src/Decompiler/Sh2Decompiler.h), [implementation](../../FrontEnd/src/Decompiler/Sh2Decompiler.cpp) | `Token` has text spans, color and instruction/call provenance, not an inspection descriptor. `LinesFromMarkup` currently consumes `OPREF`, not `VARREF`. |
| [prettyprint.cc](../../FrontEnd/third_party/ghidra-decompiler/cpp/prettyprint.cc), `EmitMarkup::tagVariable` | Emits `VARREF = Varnode::getCreateIndex()` when a Varnode exists, and independently emits `OPREF`. |
| [printlanguage.cc](../../FrontEnd/third_party/ghidra-decompiler/cpp/printlanguage.cc), `pushSymbolDetail`; [printc.cc](../../FrontEnd/third_party/ghidra-decompiler/cpp/printc.cc), `opPtrsub` | Printed names can denote whole symbols, partial symbols or address expressions. The spacebase-global PTRSUB path can print a global with a null Varnode: VARREF-only export misses real cases. |
| Same `printc.cc` and panel/wrapper provenance handling | Brackets/field syntax need not denote a LOAD result. Do not bind `opref -> output` universally. |
| [Sh2RegInfo.h](../../FrontEnd/src/Debug/Sh2RegInfo.h); [App.cpp](../../FrontEnd/src/App.cpp), `DrawSh2Registers` | Existing register names, values, SR summaries, region annotations, Memory/disassembly navigation, PC decompilation and Add to Watch. Reuse this UI rather than implementing a second register viewer. Its 23-entry UI order differs from the decompiler's 22-register CallEffects bit order; do not interchange indices. |
| [AssemblyPanel.cpp](../../FrontEnd/src/AssemblyPanel.cpp), `MemReaderFor`; [Sh2Operands.cpp](../../FrontEnd/src/Sh2Operands.cpp) | Existing big-endian previews/ASCII formatting are useful examples, but the reader uses the general backend and is not safe to copy unchanged into this feature. |
| [MemoryBackend.cpp](../../FrontEnd/src/Debug/MemoryBackend.cpp), `ReadOne` | Exposes VDP register images and uses broader address folding than `Sh2Address`. Do not use it as the pseudocode preview reader. |
| [WatchList.h](../../FrontEnd/src/Debug/WatchList.h), [implementation](../../FrontEnd/src/Debug/WatchList.cpp) | Existing scalar formatting and fixed-address watches. Pointer formatting can probe an optional backend: never pass the App backend into this feature's formatter. |
| [App.cpp](../../FrontEnd/src/App.cpp), `AddAddressWatch`; [WatchPanel.h](../../FrontEnd/src/WatchPanel.h) | Existing route to add/open a fixed-address watch. Watch refresh and change highlighting already exist. |
| [FormatString.h](../../FrontEnd/src/Debug/FormatString.h), [implementation](../../FrontEnd/src/Debug/FormatString.cpp); App's `ContextFormat::ReadString` | Existing register/memory formatting and bounded string support. The App adapter reads the general backend, so it cannot be copied unchanged into the snapshot inspector. |
| [ConditionEval.h](../../FrontEnd/src/Debug/ConditionEval.h), [implementation](../../FrontEnd/src/Debug/ConditionEval.cpp) | Existing register/dereference expression parsing for guards. Its public result is boolean and errors fail open; it is not a typed pseudocode-value evaluator. Consider reuse in a later expression proposal, not a new parser in this release. |
| [FrontEnd.vcxproj](../../FrontEnd/FrontEnd.vcxproj) | Includes `src\**\*.cpp` and `src\**\*.h` through wildcards. New frontend sources need no explicit MSBuild registration. CMake source/test lists remain explicit. |
| [LiveDriver.cpp](../../Drivers/Live/src/LiveDriver.cpp), `SameContent`; [CaptureRepeatTracker.h](../../FrontEnd/src/CaptureRepeatTracker.h) | Live content comparison already includes both SH-2 register files. Register-only changes are not intentionally suppressed as identical captures. |

## 3. User-visible semantics

### 3.1 Global tooltip

Example, with illustrative names/addresses:

```text
DAT_06010000
Storage: 0x06010000   Size: 4 bytes
Captured memory: 0x0000002A   Unsigned: 42
Source: live, paused / master SH-2 / frame ...
```

The value is the object's memory in this capture. It is not claimed to be the value read
or written when this particular source occurrence executed. This remains true on an assignment
destination and on an occurrence above or below the paused statement.

For a typed pointer global, show separate sections:

- Storage address and captured pointer bits.
- Pointer target address.
- At most 16 target bytes and their escaped ASCII representation from the same snapshot.
- A reason when the target cannot be read.

Do not confuse inspecting `&global` with reading the global's stored pointer value.
An address-form descriptor previews the object at that address; an object-form descriptor
first reads its storage. Never silently add or remove a dereference.

### 3.2 Captured registers

The minimum delivery provides access to the existing Registers window, selecting the same CPU
as pseudocode. Existing register values, SR summaries, region notes, Memory/disassembly navigation
and Add to Watch are reused; they are not new capabilities to rebuild.

An optional collapsible “Captured registers” section above the code child may embed the same
presentation. If implemented, first extract a small register-drawing widget from
`App::DrawSh2Registers` that accepts an explicit register file and returns user-action requests.
App supplies its displayed-context registers to the existing window; DecompilerPanel supplies
`mSnap->regs[cpu]` to the embedded section. Do not call the App-owned renderer directly from the
panel or independently sample registers for the embedded view. Reuse `Sh2RegTable`,
`Sh2RegValue`, SR summaries and existing menus. Preserve PC decompilation and disassembly
navigation in the existing window during extraction. The new bounded snapshot preview is a
separate addition, not a reason to duplicate register rendering.

The ordinary Registers window keeps its existing current-view availability and behavior.
Only the optional embedded inspector uses the stricter matched-output gate in section 4.4;
opening the existing window does not certify a relationship to a pseudocode variable.

Register values are observations of registers, not aliases for pseudocode names. Do not label
R4 “param_1” or R8 “local_x”. Preserve calling-convention descriptions as conventions only.
A plausible register address may offer “Preview memory at value”; that action does not assert
that the register's type is a pointer. MACH/MACL/SR should not automatically acquire pointer
semantics from their numerical range.

PC is labelled “captured PC”. Static/scrub/frame-pause captures do not establish that this is
the next instruction to execute. This feature makes no instruction-before/after claim and
does not need to change Mednafen's register export.

### 3.3 Availability and labels

- Enable inspection only for a matched, non-running displayed capture.
- LivePaused: label “captured memory/registers”, not a historical expression result.
- Static and Scrub: permit captured-memory/register inspection with explicit source/frame labels;
  their usefulness does not require debugger-halt PC semantics.
- Running, stale output, missing capture, closed source and mismatched identities: no numeric
  inspection tooltip or active inspection action. Existing source/provenance help remains.
- Missing registers disable only the register section, not absolute-global inspection.
- A user-supplied BIOS preview is labelled “loaded BIOS image”; it is not evidence of the
  emulator's exact BIOS contents.
- Unsupported scalar types show an address and bounded raw bytes where the binding is valid;
  do not render float/aggregate bits as a known integer type.
- Captured WRAM remains inspectable even when declared volatile for decompilation. A volatile
  qualifier means it may change during execution; the tooltip reports sampled capture bytes,
  not a fixed analysis constant or the result of a historical load.
- Hardware registers show their existing names/notes and “device value not available from this
  capture”; no hardware/VDP-image fallback is attempted.

## 4. Architecture and data contracts

### 4.1 Small result extension, not an SSA evaluator

Add an owned optional inspection binding to `Token`, preferably a zero-default index into a
`DecompileResult::inspectionTargets` vector. Index 0 means none. Keep existing instruction
`addr`, `target` and `calledAt` semantics unchanged; address 0 is valid for an inspection
target and must not double as the absent sentinel.

Proposed host-only descriptor fields:

| Field | Meaning |
|---|---|
| kind | AbsoluteObject or ConstantAddress |
| address | Original SH-2 bus address of the object/target |
| byteSize | Object width when proven; supported scalar decoding is initially 1/2/4 |
| display kind | RawBytes, SignedInteger, UnsignedInteger, Bool or Pointer |
| type label / TypeShape | Copied type information, never an engine pointer |
| binding form | Whole object versus address form; ambiguous partial forms have no binding |

Reuse `TypeShape` where suitable, but do not let its Unknown fallback turn floats/aggregates
into signed/unsigned integers. The inspection export must check the actual metatype and width.
No graph, raw-instruction program, live ranges, stack homes or per-PC dataflow is exported.

All bindings are extracted while Funcdata/markup references exist. The result/cache holds only
owned C++ values. Runtime memory values and preview strings do not enter the decompiler LRU.

### 4.2 Export rules in LinesFromMarkup

Build the VARREF lookup once from Funcdata Varnodes using `getCreateIndex()`; retain the
existing OPREF lookup. Resolve only a small, explicit set of printed forms:

1. A variable token whose symbol/Varnode proves a whole, fixed global-memory mapping in the
   default bus/code space. Confirm whole-symbol correspondence, width and exact location.
   A register/unique/stack-space Varnode is never accepted as an absolute global.
2. A null-VARREF global token from the spacebase PTRSUB printer path. Resolve the same static
   global symbol mapping/offset or TypeSpacebase address the printer uses. Require global
   bus storage; a stack spacebase is refused. Export a whole-object/address binding only when
   the printed form is unambiguous.
3. A variable-form constant whose engine type proves a 32-bit pointer/address and whose printed
   token denotes that complete address. This includes eligible folded pointer literals;
   arbitrary numeric constants are not auto-dereferenced.

Use engine symbol mappings and print context, not a parser for `DAT_...`, color alone,
variable names, or comparison with instruction addresses. Do not choose the first symbol
mapping if several mappings cannot be disambiguated.

Partial symbols, structure members, array syntax, dynamic addresses, joined storage,
mismatch symbols, hidden LOAD provenance and ambiguous printer forms remain unbound.
In particular, the global base token in `global.field` must not receive the field's value
or misleadingly present itself as the entire expression. Reject ambiguous spans instead.

No engine patch is assumed necessary. Add deterministic fixtures for both global printer forms
before completing the export; if a form cannot be distinguished from existing markup and engine
objects, leave it unsupported and document coverage rather than guess or expand into a printer
rewrite. The release gate still requires ordinary absolute scalar loads AND stores to be
inspectable; read-only mapping success alone is insufficient.

### 4.3 Pure snapshot inspection helper

Add proposed files `FrontEnd/src/Debug/CapturedValueInspection.h/.cpp`, with no ImGui,
Ghidra, transport or App dependencies. It consumes an immutable MemorySnapshot and the small
descriptor/register observation, returning status, raw bytes, numeric presentation data,
optional pointer target and explicit refusal reason.

Keep UI strings/layout primarily in the panel; the helper owns read/format policy.
It can use `FormatWatchValue` for supported scalar bytes with a null backend, but pointer
availability and previews must be determined exclusively through the helper's snapshot reader.
Do not reuse `IsReadableAddress(AppBackend,...)`.

Read policy:

- Restrict this release to captured low/high WRAM and the optional loaded BIOS image.
- Apply [Sh2Address](../../FrontEnd/src/Sh2Address.h) partition/canonical rules and reject device
  areas/register images. Do not reject captured WRAM merely because it is user-declared volatile.
  `CaptureLoadImage::loadFill` rejects volatile data during engine analysis to prevent treating
  it as a fixed value; that is a different contract from showing immutable captured bytes.
  Leave the engine's volatility, load-preservation and constant-folding policy unchanged.
- Validate the complete access, including address overflow and every spanned alias/range,
  using checked/wider arithmetic before calling `MemorySnapshot::Read`.
- Require complete scalar bytes; never zero-fill missing data.
- A preview is at most 16 bytes, read incrementally within allowed spans if necessary. A
  readable prefix may be shown only with an explicit “preview truncated” label; it never
  converts an incomplete scalar read into success.
- ASCII is a byte rendering, not a string decoder. Escape quotes/backslashes and replace
  nonprintable bytes. No Shift-JIS inference, pointer chasing, or NUL-terminated unbounded read.
- Retain original addresses for display; canonical aliases determine which captured bytes to read.
- Register target previews use this same policy. A zero register/pointer value is a null
  candidate; do not automatically preview BIOS address zero for it. Explicit global storage
  at address zero can still be inspected when captured.

Use a named constant for the preview limit. Keep work bounded per hovered token or register:
no per-line backend reads, per-frame RAM copies, or recursive evaluation.

### 4.4 One capture-validity gate

Add a panel helper, proposed name `InspectionAvailability()`, that returns an eligible
snapshot/CPU or a reason. It must check:

- successful shown output, valid current view, and a held snapshot;
- `!ShownStale()`, matching held/shown/current identities, and
  `mShown.key == mSnap->key`;
- view kind is Static, Scrub or LivePaused, never LiveRunning/None;
- register observations use the currently selected panel CPU and require its captured regs.
  DecompileResult is CPU-independent today: SetCpu deliberately retains explicit-entry output
  because both CPUs share the captured memory. Preserve that behavior. On CPU change, clear
  register hover/popup scratch and select the other captured register file; Follow PC still
  uses the existing re-resolution/staleness rules. No CPU field or CPU cache key is needed.

Global/current-memory inspection does NOT need the captured PC to be inside the function,
nor the PC to use the function's execution mirror. A user may browse another function and
inspect its absolute globals. Keep HaltHint's PC/body/mirror-specific logic specific to that
hint. Never relax existing HaltHint or stale-result policy to make inspection available.

Use the existing App reset/reconnect/restore/source-identity mechanisms. Add a narrow
inspection eligibility field/reason to DecompilerView, populated by BuildDecompilerView.
For live views, reject while `mRestoreOutstanding > 0` or `mRestoreUnconfirmable`, as the
existing edit/compare paths do; a timeout does not clear that unresolved restore. Static and
independently selected scrub captures remain labelled captured observations. Preserve App's
emulator-adoption resets and reject any stale capture after adoption. Do not infer readiness
from `mbPaused` alone.

Do not introduce a second independently sampled register file. LiveDriver::SameContent already
compares both SH-2 register files, and CaptureRepeatTracker only suppresses repeated content.
Preserve that existing contract and add a register-only-change regression case; no capture
identity redesign is planned. Mixing new registers with old memory is not a workaround.

### 4.5 UI interaction and actions

Add a pure `TokenAtX(line, x, measure)` helper alongside the panel model. Use the same text
origin and font measurement as DrawLines; input x is relative to the actual rendered text,
including horizontal scrolling. Ignore zero-width tokens, invalid/clamped byte ranges,
indentation and trailing comments. Use half-open bounds and handle UTF-8 text consistently.

Keep the row as the click/navigation target through RowSelectable. Tokens are drawn with a
draw list today; avoid adding overlapping invisible widgets for every token.

Determine the hovered token before choosing a tooltip. A supported inspection tooltip takes
priority; otherwise retain LineOriginText. Unsupported locals can retain provenance help
without a false value. Do not emit two competing tooltips for one row.

When opening a context menu, freeze the clicked inspection target as owned data with the
result/capture identity. Revalidate on activation so CPU switches, a replaced result, edits,
restore or source changes cannot act on stale descriptors.

Actions distinguish:
- “View Object Storage in Memory”
- “View Pointer Target in Memory”
- “Copy Storage Address” / “Copy Pointer Value”
- “Add Object Address to Watch” / “Watch Memory at Pointer Value”

Reuse Request::viewHex/hexAddr and App's existing handler only for targets the Memory viewer
can represent. Resolve the full requested WRAM location with CanonicalAddress and send the
canonical address, retaining the original spelling in tooltips/copy actions. For example,
0x06101234 must navigate to 0x06001234: HexEditorPanel::GoTo only masks to 27 bits and does
not itself fold HWRAM repeats. Do not change the general Hex Editor's address policy here.

The optional BIOS image belongs to MemorySnapshot and is not served by ContextBackend or the
Memory viewer. BIOS tooltips/byte previews and address copying may work, but “View in Memory”
is disabled for that image in this release. Do not add a BIOS backend/region as an incidental
change. Disable target navigation for uncaptured/device/unsupported locations and test it
separately from whether a pointer value can be displayed or copied.

Preserve “Show in SH-2 Assembly”,
“View in Memory” for the instruction, notes, selection, scrolling, copy and function menus.
No editable value cells, writes, breakpoint installation or run-control actions are added.

## 5. Fixed-address Watch integration

This is the last small increment, not a new expression/watch system.

Extend DecompilerPanel::Request with one address-watch intent (address, supported WatchType,
display label). App handles it through AddAddressWatch or WatchPanel::AddWatch and opens
the existing Watch window. For the minimum register-access action, add a separate
show-registers intent carrying the selected CPU; App selects that CPU and opens its existing
Registers window. No new register storage/model is required.

For a scalar global, watch its storage. For a pointer global, clearly offer watching its
pointer storage separately from watching memory at the captured pointer target. For a register,
the action watches memory at the captured numeric value; it does not track the register
or follow a pseudocode local.

Limit this shortcut to canonical, supported WRAM locations/widths. BIOS and addresses the
existing Watch backend cannot faithfully represent do not get an enabled Watch action.
Revalidate the complete location before adding it. This avoids importing the backend's
broader folding and device-image behavior into the new inspector.

The new watch is a fixed address, sampled under the existing Watch panel's current-view
semantics, not a pin of an SSA occurrence. Existing change highlighting is sufficient.
Document that distinction; do not promise source/session-bound expression tracking.
Do not change watch persistence, refresh frequency, parser syntax or history behavior.

## 6. Files and ownership

| File(s) | Planned change |
|---|---|
| FrontEnd/src/Decompiler/Sh2Decompiler.h | Plain inspection descriptor collection and Token reference |
| FrontEnd/src/Decompiler/Sh2Decompiler.cpp | VARREF lookup and bounded export of supported absolute forms |
| FrontEnd/src/Debug/CapturedValueInspection.h/.cpp (new) | Snapshot-only read policy, scalar decoding and bounded byte/ASCII preview |
| FrontEnd/src/DecompilerPanel.h/.cpp | Availability gate, hit test, tooltip, existing-register-window action, optional embedded register section and explicit requests |
| FrontEnd/src/App.cpp | Populate inspection eligibility; route register-window/watch requests and canonical Memory navigation; extract existing register drawing only if embedding |
| FrontEnd/src/App.h | Only if routing/eligibility needs an additional declaration |
| CMakeLists.txt | Register new sources for desktop and affected test targets; FrontEnd.vcxproj already discovers frontend sources through wildcards |
| FrontEnd/src/Sh2RegisterView.h/.cpp (proposed, optional) | Shared snapshot-input register drawing extracted from App::DrawSh2Registers only if embedding is implemented |
| FrontEnd/tests/CapturedValueInspectionTests.cpp (new) | Pure reader/descriptor/hit-test tests as appropriate |
| FrontEnd/tests/Sh2DecompilerTests.cpp | Synthetic engine export fixtures |
| FrontEnd/tests/DecompilerPanelTests.cpp | Real panel interactions and invalidation |
| DECOMPILER_GUIDE.md | Feature scope, labels, pointer/storage/watch distinctions |
| Docs/Codebase/FEATURE_MAP.md; Systems/Decompiler.md | Update when implementation lands; describe partial support, not full variable evaluation |
| Docs/Codebase/Systems/Debugger.md; Systems/MemoryAndHistory.md | Update only where Watch/navigation responsibilities actually change |

Keep the feature behind the existing desktop decompiler build boundary.
Do not make a decompiler-disabled/web build compile engine-dependent code. Update the
explicit CMake test source lists as well as the desktop frontend list. Do not add explicit
entries to the checked-in MSBuild project: its existing source/header wildcards already include
the new frontend files. New helper files remain independent of Ghidra despite using its host-side snapshot model.

No Core ABI, live protocol, Mednafen patch, vendored engine/spec, extra worker, or cache-key
redesign is expected. Do not refactor Assembly hover onto the new reader as part of this work;
it is a separate behavior change with separate compatibility questions.

## 7. Milestones and acceptance gates

### P0 — Establish contracts and fixtures (small)

- Implement the snapshot-only helper and availability result.
- Add pure tests for endian decoding, reads, policy, bounds and refusal reasons.
- Add a synthetic metadata fixture that demonstrates the two global printer forms and at
  least a scalar load/store; inspect real markup before finalizing binding rules.

Gate: no App/backend/transport read is possible through the helper; all unsupported mappings
produce no binding. Record any printer-form limitation explicitly.

### P1 — Reuse register access; optional embedding (small)

- Add “Open Registers” for the pseudocode-selected CPU, using the existing window and actions.
- If embedding is desired, extract and reuse the register-drawing widget described in section
  3.2, then add bounded snapshot previews and copy/navigation actions there.
- Preserve the captured/source labels and uncertain-PC wording in any embedded view.

Gate: the open-window request carries the selected CPU. If embedding is implemented, changing
CPU clears CPU-specific hover/menu state; source/frame/edits/restore readiness gate inspection;
running/stale output shows no embedded values; displayed values exactly equal the fixture
snapshot; register Memory actions use supported canonical targets. Existing Registers-window
behavior and menus remain unchanged by optional extraction.

The existing-window shortcut can ship before global metadata is complete. Embedding is not a
release prerequisite, and it does not justify creating a second register viewer.

### P2 — Absolute global hovers (small-to-medium, largest piece)

- Add the result schema and supported metadata export.
- Integrate token hit testing, scalar/pointer tooltips and storage/target navigation.
- Exercise direct globals and null-VARREF global printer forms, including writes.
- Keep unsupported partial/global forms as provenance-only hover.

Gate: synthetic scalar loads/stores expose correct absolute storage and width; hovering an
earlier read after RAM changed explicitly shows captured memory, never a recovered old result.
Pointer storage and target are tested independently. No local is “resolved” from a register name.

### P3 — Existing Watch shortcut and documentation (small)

- Route supported fixed-address watches through App into the existing Watch panel.
- Reuse its current change highlighting.
- Update the user guide and affected codebase maps in the implementation change.

Gate: a watch of pointer storage is distinct from a watch of its captured target; no dynamic
register/local tracking is implied. BIOS/device/invalid Watch actions remain unavailable.
All existing line navigation, context menus and HaltHint behavior remain intact.

## 8. Verification matrix

Tests below are the implementation requirements from the original planning task. Current results are recorded in [VALIDATION.md](VALIDATION.md).

| Area | Required cases / oracle |
|---|---|
| Reader and decoding | Distinct byte patterns at widths 1/2/4; signed extrema; bool; raw unsupported type; complete-read refusal |
| Address policy | Cached/cache-through WRAM; documented HWRAM repeats; absent/partial regions; null; valid explicit BIOS address zero; purge/cache-array/on-chip; VDP and other hardware refusal; user-volatile WRAM still readable as captured bytes; address-space overflow; range crossing |
| Preview | 16-byte cap, escaped/nonprintable ASCII, truncated prefix explicitly labelled, no recursive pointer chase, no backend read after initial capture |
| Engine metadata | Whole absolute scalar read and write; pointer global; pointer literal; null-VARREF global; address-of form; negative tests for locals, stack spaces, partial symbols, array/field syntax, dynamic/split mappings and zero-width LOAD provenance |
| UI hit testing | Indentation, font scaling, horizontal scroll, UTF-8 token ranges, whitespace, adjacent boundaries, zero-width provenance and trailing comments |
| UI behavior | One tooltip, row selection/navigation retained, correct object/target menu, existing Registers-window shortcut selects CPU; optional embedded register CPU; absent regs with readable globals |
| Memory navigation | HWRAM-repeat target 0x06101234 produces 0x06001234; cached/cache-through aliases; BIOS preview remains readable but Memory action disabled; device/uncaptured target disabled; original address preserved for copy |
| Freshness | Running, stale while replacement is pending, hidden/reopen, CPU change, memory edit, same-frame changed capture, source replacement, reconnect, scrub, BIOS add/replace/remove and restore transition |
| Popup lifetime | Open menu, then replace source/result/CPU or edit memory before activation; stale request must not fire |
| Watches | Correct address/type/label; pointer storage versus target; frozen register address; unsupported BIOS/device action disabled |
| Existing behavior | TestRegisterHintAtHalt, install/current-view rejection, mirrors, scrub cache and line-origin/menu tests remain passing |

Use deterministic synthetic captures and the real panel harness. Existing
[DecompilerPanelTests.cpp](../../FrontEnd/tests/DecompilerPanelTests.cpp) already supplies
a MockBackend read counter, handshakes, Memory panel edits and real runner/panel operation.
Count backend reads after capture and assert hover/register previews do not increase them.
Use explicit assertions on produced values/requests rather than screenshots alone.
No full per-node emulator oracle is needed for a feature that only exposes captured bytes
and registers; that oracle belongs to later runtime-value recovery.

Build and relevant checks from SaturnExplorer:

```sh
cmake -S . -B build -DSE_ENABLE_DECOMPILER=ON
cmake --build build -j8
ctest --test-dir build --output-on-failure -R 'CapturedValueInspection|Decompiler|MemorySnapshot|Sh2RegInfo|MemoryBackend|ExpressionResolver|Watch|PanelInteraction'
```

Register the proposed pure test executable with CTest when implemented. Existing target
availability follows CMake/platform guards. Inspect test output for skipped optional fixtures;
a skipped real-game fixture is not a substitute for the required synthetic cases.

Also configure/build with SE_ENABLE_DECOMPILER=OFF to check compile boundaries, and perform
Windows/macOS UI checks for scroll alignment, menu ownership and copied addresses. Broader
engine/vendor test gates are required only if implementation unexpectedly changes those
components; such scope expansion is not part of this plan.

## 9. Completion and follow-on boundary

The release is complete when P0–P3 pass, useful global/pointer inspection and access to the
existing Registers window are available, the UI makes capture timing explicit, and
source/CPU/capture changes cannot produce a mixed view. Optional embedded register rendering
is not required for completion.
Unknown locals and expressions are an expected limit, not a reason to add guessed mappings.

A subsequent proposal may add proven register-backed variables, fixed stack slots, pure
expression evaluation and caller frames. It must separately establish storage/value
correspondence, execution-point validity, loop/merge semantics and memory-history rules.
This plan neither depends on nor signs off the earlier “last writer solves everything” design.

## 10. Planning validation

This document is based on source and codebase-document inspection at the revision recorded
above. Relative links and cited existing source/test paths are checked against that repository
tree. New names/files and proposed APIs are labelled as proposed.

The planning environment had no usable execution runtime. No application build, tests or
native UI run was performed for this documentation-only task. The checks in section 8 are
implementation acceptance criteria, not claimed results.

Revision 2 corrects MSBuild wildcard handling, makes existing register presentation/navigation
reuse explicit, defines canonical WRAM navigation and BIOS navigation refusal, distinguishes
captured volatile RAM inspection from engine constant-folding policy, and records existing
formatting/condition/capture-identity support. Global token-binding coverage remains a
source-supported proposal until the P0/P2 synthetic fixtures pass; neither complete coverage
nor a guaranteed small implementation is claimed.

## 11. Implementation coverage

P0–P3 use the existing Registers window; optional register embedding is not implemented.
The result exports owned absolute-object and constant-address metadata. The engine adapter
handles whole direct VARREF globals, whole null-VARREF spacebase loads/stores and address
forms, typed pointer literals, and registered volatile read/write annotations. Explicit
pointer temporaries preserve address-of semantics. Captured reads remain separate from
engine volatility and constant-folding policy.

Deterministic tests cover both packed markup forms and reject partial, split, dynamic,
stack, array and field mappings. Snapshot and real-panel tests cover bounded reads,
canonical storage/target actions, Watch distinctions, restore and capture freshness, CPU
selection and popup lifetime. See the validation record for exact executed checks and
remaining native-platform visual checks.
