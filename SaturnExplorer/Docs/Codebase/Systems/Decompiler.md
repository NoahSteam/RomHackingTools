# SH-2 pseudocode and embedded decompiler

Inspected revision: `ff6a3b1fb1c54e4c0344613094ea65292b0023c2` (2026-10-20); the pseudocode-to-assembly mapping (steps 7 and 9) rechecked at the area 7 fixer branch `review/7-a710-fix` (2026-10-30), and step 7's store tokens at `review/7-a719-fix` (2026-11-04), `review/7-a722-fix` (2026-11-05), `review/7-a726-fix` (2026-11-05) and `review/7-round7-fix` (2026-11-06); step 7's endless-loop store caution at `review/3-a3167-fix` (2026-11-08).
Captured-inspection sections rechecked against `codex/decompiler-variable-inspection` (2026-10-20).
The panel's status/trust drawing, Decompile-as-a-request and Copy marks rechecked against `5428fe6` plus the area 8 fixer branch (2026-10-30).
Unless labelled otherwise, behavior below is verified from the cited implementation.

## Purpose and responsibilities

The desktop **SH-2 Pseudocode** panel decompiles captured Saturn code into navigable,
syntax-colored pseudocode. It supports both SH-2 CPUs, follow-PC and explicit entries,
function-start corrections, function names, assembly-note overlays, optional BIOS bytes,
hardware-register names and cautions, volatile-memory ranges, and uncertainty/staleness
indicators. It is an analysis of a captured image, not an execution of the game.

The integration lives in the frontend, outside the portable rendering core. Ghidra owns
SLEIGH translation, SSA/data-flow analysis, structuring, and C printing. SE supplies the
captured load image, Saturn hardware semantics/configuration, function hints, conservative
signature lending, output/source mapping, asynchronous scheduling, and UI validity policy.
See [`Sh2Decompiler::DecompileInto`](../../../FrontEnd/src/Decompiler/Sh2Decompiler.cpp)
and [`SaturnArchitecture::Initialise`](../../../FrontEnd/src/Decompiler/SaturnArchitecture.cpp).

## Source locations and important types

| Responsibility | Source and important symbols |
|---|---|
| App integration and capture identity | [`App.cpp`](../../../FrontEnd/src/App.cpp): `BuildDecompilerView`, `UpdateDecompiler`, `LoadBiosImage`, `ResetSessionDebugState` |
| Panel refresh, cache, trust and interaction | [`DecompilerPanel.h`](../../../FrontEnd/src/DecompilerPanel.h), [`DecompilerPanel.cpp`](../../../FrontEnd/src/DecompilerPanel.cpp): `DecompilerView`, `DecompilerInputs`, `DecompilerPanel::Update`, `Accept`, `ShownStale`, `Uncertainties`, `DrawLines` |
| Worker lifecycle and request arbitration | [`DecompilerRunner.h`](../../../FrontEnd/src/Decompiler/DecompilerRunner.h), [`DecompilerRunner.cpp`](../../../FrontEnd/src/Decompiler/DecompilerRunner.cpp): `DecompilerRunner`, `RunnerOptions`, `Submit`, `Poll`, `InvalidateSession`, `WorkerMain` |
| Immutable memory/register capture | [`MemorySnapshot.h`](../../../FrontEnd/src/Decompiler/MemorySnapshot.h), [`MemorySnapshot.cpp`](../../../FrontEnd/src/Decompiler/MemorySnapshot.cpp): `MemorySnapshot`, `SnapshotKey`, `MemoryRegion`, `BiosImage` |
| Engine boundary and serializable results | [`Sh2Decompiler.h`](../../../FrontEnd/src/Decompiler/Sh2Decompiler.h), [`Sh2Decompiler.cpp`](../../../FrontEnd/src/Decompiler/Sh2Decompiler.cpp): `DecompileRequest`, `DecompileResult`, `Prototype`, `CallEffects`, `Line`, `Token`, `MemoryAccess`, `AccessFate` (with the frame's own ends, `FrameUnread`, `FrameRemoved`, `MalformedPiece`, and `DecompileResult::frameCalls`, which the tests' frame ledger reads: patch 0068), `Sh2Decompiler` |
| Architecture and engine load image | [`SaturnArchitecture.cpp`](../../../FrontEnd/src/Decompiler/SaturnArchitecture.cpp): `Initialise`, `StartLibrary`, `buildTranslator`; [`CaptureLoadImage.cpp`](../../../FrontEnd/src/Decompiler/CaptureLoadImage.cpp): `loadFill`, `SetVolatile` |
| Containing-function discovery | [`Sh2FunctionFinder.cpp`](../../../FrontEnd/src/Decompiler/Sh2FunctionFinder.cpp): `FindFunctionContaining`, `RtsScanEntry`, `CallTargetAt`, `FunctionBounds` |
| Hardware names, access policies and aliases | [`SaturnHardwareMap.h`](../../../FrontEnd/src/Decompiler/SaturnHardwareMap.h), [`SaturnHardwareMap.cpp`](../../../FrontEnd/src/Decompiler/SaturnHardwareMap.cpp): `SaturnHardwareRegisters`, `HardwareRanges`, `VolatileAliases`; [`Sh2Address.cpp`](../../../FrontEnd/src/Sh2Address.cpp): `CanonicalAddress`, `ClassifyPartition` |
| Snapshot-only global/pointer inspection | [`Debug/CapturedValueInspection.h`](../../../FrontEnd/src/Debug/CapturedValueInspection.h), [implementation](../../../FrontEnd/src/Debug/CapturedValueInspection.cpp); owned [`InspectionTarget.h`](../../../FrontEnd/src/Decompiler/InspectionTarget.h) |
| Concrete indirect target at captured halt | [`RegisterHint.cpp`](../../../FrontEnd/src/Decompiler/RegisterHint.cpp): `RegisterTargetAtHalt`; `DecompilerPanel::HaltHint` |
| Runtime language materialization | [`SpecBundle.cpp`](../../../FrontEnd/src/Decompiler/SpecBundle.cpp): `MaterialiseSh2SpecBundle`; generated [`Sh2SpecData.h`](../../../FrontEnd/src/Decompiler/Sh2SpecData.h) |
| Names shared with debugger | [`CallStack.h`](../../../FrontEnd/src/Debug/CallStack.h): `FunctionNames` |

