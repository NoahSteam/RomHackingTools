# SaturnExplorer AI Investigation Interface — Implementation Plan

Date: 2026-10-11. Revision 4. Status: proposed implementation specification, not implemented.

Code baseline inspected: `fbb01b65e2eb36476a8020739b0e32cc7422cd07`. Re-check the cited APIs before implementing; file references are relative to `SaturnExplorer/`.

Revision 4 changes, from the Revision 3 review: evidence is bound to explicit per-snapshot source identities before any action work; Generate publishes the package before Copy is possible; checklist rows have defined states; canonical targets and one encoding; persistence, ownership and visible names become a milestone of their own; data labels are specified; response schema v1 excludes experiments; the runner is gated on named protocol additions; ZIP, scope presets and text-friendly mode are deferred.

## 1. Goal and product decisions

Make SE a useful partner in an AI-assisted reverse-engineering investigation: the user selects a task and evidence, SE exports a portable package, an external agent returns structured hypotheses and proposed actions, and SE validates and applies selected changes. A later stage runs bounded experiments and exports their results for the next round.

SE contains no AI model, AI inference, provider calls, API keys, chat interface or automatic prompt interpretation. It is a deterministic prompt/evidence builder and structured-response importer, usable with any external agent. Importing a response never starts execution. The user explicitly applies debugger changes and explicitly starts an experiment; an experiment may perform its approved steps automatically. Optional Ghidra analysis is ordinary local program analysis, not AI.

Supported on the Windows and macOS desktop builds (Linux SDL2 builds may follow at no extra design cost). The AI Prompts menu is hidden when `IPlatform::HasHostFilesystem()` is false (the web build), because a package is a directory of files. Model, schema, export and validation logic stay independent of ImGui and platform APIs. The embedded decompiler is not required to ship this feature.

**Ordering principle: evidence must be trustworthy before SE accepts actions.** An agent can reason correctly about a package that accidentally describes the wrong frame; every later milestone depends on the source-identity rules in §5.

## 2. Current systems to reuse, and actual gaps

| Need | Exists today | Gap this plan must close |
| --- | --- | --- |
| Immutable A/B memory | `Debug/MemoryCompare` (`MemSnapshot`, `SnapshotOrigin{sessionId, timelineEpoch, frameNo, liveHead}`), `CompareMarkers` | Snapshots hold region bytes only — no registers, call stack, derive serial or edit status (`MemoryCompare.h:91-111`). Session/epoch are per-process counters. Sound RAM is excluded from comparisons (`InCompare`). |
| Per-frame registers | `FrameRecorder::Frame::sh2[2]`, `hasSh2[2]` (`FrameRecorder.h:51-52`) | Lives in the evictable ring, not in a compare snapshot. |
| Debugger-write evidence | Server `pokes_applied/dropped` (v23); client `pokesLost/pokesUnconfirmed` | Server counters reset with each connection (`LiveDriver.cpp:316-320`); no per-snapshot record; scrub edits are refused at capture but live-head pokes are not recorded. |
| SH-2 code text | `Sh2Dump` / `Sh2DumpJob` | Whole 1 MiB LWRAM/HWRAM only; input has no source identity; file name uses `se_frame_number`, which is 0 on a scrubbed frame (`App.cpp:822-823`); no function names. |
| Function names | `FunctionNames` (`Debug/CallStack.h:63-82`) | Raw-address keys (no mirror folding); `saturn_functions.txt` in the working directory; unchecked non-atomic save; shown only in the Call Stack. |
| Comments | `AssemblyPanel::mComments` | `assembly_comments.txt` in the working directory; one line per note, so an embedded newline corrupts the file. |
| Data labels | none | New `AddressSymbols` (§8). |
| Watches | `WatchList` (JSON v1), always big-endian; client-side, never installed in the emulator | Session file in the working directory; no dedup; expressions are `0xADDR [+/- N]` only. |
| Breakpoints | `BreakpointManager` | Not persisted; process-local IDs; `ToggleExecution` removes an existing breakpoint; `AddMemory` dedups and returns the existing (possibly user-owned) ID; memory breakpoints take no condition; `cpu` is ignored by the emulator for both execution and data breakpoints (`Integration/Mednafen/apply.py:339-360`). |
| Tracepoints | `ExecutionActions` (Log only; `writeToLog` the only implemented effect) | Not persisted; no dedup; 256 installed maximum, excess dropped with a log warning; guarded tracepoints forward every execution; memory dereferences in format/condition read the latest snapshot, not hit-time memory (`App.cpp:3101-3104`). |
| Live protocol | `SeLiveProtocol.h` v25; caps word; `stop_seq`; `restore_done/failed`; `step_pending` | No per-install acknowledgement; stop report has no breakpoint identity (watchpoint hits arrive as `EXEC_BP` with the accessor PC); dropped tracepoint events are counted server-side but never sent (`se_export.c:323,1468`); last-client disconnect drops all breakpoints/tracepoints and resumes (`se_export.c:917-942`); client close sends RUN. |
| Halt policy | `App.cpp:1424-1530` with `StepHaltMachine` | Lives in `App`; auto-resumes on logging watchpoints and false guards. |
| Files and exports | `IPlatform::SaveFile/PickDirectory`, `FileWrite` (`WriteFileAtomically`, `MovePath`), `DiscImage`/`IsoFs`, `DataSearch` | No ZIP writer; no reusable JSON parser (the watch parser is private to `WatchList.cpp`); `Sha256Hex` exists but is compiled only with the decompiler (`Decompiler/SpecBundle.h:70`, `CMakeLists.txt:1491-1495`). |
| Ghidra | Step 1 of 5: vendored engine and spec bundle (`Docs/Decompiler/PLAN.md`) | Nothing user-facing; planned `SnapshotKey{sourceId, deriveSerial, frameNumber, regionSetHash}`; planned scope is seeded from `FunctionNames` only. |
| Game identity | ROM path (Launcher), disc volume ID (`IsoFs::volumeId`) | Nothing reads the disc header's product number/version; the live protocol does not identify the game. |