## External files and their loaders (area 9)

Every file the decompiler's path reads is read through a loader with a ceiling, a verdict and a message; the oracle that
holds them is `LoaderFuzzTests` (see `Docs/FunctionalityVerification/15_DECOMPILER_STEP5.md`, "Area 9, round 1").

| File | Loader | Rule |
|---|---|---|
| BIOS image (path in `settings.ini` `[decompiler] bios`) | `LoadBiosImageFile` (`Decompiler/BiosImageFile.cpp`), called by `App::LoadBiosImage` | A regular file of 1 byte to 512 KiB; stat first, read to one byte past the ceiling. A failed Reload drops the image. |
| `settings.ini` | `Settings::Load` / `Save` | Up to 4 MiB; a lone CR ends a line; saved by merging this session's changed keys into the file as it is, line by line, under a lock (comments, order and unknown lines are kept; a changed key is rewritten where it stands, a new key goes after the last key of its section); a leading byte-order mark is ignored (and dropped by the next save); a line end character in a value is written as a space. A key set back to the value it was loaded with is not written (A9-35, accepted). |
| `saturn_functions.txt`, `saturn_symbols.txt`, Load Symbols | `FunctionNames::Load` / `Import` / `Save` | `<hex-address> <name>` (`AddressNameLine.h`), up to 8 MiB; other lines are skipped and counted (and kept by the next save); a word of the letters a-f alone ("dead", "add", "a") is a word, never an address (A9-18, A9-41), and neither is an unpadded number below 0x80000 without `0x` ("2026", "12", "3D": the start of a remark, A9-45); the Log warning says the line may be an old unpadded address and should be written with eight digits (`0000DEAD`; A9-32); a remark that opens with an eight-digit date still reads as an address; a leading byte-order mark is ignored; a name is saved trimmed, a line end in it as a space, and a blank one deletes the entry (`CleanEntryText`, A9-31, A9-36); saved with eight digits. Save: `SaveStore` (`StoreMerge.h`). |
| `saturn_function_bounds.txt` | `FunctionBounds::Load` / `Save` (`Sh2FunctionFinder.cpp`) | An address as the first word, a remark after it allowed; only eight hex digits or `0x` and up to eight are starts (the file has always been written with eight, so a first word of fewer digits, "2026 trace", "12 Nov", is a skipped, counted line, A9-45); up to 4 MiB; an address in no memory (`CanonicalAddress` refuses) is counted as skipped. |
| `assembly_comments.txt` | `AssemblyPanel::LoadComments` / `SaveComments` | As the names file, up to 8 MiB. Failed saves after an edit go to the Log (`SetSaveErrorHandler`). |
| Volatile list text | `ParseAddressRanges` (`Decompiler/AddressRanges.cpp`) | Capped at 2047 characters by `SetVolatileMemory`. |
| Language files | `MaterialiseSh2SpecBundle` | Each file read to one byte past its embedded size, regular files only. |
| Savestates, dumps | `se_savestate_open*` (`StateSources.md`) | A region of a state is whole or absent; a dump may hold a region in part, and `MemorySnapshot::Capture` then leaves it out. |