## 3. User workflow and interface

Add **Tools → AI Prompts** with premade prompts and a final **Custom Prompt…** entry (a new `MenuCommand`, with entries in the Win32 and macOS native menu bars and the ImGui toolbar). Context entry points — Memory Compare, a memory selection, a disc file, an assembly instruction, a halt — open the same builder with a relevant preset and bound source. Presets whose collectors have not shipped are not listed.

### 3.1 Prompt Builder window

Selecting an entry opens the **Prompt Builder** window (dockable). Controls, top to bottom:

1. **Bound source** (§5.1): what the evidence is read from, with its identity shown, and a Change Source control.
2. **Preset name and editable multiline prompt text.** Premade entries supply complete initial text; Custom supplies an empty text area. No AI interprets this text or chooses exports. Prompt wording never changes which collectors run.
3. **Export-item checklist**, independent of the text. Each row shows name, source/range/frame, state, estimated size and configuration controls.
4. **Options:** byte budgets, inline limit, "Retain source data for follow-up requests" (§5.6, size shown), and "Include structured-response instructions" (on for presets, user-selectable for Custom).
5. **Generate** — builds and publishes the package (§3.3).
6. **Generated Output** — read-only final prompt, the actual attachment inventory, Copy Prompt, Reveal Folder and Copy Package To….

### 3.2 Checklist states

Every row is in exactly one state:

| State | Meaning | Generate |
| --- | --- | --- |
| Off | Not exported. | Allowed |
| On | Exported from the bound source. | Allowed |
| On – needs source | Wanted, but the bound source cannot supply it now; shows the reason and a Select Source control. | Blocked until resolved or unchecked |
| Unavailable | The bound source can never supply this item (e.g. registers for a snapshot that did not retain them). Cannot be checked; shows the reason. | — |

Rules:

- Opening a preset sets its default items On, or On – needs source when the bound source cannot currently supply them. All other available items are Off and can be checked.
- Custom starts with every content item Off. The bound context is highlighted as available, not silently exported. Package identity metadata is always included. A text-only Custom prompt is valid; empty prompt text cannot generate.
- Switching presets after any edit asks **Keep** (text and every row unchanged) or **Reset** (text and rows replaced by the new preset's defaults). Nothing is silently discarded.
- Editing the text never changes a row. Editing text or rows after Generate marks the shown output out of date; Copy then refers to the published package as it was, and is labelled as such until Generate runs again.
- If an On item becomes unavailable (source evicted, emulator replaced), it moves to On – needs source; it is never silently omitted.
- Long collection runs as a cancellable job with progress.

### 3.3 Generate, publish and copy

Generate:

1. Re-verifies every bound source key (§5.1). On mismatch it refuses and publishes nothing.
2. Collects into a fresh staging directory under the chosen investigations folder (default `ConfigSubDir("investigations")`), streaming large files.
3. Writes `manifest.json` last, then publishes by renaming the staging directory to its final name. Cancellation or any failure publishes nothing and removes the staging directory.
4. Assigns a new `evidence_id`. Earlier packages and responses to them remain valid.

Only after publication succeeds are Copy Prompt and the attachment list enabled.

- **Copy Prompt** copies the exact bytes of `prompt.md`, and the UI shows "Attach these files with this prompt" with the required filenames and the folder location.
- **Copy Package To…** copies the finished directory elsewhere; it never re-collects.
- The prompt always begins with an identity block (`investigation_id`, `evidence_id`, package folder name), whether or not the response instructions are included, so any response can be matched on import.
- The final text is: the user's edited prompt verbatim, then deterministic evidence and attachment sections, then (if enabled) the response contract. Generation never rewrites the user's request.

### 3.4 Investigation panel

A dockable Investigation panel hosts **Responses** (paste or open, validation, preview, apply), **History** (packages, responses, applications, runs, linked by IDs) and later **Experiment**. Parsing and previewing have no side effects. Settings remember budgets and the last folder, never a live connection as an authoritative target. Closing a dialog is never approval to run.

## 4. Presets

| Premade prompt | Default evidence (user can change it) | Expected agent output | Milestone |
| --- | --- | --- | --- |
| Find gameplay value | A/B changed ranges, A/B bytes for the changed regions, debugger-write status, user observations and known values | Ranked address/type candidates; labels, watches, optional write-watchpoint | M1 |
| Identify file structure | File identity (name, size, SHA-256, disc path/LBA when from a disc), header and tail samples, user-selected ranges | Header/record layout hypotheses with supporting ranges | M1 |
| Explain memory write or halt | Halt cause/PC/CPU/stop sequence, registers with validity, call stack with confidence, bounded code and literals, bounded stack memory | Likely writer/callers, names, follow-up instrumentation | M3 (export only) |
| Understand routine | Selected code range with raw words and literals; optional pseudocode when supported | Purpose, inputs/outputs, names, follow-up instrumentation | M3 (export only) |
| Explain graphics mismatch | Screenshot/layers, VDP registers/commands, bounded VRAM/CRAM | Hypotheses and inspection targets | M5 |

Each preset is stored with a stable ID, title, editable default text, default export-item IDs and structured-response preference. `ExportItemRegistry` defines item kinds, the source kinds each can read, collectors and configuration schemas. A preset is a deterministic collector and prompt template, not an AI call.

For gameplay searches, allow explicitly selected U8/S8/U16/S16/U32/S32 interpretations, alignment and region filters. Preserve raw bytes; a diff identifies candidates, not the writer instruction.

For file formats, named fields and bounded image-preview recipes come later (M5). Recipes specify dimensions, pixel encoding, stride, offsets and palettes from an allowlist; SE performs bounds-checked decoding. Code returned by an agent is never run.

### SH-2 code and Ghidra

Code evidence is a **bounded range adapter** over the existing disassembler (not the whole-RAM dump): address, raw word, decoded instruction, selected comments, function names and labels, and the literal-pool words that PC-relative loads in the range reference (as separate binary ranges). Register-derived comments are labelled as observations from the named CPU and source, not constants. Linear disassembly can decode data as instructions; rows are not presented as confirmed code or function boundaries. A whole-region listing is an opt-in attachment with size shown. The existing Data → Dump SH-2 is fixed to record the recorder frame on scrubbed frames.

`ICodeAnalysisProvider` wraps the decompiler's planned `DecompileResult` and its `SnapshotKey`, extended with session and epoch for scrubbed frames, rather than defining a parallel identity. It exports pseudocode, references and diagnostics only when the installed provider supports them, with unsupported delay-slot and truncation warnings beside the result. Raw bytes and assembly remain the ground truth. Callers or references are never fabricated from a linear dump.

## 5. Evidence identity and package contracts

### 5.1 Source identities

Every collected item records the identity of the source it was read from. A package may contain several sources; each item names exactly one.

```
SourceKey {
  kind:            "compare_snapshot" | "paused_live_head" | "recorder_frame" | "file" | "disc_file"
  source_id:       IMemoryBackend::SourceId()          (process-local, diagnostic)
  derive_serial:   se_derive_serial at capture          (process-local, diagnostic)
  session_id, timeline_epoch                            (process-local, diagnostic)
  frame:           decimal string; from FrameRecorder on a scrubbed frame, never se_frame_number's 0
  connection_generation                                 (live only)
  content_sha256:  of the captured bytes, per region/file (persistent)
  debugger_writes: "detected" | "none_detected" | "unknown"   (memory sources only)
}
```

- **A comparison has two sources.** A and B each carry their own `SourceKey`. Per-snapshot attachments name their own snapshot; the diff item references both. No item is labelled with a frame it was not read from.
- **Registers and call stacks come only from what was retained with that source.** M1 extends compare capture to retain both SH-2 register files and their validity alongside `MemSnapshot`. A snapshot captured without them reports those items Unavailable; they are never filled from the live head or a later frame.
- **Debugger-write status is three-state.**
  - *detected*: SE recorded a poke, LST edit or Hex Editor write to that source between the two captures (or since load, for a single source).
  - *none_detected*: SE's write tracking was complete for the whole interval — one connection generation, no `pokesUnconfirmed`/`pokesLost` change, no restore, tracking active throughout.
  - *unknown*: anything else, including a reconnect, counter reset, an older server without poke counters, or history SE did not observe.
  The diff item carries the status; the prompt states it verbatim.
- **Generate is refused** while a bound source is a running live head, or when any bound key no longer matches. Collectors that read live state require a paused live head or a retained snapshot.
- Process-local fields are diagnostic only. After a restart, only content hashes and the package's own files bind evidence.

### 5.2 Game identity

Three layers, so patched-disc rebuilds do not orphan the user's work:

1. **Scope** (decides which names, labels and saved instrumentation apply): the disc header's product number and version when SE knows the disc, otherwise `unscoped`. A rebuilt patched disc keeps its scope.
2. **Provenance** (recorded, not used for matching): SHA-256 of the disc image, cached by path, size and modification time. A different hash under the same scope shows a "different build" notice, not a separate store.
3. **Per-item code fingerprints** (warnings only): function names and code-specific actions store a hash of the bytes at their entry/range. A mismatch marks the item "code changed here (overlay or patch?)". A short prefix is a warning signal, not proof that a routine is unchanged. Data labels are not fingerprinted; their values change by design.

Unknown identity is shown as unknown; a filename alone never establishes identity.

### 5.3 Canonical targets and encoding

- One function, `CanonicalTarget(bus_address) → {region, offset, canonical_bus}`, shared with the decompiler's address-partition rule (`Docs/Decompiler/PLAN.md` A2), keys every store, dedup and display. Cache-through mirrors fold to the cached form, which is also the form the emulator's data watchpoints expect.
- A location is either `bus_address` or `{region, offset}`. If a response gives both, they must resolve to the same canonical target or the action is rejected.
- Region IDs are the lower-case `RegionId` names: `lwram`, `hwram`, `soundram`, `vdp1ram`, `vdp1fb`, `vdp1regs`, `vdp2ram`, `vdp2cram`, `vdp2regs`. Device and register images are never converted to writable SH-2 bus memory; each action declares which region capabilities (`kCapWatch`, `kCapBreakWrite`, `kCapSh2Code`) it needs.
- Addresses, offsets and lengths are `"0x"`-prefixed upper-case hex strings. Frames and counters that may exceed 2^53 are decimal strings. Other integers are JSON numbers.
- IDs are UUID strings. Attachment identity is SHA-256; hashes prove byte identity, not authenticity or correctness.

### 5.4 Package layout

```
<investigations>/<yyyymmdd-hhmmss>-<preset>-<short evidence id>/
  manifest.json          package/schema version, IDs, SourceKeys, item → files, ranges, sizes, SHA-256, coverage
  prompt.md              exactly what Copy Prompt copies
  evidence.json          captured facts, observations and derived interpretations in separate fields
  response.schema.json   machine-readable response schema (only when the contract is included)
  attachments/           selected binary ranges, text listings, images
  source/                retained source data (only when the user chose it, §5.6)
```

The reader rejects absolute paths, traversal, links escaping the package and hash mismatches.

### 5.5 Large datasets

- Default inline limit: 64 KiB of UTF-8 in `prompt.md`, configurable. Above it, a bounded overview stays inline and the selected data goes to attachments.
- Default budgets (configurable): 8 MiB structured JSON, 32 MiB attachments, 2,000 diff ranges, 512 instructions, 64 KiB per surrounding memory selection.
- Binary stays binary. Each memory attachment is one contiguous range of one region of one snapshot; the manifest records region, base bus address (when it has one), offset, length, SourceKey and hash. There is no concatenation.
- `changes.json` lists changed ranges with bounded typed interpretations and references the binary slices instead of repeating bytes as hex.
- File samplers record exact offsets and omitted ranges. A whole large file is opt-in. A selected full artifact is never silently truncated: Generate asks for a smaller selection or a larger budget.
- The prompt names every required attachment by relative filename, evidence ID, purpose, encoding and relevant ranges, and asks the agent to report missing or unreadable files and request a narrower export rather than guess. SE does not claim the agent can read files or that every attached byte entered its context.
- Show actual inline and attachment sizes. Do not infer any provider's limits.
- Deferred: optional ZIP of the published directory, Overview/Focused/Full scope presets, a text-friendly hex mode and range-file splitting.

### 5.6 Retained source data and follow-up requests

"Retain source data for follow-up requests" is an explicit option, with its size shown. When chosen, the complete captured regions of each bound memory source (about 3.5 MB per compare snapshot) are written under `source/` and hashed. They are not listed as attachments the agent must read. Nothing the user did not choose is retained.

A read-only `request_evidence` action (and its specialised form `request_code_evidence`) names a source by `evidence_id` and SourceKey, explicit ranges or a function entry, representation, maximum bytes and reason. Importing one opens a pre-filled Prompt Builder: requested ranges are proposed rows the user can edit. Generate creates a child package linked to the parent response and evidence IDs.

Fulfilment order: the parent package's `source/` (hash-verified), then the parent's attachments, then a snapshot still retained in memory with an identical SourceKey. Otherwise the row is On – needs source, and choosing a new source records a different SourceKey. **Current live RAM is never substituted for a requested historical source.**

## 6. Structured response contract

### 6.1 Accepted input

Paste or open a file. The importer accepts either a single bare JSON document, or text containing exactly one fenced code block labelled `json`, which is extracted. Anything else is rejected with the reason. This is deterministic text handling, not interpretation.

### 6.2 Parsing and validation

- A maintained JSON parser compatible with C++14 and the MSBuild solution, pinned in-repo with its licence. M0 must prove by test that it rejects duplicate keys (many parsers keep the last silently) and exposes integers losslessly.
- Limits on bytes, depth, array counts and string lengths. Unknown fields are rejected, except inside the namespaced `extensions` object, which is retained inertly.
- Validation is generated from `ActionRegistry`; the same registry emits `response.schema.json`, the prompt's action list and reference documentation. A general JSON-Schema engine is not required.
- No partial execution: a response with any invalid selected action applies nothing.

### 6.3 Schema v1

Required: `schema_version` (1), `investigation_id`, `evidence_id`, `response_id`, `hypotheses`, `actions`. Schema v1 has no `experiment` field; one present is rejected as "unsupported in schema v1". Experiments arrive in schema v2 (M4).

Hypotheses: `id`, `claim`, `confidence` (`low|medium|high`, the agent's assessment, not a probability), `evidence_refs` (each resolving to a manifest item/range), `prediction`.

Actions: `id` (unique in the response), `type` (allowlisted), `hypothesis_id`, `target`, typed `parameters`, optional `depends_on`, optional `expected_code_sha256` for code-specific actions.

```json
{
  "schema_version": 1,
  "investigation_id": "2f1c…",
  "evidence_id": "9b07…",
  "response_id": "c4e2…",
  "hypotheses": [{
    "id": "h1", "claim": "General HP, 16-bit", "confidence": "medium",
    "evidence_refs": ["diff/range-12"],
    "prediction": "A damage event decreases this value"
  }],
  "actions": [
    { "id": "a1", "type": "add_address_label", "hypothesis_id": "h1",
      "target": { "bus_address": "0x06012340" },
      "parameters": { "name": "GeneralHP", "size": "0x2", "data_type": "u16" } },
    { "id": "a2", "type": "add_watch", "hypothesis_id": "h1", "depends_on": ["a1"],
      "target": { "bus_address": "0x06012340" },
      "parameters": { "name": "GeneralHP", "data_type": "u16" } }
  ]
}
```

Grammar: names match `[A-Za-z_][A-Za-z0-9_]{0,63}`; comments are one line of at most 256 characters with no control characters; condition and format strings must parse with `ConditionEval`/`FormatString` under bounded complexity.

### 6.4 Matching, duplicates and staleness

- `evidence_id` locates the package in History. If it is unknown (for example mistyped by the agent), the user picks the package or the import is rejected.
- Action identity is `(investigation_id, response_id, action_id)` plus `payload_sha256`, the SHA-256 of the action object re-serialised with sorted keys and no insignificant whitespace. The same identity with a different payload is an error. A re-import shows already-applied actions as such and changes nothing.
- A response is stale for code-specific actions whose `expected_code_sha256` no longer matches the bytes at the target. Changed data bytes do not invalidate a watch or label. Stale actions remain viewable and are never automatically rebound; rebinding creates a new validated application record.

## 7. Actions and application semantics

### 7.1 Action types

Schema v1: `add_address_label`, `rename_function`, `add_comment`, `add_watch`, `add_execution_breakpoint`, `add_memory_watchpoint`, `add_log_tracepoint`, `request_evidence`, `request_code_evidence`.

Later: capability-gated `request_decompilation`, file-layout annotation, image-preview recipe, experiment plans (schema v2), checked memory write.

Never: shell execution, script effects, URL fetching, disc writes, process launching, or any tracepoint effect other than `writeToLog`.

### 7.2 Semantic keys and model calls

| Action | Semantic key | Model call | Constraints |
| --- | --- | --- | --- |
| add_address_label | canonical address | `AddressSymbols::Add` | Data-capable region; not at an existing function name. |
| rename_function | canonical entry | `FunctionNames::Rename` | `kCapSh2Code` region. |
| add_comment | canonical address | Comment store | Single line. |
| add_watch | (canonical address, type) | `WatchList::Add` | `kCapWatch`; types U8/S8/U16/S16/U32/S32/RGB555/Pointer; always big-endian (no byte-order field). |
| add_execution_breakpoint | canonical address | `EnableExecution` — never `ToggleExecution` | No `cpu` (shared by both SH-2s); optional condition. |
| add_memory_watchpoint | (canonical address, kind, size) | `AddMemory` | size ∈ {1, 2, 4}; kind read/write/read-write; no `cpu`, no condition (unsupported); `kCapBreakWrite` for write kinds. |
| add_log_tracepoint | (cpu, canonical address) | `ExecutionActions::Add` after a key check | Log effect only; rejected if it would exceed 256 tracepoints; format memory dereferences are labelled "read at next snapshot, not at the hit". |

### 7.3 Ownership, transactions and undo

- Apply is one local transaction: preflight every selected action and its dependencies, construct changes, then commit through the existing models. Any preflight failure applies nothing.
- If the semantic key already exists, the action is recorded as *already present, not owned*. The existing item is not modified unless the user explicitly chooses Replace for that conflict.
- Every item SE creates from an import carries a **persistent owner record**: a persisted item UUID (new field on `Breakpoint`, `WatchEntry`, `ExecutionAction`, labels, names and comments), the application ID and the values written.
- Undo removes or restores an item only when its UUID still exists and its current values equal the recorded values. An item the user deleted and recreated has a new UUID and is never touched. Undo across restarts uses the same rule.
- Committing to the model and installing in the emulator are separate states, shown as *pending / installed / failed / unconfirmed*. Until the M4 protocol additions exist, emulator installation is *unconfirmed* except for what the caps word proves.
- An "Apply all valid actions on import" preference may be offered for annotation and configuration actions. It never starts execution.
- Annotation actions may be applied to a matching scope without a live connection. Live instrumentation requires an explicitly bound, connected session.

## 8. Naming

Three separate stores, with defined boundaries:

1. **Function names** (`FunctionNames`): code entry points. Re-keyed by canonical entry with a one-time migration of existing raw keys. Displayed in the Call Stack, the Assembly panel (entry lines and call/branch targets), the SH-2 dump and code evidence, and fed to the decompiler scope as already planned.
2. **Data labels** (`Debug/AddressSymbols`, new, no ImGui):
   - Entries: canonical address, name, optional size and type (u8/u16/u32/s8/s16/s32/array of these with count), optional note, provenance (user, import, symbol file), owner record.
   - Unique name per scope; range lookup ("which label covers this address") so offsets inside a sized label display as `GeneralHP+1`.
   - A label may not sit on an address with a function name.
   - Displayed in Assembly operands and literal-pool targets, the Hex Editor's address under cursor or selection, Memory Compare rows, and code evidence.
   - Watch expressions accept a label and an optional offset (`GeneralHP`, `GeneralHP+2`) through `IExpressionResolver`.
   - Edited from the Hex Editor, Watch and Memory Compare context menus and a small Labels panel.
   - Not shown in pseudocode until a decompiler step adds them to its scope; nothing here promises that.
3. **Comments**: free text per canonical address, as today.

Decompiler-local variable rename/type actions remain deferred until the engine supplies stable storage identities tied to function, snapshot and analysis version. Rendered names such as `iVar1` or `local_4` are never used as identities.

## 9. Bounded experiment runner

### 9.1 Protocol prerequisites (additive, version-gated)

1. Control block gains `bkp_applied_gen` and `trc_applied_gen`: the descriptor-set generation the emulate thread actually installed. BKP/TRC carry the generation they send.
2. Stop report gains the installed generation it was evaluated against and a matched-descriptor bitmap, or explicit `ambiguous`/`unknown` when the emulator cannot tell, for example overlapping watchpoints or SCU DMA.
3. Control block gains `events_dropped` (monotonic per emulator run).
4. Results never treat tracepoint memory dereferences as hit-time values unless a later protocol revision captures them at the hit.

Unattended Run is disabled unless the connected server reports these. Setup can still be imported and applied manually without them.

### 9.2 Runner rules

- An ImGui-free state machine: Validating → Preparing → AwaitingInstall → Running → Capturing → CleaningUp → Completed / Failed / Cancelled / Interrupted.
- Halt handling is extracted from `App` into an ImGui-free `HaltPolicy` that the runner consults before the existing condition-guard and access-log auto-resume logic.
- Preconditions (session, epoch, connection generation, target code hashes) are re-checked at the point of execution, not only at import.
- Steps allowed in v2: require a paused live head; install runner-owned instruments; resume until a designated halt, frame limit, wall-time limit or event limit; bounded instruction/frame stepping where supported; capture designated evidence; evaluate simple assertions; clean up and export.
- **Limits** record both "stop requested at frame N / time T" and "paused at frame M / time U". A pause takes effect at the next frame gate, so overshoot is expected and reported. Wall-time limits apply even when the frame counter stalls. Counter resets are distinguished from progress.
- **Halt attribution:** a halt is the runner's only when the stop report names a runner-owned descriptor. A user-owned descriptor, `ambiguous` or `unknown` stops the run with that cause.
- **Disconnect** aborts the run as Interrupted – emulator resumed by server (the server drops all instruments and resumes when its last client leaves). Before any reconnect resync, the runner removes its owned items from the models, so they are never reinstalled into a later session. It never resumes on a new connection.
- **Cancel** requests a pause, waits a bounded time for confirmation, then cleans up. It never resumes to restore a previous running state. Closing or replacing the source cancels the run.
- Cleanup removes only runner-owned instruments, matched by owner UUID. User instruments are preserved.
- Defaults: 10 s wall time, 300 frames, 100 trace events, 8 MiB captured data.
- Results distinguish observation from assertion (a watchpoint firing does not prove HP identity). Record step status, halt reasons, CPU/PC, `stop_seq`, both limit timestamps, values, fingerprints, dropped events, timeouts and cleanup outcome. Export `results.json` plus a new evidence package, linked to the parent response and run IDs.

### 9.3 Controlled comparison trials (M6)

- Checkpoints are runner-owned in-memory savestate images restored through LST and confirmed by `restore_done/failed`. Never `ESV` or `SavestateSlots`, which overwrite the user's slots.
- Trials replay a recorded input sequence (the existing controller-recording format) from the same confirmed checkpoint, or report "input not controlled".
- Edits require exact expected bytes, an allowlisted RAM target, explicit selection and readback.
- Each trial has independent bounds. Matching frame numbers are never claimed as determinism.

## 10. Architecture and persistence

New frontend modules under `FrontEnd/src/AIInvestigation/` (added to both CMake and the MSBuild project):

- `SourceKey`, `CanonicalTarget`, `GameScope`: identity rules (§5).
- `EvidenceCollectors`: diff, memory range, file, code range; later halt and graphics.
- `PromptTemplates`, `ExportItemRegistry`, `ActionRegistry`.
- `PackageWriter` / `PackageReader`: staging, streaming, hashing, publish, bounded reads.
- `ResponseValidator`, `ActionPlanner`, `ActionApplier`, `ApplicationJournal`.
- `ExperimentRunner`, `HaltPolicy`, `ResultsExporter` (M4).
- `InvestigationPanel`, `PromptBuilderWindow`: ImGui presentation only.

Shared foundations: move `Sha256Hex` into a utility built on every desktop build; add the pinned JSON parser.

Persistence (M2):

- Function names, comments, watches, labels, breakpoints and tracepoints move to per-scope files under the config directory (`ConfigSubDir`). They are written with `WriteFileAtomically`, versioned, with failures reported to the user.
- Existing working-directory files are migrated once into the `unscoped` namespace and left in place.
- Breakpoints and tracepoints become persisted. They are re-validated, and installed only on an explicitly bound live session.
- Responses and application journals are persisted the same way. Interrupted runs become interrupted records and never auto-resume.
- Complete files and raw memory are written only to user-chosen packages.

## 11. Milestones and acceptance gates

### M0 — Contracts and foundations

- Freeze `SourceKey`, `CanonicalTarget`, encoding rules, package layout, schema v1 and the action registry.
- Choose the JSON parser and prove duplicate-key rejection and lossless integers; move `Sha256Hex`.
- Write the protocol-gap specification for §9.1.

Done when a synthetic response validates and previews with no side effects, every v1 action maps to an existing model call and capability, and `CanonicalTarget` agrees with the decompiler's A2 rule in tests.

### M1 — Prompt builder and package export

- Tools → AI Prompts; **Find gameplay value**, **Identify file structure** and **Custom**.
- Prompt Builder with the §3.2 states; bound sources (compare pair, paused live head, recorder frame, file/disc file).
- Compare capture retains register files; debugger-write status.
- Collectors: diff/changed ranges, memory ranges, file samples, watch/label/comment definitions.
- Publish-then-copy; the retain-source option; budgets and inline limit.

Done when:
- the user can open a preset, edit its text, uncheck a default, check another item, generate, copy, and hand the folder to an external agent;
- Custom supports the same flow;
- every manifest item's SourceKey equals the source it was read from;
- unselected data never appears;
- all hashes verify.

### M2 — Persistence, canonical names and data labels

- Config-directory, per-scope, atomic persistence with migration.
- Persisted UUIDs on all items; persisted breakpoints and tracepoints.
- Canonical keys for names and comments.
- `AddressSymbols` with its edit UI; names and labels displayed in Assembly, Hex Editor, Memory Compare, the dump and code evidence; label-based watch expressions.

Done when everything survives a restart from a different working directory, injected write failures are reported with the old file intact, and a name applied at a mirror address appears at the canonical one.

### M3 — Structured import, application and follow-ups

- Response import (§6), preview, ownership-aware application and undo (§7).
- History.
- `request_evidence` / `request_code_evidence` fulfilled from retained sources.
- The halt and routine presets as export-only.
- The bounded SH-2 code adapter.

Done when imported candidates appear in their panels, a repeat import changes nothing, an undo never removes a user's pre-existing or recreated item, stale code actions cannot apply, and a follow-up for an unretained historical source reports needs-source rather than reading live RAM.

### M4 — Protocol additions, then bounded experiments

- §9.1 protocol work in `SeLiveProtocol.h`, `se_export.c`, `apply.py` and the live driver.
- `HaltPolicy` extraction; the runner; schema v2 experiments; results packages.

Done when, on a real patched Mednafen on Windows and macOS (recorded in `Docs/FunctionalityVerification/`): a write-watchpoint investigation runs, captures the writer, cleans up and exports; cancel, timeout and disconnect behave per §9.2; and a reconnect never reinstalls runner-owned instruments.

### M5 — File interpretation, graphics, optional Ghidra

- Layout annotations and allowlisted image-preview recipes.
- Graphics collector and preset.
- Ghidra evidence and `request_decompilation` when Decompiler Steps 2–3 have shipped. This can land earlier without schema changes and never blocks other milestones.

Done when an agent's proposed layout or image recipe previews without modifying the source file, and invalid recipes fail before decoding.

### M6 — Controlled comparison trials

Per §9.3.

Done when baseline and edited trials start from the same confirmed checkpoint with controlled or explicitly uncontrolled input, report actual changes and restore outcomes, and never claim determinism from matching frame numbers.

Later, separately scoped: decompiler-local names, bounded batch experiments, ZIP and alternate export modes. Embedded AI, provider APIs and autonomous agent connections are out of scope.

## 12. Validation strategy

- **Identity:**
  - live source changing between collectors (Generate refuses, nothing published);
  - evicted A/B frames;
  - scrubbed frame with staged edits;
  - registers missing from a snapshot (Unavailable, never back-filled);
  - debugger-write status across reconnect (`unknown`), with a recorded poke (`detected`), and with complete tracking (`none_detected`);
  - mirror versus canonical addresses.
- **Package:**
  - exact filenames and range metadata;
  - byte equality and hashes;
  - inline-limit behaviour;
  - sampling coverage;
  - budget rejection for full artifacts;
  - cancellation and write/rename failure leaving no published directory;
  - `sha256(clipboard) == sha256(prompt.md)`;
  - identity block present with the response contract off.
- **Response:**
  - malformed JSON, duplicate keys, oversized counters;
  - zero or multiple fenced blocks;
  - unknown evidence IDs and references;
  - bus/region mismatch;
  - name/comment grammar;
  - unsupported CPU, condition or effect;
  - exceeding 256 tracepoints;
  - stale code hashes;
  - duplicate imports and same-ID-different-payload.
- **Application:**
  - pre-existing user watchpoint plus imported duplicate, then undo (user's remains);
  - user deletes and recreates an applied item, then undo (untouched);
  - `ToggleExecution` never reachable from the applier;
  - restart between apply and undo.
- **Runner** (fake transport, then real server in-process on POSIX):
  - generations acknowledged late, out of order, missing or from a replaced connection;
  - ambiguous stop reports;
  - `events_dropped`;
  - cancel in every state;
  - disconnect followed by reconnect resync containing no runner-owned descriptors.
- **Headless panel tests:**
  - preset text editing, Custom, the four checklist states;
  - Keep/Reset on preset switch;
  - out-of-date output;
  - Copy disabled until publication;
  - conflict selection;
  - disabled Run states;
  - JSON preview errors;
  - prompt wording never changing the collector set.
- **Manual records:**
  - CI runs the unit and headless tests on Windows and macOS. The real-server socket tests are POSIX-only, so the Windows named-pipe path is covered only by M4's manual records, as are native dialogs and Unicode paths.
- **End-to-end (manual, real game, not committed):**
  - export battle A/B → import HP candidates → apply label, watch and write watchpoint → explicitly run → capture writer → export results → import a function name and tracepoint → bounded logging run;
  - separately: export an unknown file → import a layout → preview with the original unchanged.

## 13. Plain-language summary

You pick a premade prompt from SE's menu, or Custom. A window shows exactly what the evidence is taken from, editable prompt text, and a checklist of data to include. Presets tick useful items; you can untick them or add others. Generate writes a finished folder; Copy Prompt then copies the text, and SE tells you which files in that folder to attach. SE contains no AI and sends nothing anywhere.

The AI replies with an explanation and a JSON list of suggestions. SE checks it and shows each suggestion before applying it: name this address, add this watch, break on this write, log these registers. Applied items are yours to keep or undo, and undo never touches anything you made yourself.

The AI can ask for a smaller follow-up dataset; SE fills it from the data you chose to retain, never from whatever the game is doing now.

Later, once the emulator can confirm what it installed and which breakpoint fired, SE can run a small approved experiment with hard limits and a Cancel button, and export the results for the next round.

Build order: trustworthy evidence export first, then reliable names and labels, then importing suggestions, then experiments.