**Saving the user's files (A9-19 to A9-21, A9-23, A9-28, A9-33, A9-37).** `StoreMerge.h` (`SaveStore`) is the one save of the names, notes, function starts, and (by the same rules, in `Settings::SaveTo`) `settings.ini`: a file the loader refused is never written over (the store is marked not-loaded, and a save that would change it is refused with a message); a store that changed nothing since the load writes nothing; otherwise the file is read again under a `FileLock` on `<file>.lock` (the wait for another process's lock is bounded, `FileLock::kWaitMs`, 3 s, A9-40, and 10 s once `App::Shutdown` has called `FileLock::SetClosing(true)`; a save still refused at exit is said in a message box, `NoticeAtExit`, A9-46; the settings retry in a frame waits at most 50 ms for the lock (`Settings::Save(error, false)`, `FileLock` `tryMs`, A9-52, A9-59), and a later successful save clears its "could not save" status; the exit's 10 s wait is one budget shared by every store (A9-53), so `App::Shutdown` saves the user's work first (notes, names, starts) and the settings and the watch list after (A9-58), and a refusal text says what was waited or that the exit time ran out (`FileLock::WhyNotHeld`); a refused write or flush reads `<subject>: not saved, <reason>` with the path once, and a trailing full stop of an OS message is cut (A9-60, A9-62); the Watch panel's `saturn_explorer_watches.json` is written with `WriteFileAtomically` and a failure is logged and listed at exit (A9-57); the notice at exit carries each store's own error (the current attempt's, with the "kept in memory only" tail cut, A9-61) and is a message box on Windows and on the SDL2 builds (`SetExitNoticeHook`, set in `WebMain.cpp`), stderr otherwise; a refused sync names the target file and, on Windows, keeps `GetLastError`; on macOS only `ENOTSUP` or `EINVAL` from `F_FULLFSYNC` falls back to `fsync` (A9-55); a lock that cannot be opened or taken in that time refuses the save, with a message that gives the cause (the OS reason when the file cannot be created: a folder that cannot be written or is gone, A9-42; or the wait that ran out) and names the lock file, and the session keeps its changes pending: an unlocked save could lose another instance's entries; `Settings::SaveTo` does the same; a pending save of names, notes and function starts is tried again by `App::Shutdown`, A9-43; the Windows `LockFileEx` loop is the same bound and retries only `ERROR_LOCK_VIOLATION` and `ERROR_SHARING_VIOLATION`, A9-47, not run on Windows), the `<file>.separt-<pid>-<n>` staging files that killed writers left are removed (`RemoveStaleStagingFiles`; only dead writers' or ten-minute-old regular files of that exact name; the empty `.lock` files stay), this session's adds, edits and deletes are applied to its lines (the rest is kept as written), and `WriteFileAtomically` writes it (the staging file is `fsync`ed, `FlushFileBuffers` on Windows, before the rename and the folder after it on POSIX, A9-48), keeping the permission bits and refusing a read-only target. The three files are opened at `WorkFilePath(...)` (`WorkFiles.h`), anchored at the folder the application started in. Not run on Windows: `OFN_NOCHANGEDIR` on the open and save dialogs (`WindowsPlatform.cpp`).

`FileRead.h` (`ReadFileCapped`) is the one bounded reader (a link loop is Unreadable, so the loader refuses the file and no save replaces the link, A9-38; it opens with `O_NONBLOCK` and looks at the descriptor, so a path swapped for a FIFO after the stat is refused); use it for a new file rather than `ReadWholeFile`, which reads to
the end of whatever it is given.

What the save after a load does is the other half, and round 2 of area 9 found it open (A9-19 to A9-21, A9-28 in
`REVIEW_LEDGER.md`): `App::Shutdown` saves the names and notes at every exit and `settings.ini` at the exit whatever their
`Load` returned, the function starts at the next change, each from the entries in memory only. So a file a loader refused,
lines it skipped, comment lines and remarks, and what a second instance in the same folder saved are written over; a save
also replaces a read-only or hard-linked file. The `lifecycle` module of `LoaderFuzzTests` drives the classes in that order
and lists these classes as open until they are fixed.

## Entry points and execution flow

1. `App` starts its single runner with `DecompilerRunner::Start`. `App::UpdateDecompiler`
   executes each UI frame, including when the window is hidden. Assembly, Call Stack and
   Registers invoke `DecompilerPanel::ShowFunctionContaining` through App. Like Enter in the
   entry box it is a request: it sets `mAskAgain`, so the same question is asked again after
   a Stop or a failure, and it clears a refused entry's text from the box (`SetTarget`).
   The entry box remembers the text the panel last wrote (`mBoxCleanText`): an edit taken back to it
   (Escape, Backspace, Ctrl+Z) leaves the box untouched (A8-46), and a refused text stays in the box
   with its refusal even when an install left the sync pending (A8-50).
2. `App::BuildDecompilerView` describes the **context currently displayed**. A scrubbed
   recording uses its recorded context and stable timeline/frame identity. Live capture
   repeats are normalized by `CaptureRepeatTracker` so unchanged paused captures do not
   repeatedly invalidate analysis. It collects both CPUs' registers and optional BIOS.
3. `DecompilerPanel::Update` updates view identity and engine inputs **before** accepting
   completed work. A visible panel captures memory according to its source/refresh policy,
   then resolves a containing function with `FindFunctionContaining` or validates the
   explicit entry. A hidden panel polls results but neither captures nor submits work.
4. `MemorySnapshot::Capture` copies complete low/high work-RAM regions through
   `ReadRegionBytes(IMemoryBackend, ...)`, copies optional BIOS, and copies available
   register files. A region that cannot be read completely is omitted. The returned
   `shared_ptr<const MemorySnapshot>` crosses to the worker; the worker never reads the
   changing App/context/backend directly.
5. `DecompilerRunner::Submit` coalesces queued jobs by requester slot and cancels an older
   running job for that slot. `WorkerMain` calls `Sh2Decompiler::Decompile` serially.
6. `Sh2Decompiler::DecompileInto` validates partition/capture/even-address entry, starts the
   engine library, and runs fresh-architecture passes. Normally the first discovers literal
   words and constant callees; the printing pass registers those callees and their actual
   execution aliases, applies compatible lent prototypes and read-only literals, follows
   flow, runs engine actions, and emits packed markup. Discovered ordinary-memory aliases
   can cause bounded extra passes with those ranges made volatile. This is more than an
   unconditional fixed two-pass pipeline.
7. `LinesFromMarkup` converts engine markup into plain `Line`/`Token` data, canonical
   instruction provenance, call targets and conservative owned inspection bindings. A token's
   address is `InstructionOf(op)`: the translator gives a delayed branch and the instruction in its
   delay slot one address, so the engine marks the ops the slot built (and the ops made for them;
   `PcodeOp::isFromDelaySlot`, patch 0075, and the pieces heritage makes for them, patch 0079) and
   the host addresses them at the branch + 2.
   `AddFrameStoreTokens` gives a line that prints a frame local or a global where no instruction
   writes it (a COPY or a conversion at any instruction, any op at a call or a load) an empty token
   for each folded store whose value it shows (the engine's `AccessRecord`s): found back through
   the final basic blocks' address covers, with a store whose block the engine emptied placed on
   the edges through its flow block, else forward (`FlowRecord::insnBlock`). A copy at the end of
   its block that shows a store's value takes the store's address for its own tokens. A store is
   named only when its value may be the statement's (`StoreShowsValue`): `RecordFlow` keeps the
   flow's ops (`FlowRecord::blockOps`, before the actions), the stored register is followed back
   through copies, and through the frame and globals to the store that wrote them, to where the
   value is made, and compared with what the statement's value copies, merges or extends in the
   final p-code; a store whose every path ends at another value is never named, and the search
   goes on to the store the statement is ahead of. The walk goes through arithmetic the engine
   folds to one of its values (`FlowIdentitySources`: the op's value is built back through its
   block, single predecessors and each path through a join, to numbers known on every path in --
   `FlowNumberBefore`, which reads a branch's condition on the edge taken, frame words and
   non-volatile RAM a store of the flow wrote, and registers a callee's lent prototype keeps
   (`CallKeeps`) -- and evaluated on test values: `add r4,r3` with r3 0 after a `dt` loop, `and
   r1,r1`, `xor #k` twice; A7-31); a path a known condition never takes is left out
   (`EdgeDead`), a loop's way back into a block that dominates it, on which nothing writes the
   value, brings what the other edges bring (`LoopKeeps`, A7-39); when the op's value is all
   numbers the walk knows (`mov #0,r2 ; xor r1,r2` with r1 0 after a `dt` loop) the build is
   retried on the op's own block, every value at its start a leaf; and a statement that shows a number takes the number the walk knows the store
   wrote. A statement that shows only an input or a variable shows a number only when a condition of
   the flow compares a storage with that number (A7-41: a copy of `param_2` the engine put at a store
   of 3 is not that store's). The number walk passes a frame word over a store through a pointer it
   cannot place when no address of the frame leaves the stack pointer and the temporaries its accesses
   compute (`FlowRecord::frameKept`, `FrameKept`), as the engine does when it forwards the word (A7-38). A number is compared on the bytes a store writes of the variable, big-endian, and of
   those the ones no other store of it writes (A7-36); a number the engine put in a copy of its
   own is that number only (A7-35). A copy at a call or a return names only stores that run
   before it when one does (A7-32), unless its value is the call's own output. An emptied store is on an edge also through flow blocks the
   engine emptied too and through the flow blocks of one instruction the flow splits (`mac.w`), and the forward search passes a store of another value. A conversion at
   the branch ending its block takes its store's address as a copy there does. A number put at an instruction that did not
   make the value a store stores takes its store's address too: the instruction must make the
   number into the register the store stores AND that value must reach the store (`FlowLastWriters`
   of the register from the store, the whole register for a half-word store; a delay slot that
   makes the number into another register is no maker, A7-28); a named store the number does not
   reach, when it reaches another store of the same bytes, is dropped. A number put in a register the
   engine merged under a memory variable's name, at an instruction that makes the number into that
   register, names only the stores that instruction's value reaches (`FlowLastWriters` from the store):
   not a `tas.b` (no register) nor a store of the register after a loop changed it, whatever the value
   walk cannot rule out (A7-44); a store of the function's input is no such store (`FlowLastWriters`
   with `input`, A7-46). A copy of memory the engine put anywhere in its block at an instruction that
   does not read that memory (materialised at an access that may alias it) is drawn at the nearest
   load of it in the block, as a copy at the block's end is (A7-49). The value walk follows the T bit
   that `movt` reads (`sr & 1`) back to the compare that set it (A7-48). A register the engine merged under a memory variable's
   name, a copy at the end of its block that no store shows, is addressed to the instructions that
   last set the register on each path into the block's end (`FlowLastWriters`), or to nothing,
   never to the branch (A7-21). A statement that keeps its drawn tokens at its instruction beside
   the stores it names (a copy the engine materialised at a call or a store that may alias) marks
   them `Token::materialised`, and `InstructionLineIndex` offers the instruction's own statement
   before it (A7-25). `AddJoinConstantTokens` addresses a number the engine
   merged (a COPY that is no flow op, at an instruction that does not make the number into the
   COPY's register: a collapsed MULTIEQUAL at a join; or a copy of a temporary the engine made of a
   number and prints in place, A7-40's triage, even on a line it shares with that copy, A7-47) to the instruction on each path that set the
   register, or to nothing. A case label's token is where the jump table sends its value
   (`JumpTable::getLabelByIndex`/`getAddressByIndex`; the table of the `switch` printed at the
   label's own indent, so a case after a nested switch is not the inner one's, A7-29), the
   destination itself when a block of the final graph holds it, else the first instruction of the
   flow's block that does (a case that is only a branch or a nop, A7-27). For a table without
   labels: the destination in its block, else the start of the run
   of the block's addresses that holds the case's first op (a block the engine merged out of
   address order covers several runs; a delay slot is no gap). A COPY the
   engine made to trim a cover and that feeds a folded read is addressed to the read
   (`LinesFromMarkup`). `DecompileResult::flowBlockRanges` is for the oracle. Tokens at a word flow
   halted at carry `haltsFlow`. The result records flow body ranges, warnings,
   hardware notes, memory-access provenance and optional recovered prototype/effects.
   `NoteRemovedAccesses` may additionally analyze an all-volatile reference when unreachable
   removal needs checking. `StoresBeforeEndlessLoop` (A3-167 to A3-169) reads the final p-code
   for stores of ordinary memory the text does not print (no op of the store's instruction that
   the text prints as more than a keyword, or that is a copy within a variable the text prints
   another write of, writes all its bytes) from which a flow path reaches a
   final block that reaches no RETURN and lies on a cycle, with no later store of the code's
   rewriting those bytes first (`DecompileResult::storesBeforeEndlessLoop`);
   `NoteStoresBeforeEndlessLoop` says each and withholds the signature. Neither depends on the
   engine's "Do nothing block" warning. Engine objects are destroyed before results reach the UI.
8. `Poll` returns current-generation/current-epoch work. `DecompilerPanel::Accept` also
   checks the snapshot, requested execution entry, names, function set, current lent
   prototypes, volatile ranges, and whether the held capture still represents the view.
   Accepted successful results enter the panel's eight-entry LRU and `Install` learns
   eligible signatures. Old visible output stays during replacement and is labelled stale.
9. `DrawLines` draws the result and overlays assembly comments/hardware explanations and
   a narrowly applicable captured-register target hint. A click navigates to the first
   originating instruction; the context menu exposes additional contributing instructions,
   function navigation/corrections, rename and memory navigation through `Request` values
   that App handles. The way back is `AssemblyPanel`'s **Show in Pseudocode** (`Request::showPseudocode`)
   -> `DecompilerPanel::ShowInstruction`, which waits for the function to be shown and then
   selects and scrolls to the line `LineForInstruction`/`InstructionLineIndex` (Sh2Decompiler.h) names;
   an instruction folded into another's line gets that line and a note. With Follow PC at a stop
   each installed result selects the PC's line.
   `DrawStatus` lays the status row out as wrapping text (`StatusRow`: a piece
   that does not fit starts the next row) and wraps the amber and red sentences, so the stale
   badge and "decompiling…" are on screen at any panel width (the default dock is 537 px on a
   1280 px window). Copy Line and Copy Function prefix `CopyMarks()` (a `/* [stale] … */`
   comment and one `/* WARNING: … */` per amber line) to the pseudocode they put on the clipboard.

```mermaid
flowchart TD
    V["App: displayed context and registers"] --> P["Panel: capture, resolve, validate"]
    P --> S["Immutable MemorySnapshot"]
    S --> R["One DecompilerRunner worker"]
    R --> G["Fresh SaturnArchitecture passes"]
    G --> O["Plain DecompileResult"]
    O --> P
    P --> U["Pseudocode and debugger navigation"]
```

## Interactions

- `App::UpdateDecompiler` supplies `FunctionNames::All/Generation`, `FunctionBounds`,
  Assembly user comments, and matching-origin Call Stack function hints. Call Stack hints
  are supplied only when their source/derive/frame identity matches the snapshot.
- Function-start corrections are persisted by `FunctionBounds::Load/Save` as
  `saturn_function_bounds.txt`; function naming uses the shared `FunctionNames` model.
  `App::LoadSettings/SaveSettings` persists auto-refresh, volatile-range text and BIOS path
  under the `decompiler` settings section. Generated results, LRU and learned signatures
  are session state, not an on-disk analysis database.
- Core `se_get_sh2_regs`, `se_frame_number`, `se_derive_serial`, frontend memory backends,
  live restore/capture tracking and recording context swaps determine the identity of
  what may be analyzed. Review [Debugger.md](Debugger.md),
  [MemoryAndHistory.md](MemoryAndHistory.md), and [StateSources.md](StateSources.md)
  before changing capture/restore/edit behavior.

## State, lifetime and invalidation

- Snapshot keys contain source ID, derive serial, frame number and region-set hash. The
  latter includes optional BIOS content hash; work RAM contents rely on the source's
  identity/serial contract rather than being hashed afresh. `BiosImage::Make` hashes bytes
  at load, including replacement at the same path/size.
- Static, paused and scrub views refresh on changed identity. Running views keep a capture
  until explicit action or opt-in debounced follow-PC refresh (30 UI ticks, only when the
  followed PC has left the displayed function, with guards against repeated unanswered or
  over-budget requests). Source and BIOS changes force recapture when visible. Hidden
  panels still reject results for an obsolete source/BIOS.
- `LearnedPrototype` is keyed by **execution address**, snapshot and current function/
  volatility inputs, plus shapes of signatures lent for its callees. `Learn` transitively
  removes prototypes dependent on a changed or removed callee signature. Canonical memory
  identity does not make mirror-executed signatures interchangeable: PC-derived values can
  differ. `PrototypeHash` participates in both request validity and caching.
- `ResetSessionDebugState` invalidates the runner epoch and resets panel session state. `App::Shutdown`
  resets the runner; its destructor cancels, joins the worker and shuts down the
  SLEIGH library. It never detaches a worker or creates a competing replacement thread.

## Extension points and task routing

| Requested change | Inspect first / reuse |
|---|---|
| Add pseudocode interaction, overlay or navigation | `DecompilerPanel::DrawLines`, `Request`, `App::UpdateDecompiler`; use existing line provenance and shared `RowSelectable` |
| Change capture, refresh or restore behavior | `App::BuildDecompilerView`, `CaptureRepeatTracker`, `MemorySnapshot::Capture`, panel `Update/Accept`; extend panel/model/live capture tests |
| Improve function discovery or correction | `FindFunctionContaining`, `FinderHints`, `FunctionBounds`, panel `Resolve`; preserve explicit user entries and uncertainty reporting |
| Improve recovered calls/types | `ApplyPrototype`, `WithholdReason`, `ReturnCheck`, `CallEffects`, panel `Learn/PrototypeHash`; preserve snapshot/dependency/mirror provenance |
| Add hardware names or semantic notes | `SaturnHardwareMap`, `HardwareNotes`, `HardwareWidthTableTests`, register audit; verify actual widths/aliases/read-write semantics |
| Fix engine or SH-2 translation | `VENDOR.md`, `patches/`, semantic/generated/differential tests; regenerate spec/header when language files change |
| Extend captured inspection or propose runtime variable recovery | Start at `InspectionTarget`, `CapturedValueInspection`, `LinesFromMarkup`, panel inspection gate/`DrawLines`; preserve the capability boundary below |

### Captured values and the variable-evaluation boundary

The desktop panel can inspect metadata-proven whole absolute globals and typed address forms
against its immutable held `MemorySnapshot`. `Token::inspectionTarget` is a one-based reference
into the result's owned inspection target collection; zero means unbound, so storage address zero
is representable. Engine metadata is copied while Funcdata/markup exist. No engine pointers,
memory values or preview strings enter the result cache. The engine-side
`InspectionBinding` adapter decodes both VARREF whole globals and null-VARREF spacebase
forms. Explicit `&global` temporaries retain address semantics; implied whole-object loads
and stores retain storage semantics. Registered volatile read/write annotations use their
exact bus address and operation width, with the same whole-symbol checks. Partial, split,
dynamic and ambiguous mappings are refused.

`Debug/CapturedValueInspection` owns read/decode/preview policy with no App, transport or Ghidra
dependency. It permits captured low/high WRAM and optional loaded BIOS, checks complete ranges
and overflow before reads, and caps target previews at 16 bytes. Hardware/device areas and the
general backend's broader mappings are excluded. Volatile captured WRAM remains readable here;
the engine's separate volatile-load/constant-folding policy is unchanged. Unsupported scalar
types remain raw bytes rather than acquiring an integer interpretation.

The panel's shared inspection gate requires successful current output, matching shown/held/view
identities, a matching result/snapshot key and a Static, Scrub or LivePaused view. App supplies
an additional live-restore readiness reason; unresolved and unconfirmable restores remain
ineligible even after timeout. Absolute globals do not require registers or a PC inside the
function. `HaltHint` retains its existing stricter PC/body/mirror checks. Frozen popup targets
are revalidated against capture/result/CPU state before requests fire.

Memory actions canonicalize complete supported WRAM locations before `Request::viewHex` reaches
App; original addresses remain available for copying. The optional BIOS lives only in the
snapshot, so previews/copying may work while Memory and Watch shortcuts remain unavailable.
Object storage and captured pointer targets have separate actions. `Request::addWatch` routes
a fixed canonical WRAM address, type and label through App into the existing `WatchPanel`;
normal current-view refresh/change highlighting applies. `Request::showRegisters` selects the
same CPU in the existing Registers window, including its SH-2 tab and existing actions. No
second register renderer or independently sampled register file is introduced.

Register/stack locals, partial or ambiguous global forms, arrays/fields and general expressions
remain unbound. Instruction origins, OPREF outputs and Varnode register addresses do not prove
runtime variable storage. A tooltip reports **captured memory**, not the value at a prior read
or assignment occurrence. General optimized-variable lifetime recovery and historical/frame
reconstruction remain follow-on work.

## Constraints and integration contracts

- Ghidra's translator and registries are process-global. Exactly one live runner claims the
  engine, and architectures are sequential and fresh. Keep Ghidra types inside the engine
  wrapper; public request/result/snapshot interfaces are owned C++ values.
- The analysis runs at `requestedEntry` to preserve PC-relative semantics; navigation and
  result `entry`/body provenance are canonical. Only bus partitions are folded by shared
  `CanonicalAddress`; cache arrays/purge/on-chip addresses are not generic RAM mirrors.
- `CaptureLoadImage` distinguishes fetch from data access. Missing **data** raises
  `DataUnavailError`; a fetch with its complete first instruction may zero-fill decoder
  lookahead, otherwise raises `BadDataError` and records a refusal. Never zero-fill missing
  data as a plausible literal or table entry. Hardware/user-volatile data reads are refused
  even if bytes could be captured; they are not fixed values to fold.
- `SaturnArchitecture::Initialise` enables read-only propagation, held/nonduplicated loads,
  instruction limits and 2-byte instruction alignment. Literal pools are selectively
  read-only, never all captured RAM; shared volatile literals remain reads. Hardware symbols
  themselves need volatility as well as property ranges.
- Prototype recovery is deliberately stricter than rendering text. `WithholdReason` and
  flow/removal checks can withhold a signature while displaying useful output. Staleness,
  incomplete flow, unsupported translation and hardware cautions are separate conditions;
  do not collapse them into a generic successful result.
- Cancellation is cooperative. Default runner budget is 10 seconds; failure to acknowledge
  cancellation within 5 seconds disables the session. No hard thread kill guarantees exist;
  shutdown waits. Request-specific over-budget memoization includes all engine inputs.
  Allocation/unexpected failures during engine work conservatively disable later engine
  use for the process; host-only failures do not poison the translator. See `Decompile`,
  `StartLibrary`, and runner failure paths.
- [`CMakeLists.txt`](../../../CMakeLists.txt) defaults `SE_ENABLE_DECOMPILER` on for desktop,
  forces it off for Emscripten. Ghidra is pinned and modified through recorded patches,
  not edits solely to vendored output. See
  [`VENDOR.md`](../../../FrontEnd/third_party/ghidra-decompiler/VENDOR.md) for bump gates,
  licenses, whole-object linking, `vendor.py`, `se-regen-sla`, and artifact freshness.

## Testing and verification

From `SaturnExplorer/`, configure/build normally, then select the relevant registered CTest
programs. Tests listed here exist and their purposes were inspected; this documentation task
does not claim to have executed them.

```sh
cmake -S . -B build -DSE_ENABLE_DECOMPILER=ON
cmake --build build -j8
ctest --test-dir build --output-on-failure -R 'Decompiler|MemorySnapshot|Sh2FunctionFinder|Sh2Semantics|Sh2Decode|Sh2Generated|Sh2PrintedC|SlaFreshness|HardwareWidth'
```

| Change area | Existing tests in `FrontEnd/tests/` |
|---|---|
| Captured inspection policy and printer metadata | [`CapturedValueInspectionTests.cpp`](../../../FrontEnd/tests/CapturedValueInspectionTests.cpp), [`CapturedInspectionMetadataTests.cpp`](../../../FrontEnd/tests/CapturedInspectionMetadataTests.cpp) |
| Snapshot coverage, alias folding, missing data, BIOS | [`MemorySnapshotTests.cpp`](../../../FrontEnd/tests/MemorySnapshotTests.cpp) |
| Entry heuristics and explicit correction | [`Sh2FunctionFinderTests.cpp`](../../../FrontEnd/tests/Sh2FunctionFinderTests.cpp) |
| Engine/wrapper, calls, frame effects, hardware reads, warning/prototype policy | [`Sh2DecompilerTests.cpp`](../../../FrontEnd/tests/Sh2DecompilerTests.cpp), [`Sh2DecompilerFixtureTests.cpp`](../../../FrontEnd/tests/Sh2DecompilerFixtureTests.cpp), [`Sh2DecompilerEngineTests.cpp`](../../../FrontEnd/tests/Sh2DecompilerEngineTests.cpp) |
| SLEIGH semantics and decoding | [`Sh2SemanticsTests.cpp`](../../../FrontEnd/tests/Sh2SemanticsTests.cpp), [`Sh2DecodeTests.cpp`](../../../FrontEnd/tests/Sh2DecodeTests.cpp), [`SlaFreshnessTests.cpp`](../../../FrontEnd/tests/SlaFreshnessTests.cpp) |
| Generated programs, memory ledgers (the frame's in [`FrameLedger.h`](../../../FrontEnd/tests/FrameLedger.h), shared with the decompiler suite), callable signatures, the pseudocode-to-assembly mapping (every violation of [`MappingOracle.h`](../../../FrontEnd/tests/MappingOracle.h) is a finding, `map-*`) and compiled printed C differential | [`Sh2GeneratedTests.cpp`](../../../FrontEnd/tests/Sh2GeneratedTests.cpp); CTest `SaturnExplorerSh2PrintedCTests` reuses this executable under its printed-C configuration |
| Coalescing, epoch, cancellation, budget and worker teardown | [`DecompilerRunnerTests.cpp`](../../../FrontEnd/tests/DecompilerRunnerTests.cpp), [`DecompilerFaultTests.cpp`](../../../FrontEnd/tests/DecompilerFaultTests.cpp) |
| Headless ImGui interactions, stale install rejection, mirrors, cache, notes, halt hints | [`DecompilerPanelTests.cpp`](../../../FrontEnd/tests/DecompilerPanelTests.cpp): `TestInstallRule`, `TestCompletedAnswerJudgedAgainstCurrentView`, `TestPrototypesPerMirror`, `TestRegisterHintAtHalt`, `TestScrubCache` |
| Stateful refresh/trust sequences | [`DecompilerModelTests.cpp`](../../../FrontEnd/tests/DecompilerModelTests.cpp), [`DecompilerTrustTests.cpp`](../../../FrontEnd/tests/DecompilerTrustTests.cpp), [`DecompilerTrustModel.h`](../../../FrontEnd/tests/DecompilerTrustModel.h). The trust test reads every indicator back from the drawn glyphs (wrapped sentences put together, no allowance for cut text), at 1250 px and at the default dock widths (537, 672, 806 px), checks the status row and the Copy marks, classifies every engine note (amber, or grey only and listed in the guide), runs Decompile as a target in the generated sequences, and sweeps a real allocation failure through one request in forked children (`SE_TRUST_OOM_STRIDE`, `SE_TRUST_WIDTH`). Tools: [`Docs/Decompiler/tools/a8_fix`](../../Decompiler/tools/a8_fix) |
| Paused repeated captures and real live identity changes | [`LiveDecompilerCaptureTests.cpp`](../../../FrontEnd/tests/LiveDecompilerCaptureTests.cpp) |
| Hardware register width/audit consistency | [`HardwareWidthTableTests.cpp`](../../../FrontEnd/tests/HardwareWidthTableTests.cpp) |

Generated/printed-C, allocation-failure and live tests have platform/tool/fixture gates in
CMake and their headers; a skipped test is not evidence that its behavior passed. Follow
`VENDOR.md`'s narrower required bump suite rather than assuming one regex covers every gate.

## Confirmed limitations and investigation coverage

- Default captures contain low/high work RAM and optional user BIOS, not arbitrary cartridge,
  sound or video memory as executable regions. Absent regions stop flow or remain unknown.
- Function starts can be heuristic. `EntryGuess::doubt`, panel `Uncertainties`, body ranges,
  cut/bad-data/indirect-flow warnings preserve that uncertainty; no complete static function
  database or universal control-flow recovery is implied.
- Some stack-walk cases print a raw `register0x...` stack name. The actual code retains useful
  text and withholds an otherwise-lendable signature; it does not universally mark the
  whole result unsupported (`DecompileInto`, raw-register handling).
- Some introductory comments are historical: `Sh2Decompiler.h` describes Step 2 delay-slot
  PC-relative operations as unsupported, while current patched SuperH semantics and wrapper
  tests cover multiple branch/slot cases. Also an earlier comment near raw-register handling
  says unsupported, while the subsequent executable branch only withholds the signature.
  Consult current branches, fixtures and patch inventory rather than treating these comments
  as the final capability specification.
- This investigation traced the host pipeline, validity/lifetime policy, result schema and
  representative verification contracts. It is **not** a new semantic proof of every
  vendored transformation, generated grammar, hardware annotation or reviewed fix. The
  extensive review ledger/program remains the detailed audit resource.

## Related documentation

- [Knowledge-base index](../README.md), [feature map](../FEATURE_MAP.md).
- [User guide](../../DECOMPILER_GUIDE.md), [demo](../../Decompiler/DEMO.md).
- [Design](../../Decompiler/DESIGN.md), [implementation plan](../../Decompiler/PLAN.md).
- [Review program](../../Decompiler/REVIEW_PROGRAM.md), [review ledger](../../Decompiler/REVIEW_LEDGER.md).
- [Hardware-register audit](../../Decompiler/HARDWARE_REGISTER_AUDIT.md).
- [Vendored engine maintenance](../../../FrontEnd/third_party/ghidra-decompiler/VENDOR.md),
  [patch inventory](../../../FrontEnd/third_party/ghidra-decompiler/patches/README.md).
