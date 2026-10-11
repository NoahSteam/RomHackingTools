# SaturnExplorer AI Investigation Interface — Implementation Plan

Date: 2026-10-11. Revision 3: attachment-based large datasets and progressive evidence handoff. Status: proposed implementation specification, not implemented.

Code baseline inspected: `3d494d468bdc6fb0852a6976d1ac6ce3f79860f4` in NoahSteam/RomHackingTools. Re-check current APIs before implementation. Suggested eventual repository location: `SaturnExplorer/Docs/AIInvestigation/PLAN.md`.

## 1. Goal and product decisions

Make SE a useful partner in an AI-assisted reverse-engineering investigation: the user selects a task and evidence, SE exports a portable package, an external agent returns structured hypotheses and proposed actions, and SE validates and applies selected changes. A later stage runs bounded experiments and exports their results for the next round.

SE contains no AI model, AI inference, provider calls, API keys, chat interface or automatic prompt interpretation. It is a deterministic prompt/evidence builder and structured-response importer, usable with any external agent. The user takes generated output to their chosen AI and brings responses back. Importing a response never starts execution. The user explicitly applies debugger changes and explicitly starts an experiment; an experiment may perform its approved steps automatically. Optional Ghidra analysis is ordinary local program analysis, not AI.

Support Windows and macOS. Keep model, schema, export and validation logic independent of ImGui and platform APIs. Do not put AI dependencies into the rendering core or emulator. Do not require the embedded decompiler to ship this feature.

## 2. Current systems to reuse, and actual gaps

| Need | Existing system | Implementation implication |
| --- | --- | --- |
| Immutable A/B memory and provenance | `Debug/MemoryCompare`, `CompareMarkers`, recorder/scrub state | Reuse immutable captures; preserve session, timeline epoch, and region type. Sound RAM is currently excluded from comparisons: report that explicitly. |
| Watches | `Debug/WatchList`, `MemoryBackend`, WatchPanel | Add through the existing model; supported values are big-endian. Do not offer arbitrary byte order through a UI that cannot implement it. |
| Function names | `FunctionNames` in `Debug/CallStack.h` | Reuse naming model, with game/artifact scope and provenance added where needed. |
| SH-2 code dumping | `Sh2Dump.{h,cpp}` and `Sh2DumpInput` | Reuse immutable code bytes, raw words, comments and register context. Add a bounded range adapter rather than exporting whole RAM listings by default. |
| Execution breakpoints and memory watchpoints | `BreakpointManager` and existing sync path | Execution breakpoints are shared between SH-2s. Validate memory-watchpoint scope against actual backend support; do not promise a CPU selector it ignores. |
| Tracepoints | `ExecutionActions`, condition/format evaluators, live synchronization | Expose only implemented effects. Stored future flags such as runScript are not executable capabilities. |
| Files, searches, graphics, export | Disc tools, PRS search, layer export, FileWrite, platform interfaces | Reuse readers and checked writers; capture data before opening blocking dialogs. |
| Ghidra integration in progress | Current tree contains embedded Ghidra SH-2 spec/vendoring infrastructure; engine/panel integration is being developed | Define optional analysis adapters now. Export pseudocode, references and warnings only when actually supported. An external Ghidra plugin, if separately supplied, uses the same interchange contract without implying it is already present. |
| Data-variable naming | No general typed address-symbol store established by this inspection | Add `AddressSymbols`; initially display labels in watches, memory and assembly. Do not imply this automatically renames decompiler locals. |
| Bounded automated tests | No general experiment runner established | New explicit runner over existing debugger commands, not an arbitrary script engine. |

## 3. User workflow and interface

Add Tools → AI Prompts with a list of premade prompts and a final Custom Prompt… entry. Context entry points from Memory Compare, a memory selection, a disc file, an assembly instruction/function, and a halt report open the same builder with the relevant preset and selection. Menu items may be shown as unavailable when their collector has not shipped; do not advertise unusable templates.

Selecting any entry opens a Prompt Builder window. Its primary controls are:

1. Preset name and editable multiline prompt text. Premade entries supply complete initial text; Custom supplies an empty editable text area. The user can change any wording. No AI interprets this text or chooses exports.
2. Export-item checklist, independently selectable from prompt text. Each row shows its name, source/range or frame, availability, estimated size and configuration controls. Suitable items are automatically enabled once when opening a preset. All other available items can be enabled manually, and enabled items can be unchecked.
3. Export options: scope, range/CPU/frames, byte limits, optional attachments, and Include structured-response instructions (on for presets, user-selectable for Custom).
4. Generate Prompt button. This freezes the selected sources and composes the exact edited text, enabled evidence descriptions/data, attachment references and optional response contract into a handoff for the external AI.
5. Generated Output view with final read-only prompt, actual evidence/attachment inventory and Copy Prompt / Save Package controls. Clipboard-friendly JSON can be included inline when small; binary and image evidence are referenced as attachments. Copy explains which files must accompany the prompt. Nothing is sent to an AI by SE.

The final generated text begins with the user's edited prompt, followed by clearly labeled deterministic evidence and response-format sections. Generation never rewrites the user's request. Editing text or export selections afterward marks the previous output out of date and requires regeneration before exporting the new selection.

Available checklist items include A/B diff, before/after region bytes, selected memory ranges, SH-2 assembly/raw words/literals, CPU registers, halt reason, call stack, comments/function names/address labels, watches/breakpoints/tracepoints, bounded access/trace logs, selected disc file/samples/metadata, screenshots, VDP registers/commands/layers, optional Ghidra results, and prior experiment results. Each item is independently selectable when its collector and source support it. Sound RAM may be exported through a separate snapshot collector when implemented, but must not be invented as part of the existing comparison diff.

Custom starts with no content items checked except unavoidable package identity metadata; a context selection is highlighted as available rather than silently exported. Empty prompt text cannot generate. A text-only custom prompt is valid. Optional structured-response instructions can be disabled for an explanation-only handoff.

Checklist defaults are preset metadata, not restrictions. Switching presets after edits offers explicit Keep current edits/selections or Reset to new preset defaults; it never silently destroys changes. Missing inputs stay visible with a reason and a Select source control. If a previously enabled item becomes unavailable, Generate requires the user to resolve or deselect it; it does not silently omit it. Long collection uses a cancellable job with progress.

Use one dockable Investigation panel with four views:

1. Prompt Builder: the exact preset/custom workflow above, optionally docked. Observed event and known values are editable supporting fields inserted into a visible section, not used to infer hidden export choices. Show source identity, missing data, export size and truncation.
2. Responses: paste JSON or open a response file; show validation errors, ranked hypotheses, cited evidence, action checkboxes, existing-label conflicts and stale targets. No side effects while parsing or previewing.
3. Experiment: starting-state requirements, exact steps, maximum duration/frames/hits, data budget, Run/Cancel, progress and stop reason. Distinguish restoration, configuration and memory modification.
4. History: exports, responses, applied changes, runs and results, all connected by IDs. Re-export results for a follow-up round.

Persistent settings remember preferred limits and last folder, not a live connection as an authoritative target. Closing a modal is not approval to run.

## 4. Presets

| Premade prompt | Automatically enabled evidence (user can change it) | Expected external-agent output |
| --- | --- | --- |
| Find gameplay value | Immutable A/B diff, region availability, user event/known values | Ranked address/type candidates; labels, watches, and optional write-watchpoint proposal |
| Identify file structure | Whole-file identity, size, bounded samples with offsets, strings and known extraction provenance | Header/record layout hypotheses, supporting ranges, proposed structured interpretation/preview |
| Explain memory write or halt | Halt cause/timing, CPU registers, nearby code, bounded memory and stack with confidence | Likely writer/callers, proposed names, breakpoint/tracepoint experiment |
| Understand routine | Selected code and literals; optional trustworthy pseudocode | Function purpose, inputs/outputs, supporting locations and follow-up instrumentation |
| Explain graphics mismatch | Selected screenshot/layers, VDP registers/commands and bounded VRAM/CRAM | Supported hypotheses and inspection targets; optional capture experiment |

Ship the first two first. Add the others after their evidence collectors exist. A preset is a deterministic collector and prompt template, not an AI call or an authoritative analysis algorithm.

Store each preset with a stable ID, title, editable default text, default export-item IDs and optional structured-response preference. ExportItemRegistry defines available item kinds, collectors and configuration schemas. Prompt wording never controls collection or execution; only explicit checklist/configuration does. Include Custom in the first release.

For gameplay searches, allow explicitly selected U8/S8/U16/S16/U32/S32 interpretations, alignment and region filters. Preserve raw bytes and do not equate matching arithmetic with semantic identity. A diff identifies candidates, not the writer instruction.

For file formats, support proposed named fields and bounded image-preview recipes later. Recipes specify dimensions, pixel encoding, stride, offsets and palettes from an allowlist. SE performs bounds-checked decoding. Arbitrary Python/C++ returned by an agent is never run by the importer.

### SH-2 dumping and Ghidra integration

Code investigations can begin from a Dump SH-2 selection as well as the assembly panel or halt. Include address, raw instruction word, decoded instruction and selected comments, plus literal-pool bytes required to understand PC-relative loads. Label register-derived comments as observations from the selected CPU/capture, not constants valid for every execution. Linear disassembly can decode data as instructions; do not present every dumped row as confirmed executable code or a recovered function boundary.

Prefer the selected routine/range and bounded neighbors over a whole-memory dump. A full listing remains an opt-in attachment with its coverage and size shown. Add a read-only follow-up request, `request_code_evidence`, that asks SE for specific bounded ranges, literals or available cross-references; satisfying it generates a new evidence package without running the game.

Define an optional `ICodeAnalysisProvider` adapter for the work-in-progress Ghidra integration: capabilities, immutable snapshot key, engine/spec version, function entry/bounds, pseudocode, structured references and diagnostics. Never fabricate callers or references when only a linear dump is available. A read-only `request_decompilation` action schedules analysis only when the installed provider supports it, with existing cancellation and snapshot invalidation rules. Preserve unsupported delay-slot and truncated-memory warnings beside the result. Ghidra support improves the package; raw bytes and SH-2 assembly remain the ground truth.

The response may propose a function entry or name, but boundary correction is a separately previewed analysis change. Local-variable rename/type actions remain deferred until stable identities exist; never locate a variable solely by rendered names such as `local_4`.

## 5. Evidence package and identity contracts

Start with an ordinary exported directory, avoiding archive-extraction complexity:

- `manifest.json`: package/schema version, task, IDs, collector version, capabilities, artifact hashes and sizes.
- `evidence.json`: captured facts, observations and derived interpretations in separate fields.
- `prompt.md`: task-specific instructions and exact response schema contract.
- `response.schema.json`: authoritative machine-readable response schema for this version.
- `attachments/`: relative-path binary ranges, optional files, images and assembly text.

Use UUID strings for investigation, evidence, response and run IDs. Encode bus addresses as canonical `0xXXXXXXXX` strings. Encode potentially large counters/offsets as decimal strings; JSON numbers are only used where exact portable precision is guaranteed. Use SHA-256 for attachment identity. Hashes prove byte identity, not authenticity or correctness.

Every memory location carries region ID, offset, address-space kind and optional bus address. Device images and register images are not silently converted to writable SH-2 bus memory. Export region availability and implemented action capabilities from the loaded source.

Each snapshot records source/artifact identity, session, timeline epoch, frame, derive serial, edit status and present regions. Across application restarts, process-local IDs are diagnostic only: persistent artifact/content fingerprints govern rebinding. If the game identity is unknown, the UI says unknown; filenames alone do not establish identity.

Capture facts from one pinned/immutable state before export. A/B remain the snapshots the comparison displays, even if markers or ring contents change. Do not mix free-running callbacks, current registers and old memory under one frame label. Halt registers must state timing/validity; call-stack entries retain confirmed/probable/heuristic status. Pseudocode includes engine/spec revision and unsupported/truncated warnings.

Budget defaults: 8 MiB structured JSON, 32 MiB attachments, 2,000 diff ranges, 512 assembly instructions and 64 KiB per surrounding memory selection. These are configurable bounded defaults, not model token guarantees. File samplers export explicit offsets and report omitted ranges. Include a sample header/tail plus selected ranges; attaching a whole large file is opt-in. Cancellation publishes no apparently complete package. Write into a fresh staging directory, then publish the finished directory; do not overwrite another investigation by default.

### 5.1 Large datasets and attachment handoff

Keep the generated prompt readable. Small evidence may be inline; larger selected datasets are written to files the user attaches alongside the prompt. Default inline evidence limit is 64 KiB of UTF-8 text, configurable independently of package limits. Above that limit, retain a bounded overview inline and put the exact selected data in attachments. Show the actual inline size and attachment sizes; do not infer any provider's upload or context limits. The prompt identifies every required attachment by exact relative filename, evidence ID, purpose, encoding and relevant ranges.

| Example attachment | Format and purpose |
| --- | --- |
| `memory-before.bin`, `memory-after.bin` | Exact selected bytes; manifest records region, base address, offset, length and A/B identity. Multiple regions use distinct files, not an undocumented concatenation. |
| `changes.json` | Changed ranges and bounded typed interpretations; references exact binary slices rather than duplicating whole regions as hex. |
| `sh2-code.txt` | Addressed SH-2 assembly with instruction words/comments and explicit coverage; optional literal bytes in separate binary ranges. |
| `game-data.bin` | Exact selected original file or range, with original name, size, hash and extraction provenance recorded. |
| `screenshot.png` | Selected visual evidence with capture identity. |
| `pseudocode.txt` | Optional Ghidra result, accompanied by structured warnings, engine revision and code fingerprints. |

Binary attachments remain binary. Avoid base64/hex copies of whole dumps inside JSON. JSON supplies the index and interpretation metadata; text provides readable assembly and summaries. All summaries are deterministic descriptions/counts or user observations, not AI-written conclusions. Default export need not include redundant full binary and full text representations; when the user selects both, show the duplicate cost explicitly.

Offer Overview, Focused Evidence and Full Dataset scope controls as deterministic convenience defaults. Overview includes bounded metadata/samples; Focused includes configured selections, changed ranges or routines; Full includes explicitly selected complete artifacts. Scope controls do not silently override edited checklist selections: preview the proposed changes and apply only on user selection. Full export remains bounded by explicit user-set budgets and available disk space, with progress/cancellation and streaming output for large files. Never silently truncate a selected full artifact; request a smaller scope or increased budget. Sampled/partial exports record exact coverage and exclusions.

Generated Output lists Required Attachments and Optional Supporting Attachments. Copy Prompt copies the complete handoff instructions and shows “Attach these files with this prompt” plus the required filenames. Save Package exports the directory containing individually attachable files. Offer optional ZIP packaging for convenience, preserving the same directory layout and hashes; retain access to the individual files because some AI interfaces cannot extract ZIPs or inspect binary files. ZIP export is not a response ZIP importer; response import continues to use standalone JSON and validated local evidence references. A ZIP is complete only after its writer closes successfully; cancellation/failure publishes no final archive.

Provide user-selected output modes: Attachments with file tools, or Text-friendly evidence. The former prompt tells the external agent to inspect manifest-defined byte ranges programmatically and verify its coverage, rather than assume every attached byte enters context. The latter includes bounded addressed hex/text samples and states omitted coverage. SE does not probe the AI service or claim the agent can read files. If evidence is missing or inaccessible, the prompt asks the agent to report that limitation and request a narrower export instead of inventing an answer.

Large text datasets may be split into stable, bounded range files when useful, but not merely to bypass service limits. A range index records filenames, source offsets/addresses, lengths, hashes and ordering so partial evidence is unmistakable. The generated prompt points first to the overview and the ranges relevant to the task.

### 5.2 Progressive evidence requests

Add an allowlisted read-only `request_evidence` response action with requested item kind, artifact/snapshot reference, explicit ranges or function entry, requested representation, maximum bytes and reason. `request_code_evidence` is a specialized form. Examples include 4 KiB around an HP candidate, a routine and its literal pool, or additional file samples around a suspected record table.

Importing such a request opens a preview in the same Prompt Builder: requested items are proposed selections, the user can edit text/selections, and Generate Prompt creates a child evidence package linked to its parent response/evidence IDs. This does not call an AI or start the emulator. Collection must use the referenced immutable artifact/snapshot; if it is no longer retained, show unavailable and require explicit selection of a new source, whose identity differs. Never substitute current live RAM for a requested historical frame. Read-only Ghidra requests may schedule supported local analysis after selection, retaining cancellation and diagnostic contracts.

Prefer this loop to a default full-RAM/full-code dump: initial bounded evidence → agent hypotheses → selected follow-up evidence → refined hypotheses or debugger experiment. Follow-up prompts include prior hypotheses needed for continuity without copying every previous dataset.

## 6. Structured response contract

Use a mature maintained JSON parser and JSON Schema validation where compatible with the frontend. The existing watch serializer is not a suitable security/validation boundary for this larger protocol. Pin dependency/version and license in the repository. Limit bytes, depth, array counts and string lengths; reject duplicate object keys and unknown executable fields. Unknown optional descriptive extensions may be retained inertly in a namespaced extension object. No tolerant partial execution.

Required response fields: `schema_version`, `investigation_id`, `evidence_id`, `response_id`, `hypotheses`, `actions`, and optional `experiment`. Hypotheses have IDs, claims, qualitative confidence, evidence references and a testable prediction. Confidence is the agent's assessment, not a measured probability.

Each action has a unique ID, allowlisted type, typed parameters, target identity, preconditions, hypothesis reference and optional dependencies. Execution-related actions reference byte fingerprints and expected backend capabilities. Evidence references must resolve to manifest entries/ranges. Agent prose and captured game strings are treated as data; neither can change the command interpreter.

Example shape (illustrative location and IDs):

```json
{
  "schema_version": 1,
  "investigation_id": "example-investigation",
  "evidence_id": "example-evidence",
  "response_id": "example-response",
  "hypotheses": [{
    "id": "h1", "claim": "Candidate general HP", "confidence": "medium",
    "evidence_refs": ["diff/range-12"],
    "prediction": "A damage event decreases this field"
  }],
  "actions": [{
    "id": "a1", "type": "add_watch", "hypothesis_id": "h1",
    "target": {"region": "hwram", "offset": "74560", "bus_address": "0x06012340"},
    "parameters": {"name": "GeneralHP_candidate", "data_type": "u16", "byte_order": "big"}
  }]
}
```

The shipped schema, not this abbreviated example, defines required fields and action unions. Generate reference documentation and prompt action lists from the same registry to prevent drift.

## 7. Actions and application semantics

Phase-one action types: add address label, rename function, add comment, add watch, add execution breakpoint, add read/write watchpoint, add log tracepoint, and read-only request_evidence/request_code_evidence. Later: capability-gated request_decompilation, file-layout annotation, bounded image preview, experiment plan, checked memory write.

Defer decompiler-local renaming until the engine supplies stable storage identities tied to function, snapshot and analysis version. Address symbols do not promise typed struct recovery or executable pseudocode changes.

Validate condition and trace-format strings through the existing parsers, with bounded complexity and captured-memory reads only. Reject unsupported effects, ranges or CPU semantics. Never route an AI action to shell execution, script effects, arbitrary URL fetching, disc writes or process launching.

Apply selected annotations/configuration as one local transaction: preflight every selected action and dependency, construct changes, then commit through existing models. Preserve user items by default; show conflicts and require selection to replace. Track before/after values and AI provenance. Undo restores only values still owned/unchanged by that transaction, rather than destroying later user edits.

Deduplicate by `(investigation_id, response_id, action_id)` and payload hash; same ID with different content is an error. Persist the mapping to actual model IDs. Preview marks already-applied actions. Configuration commit and emulator installation are separate: pending/installed/failed must be visible. Do not claim atomic remote installation if the protocol cannot acknowledge it.

Annotation import can be applied to matching static artifacts; live instrumentation requires an explicitly bound active session and validated targets. Changed HP bytes alone should not invalidate a watch candidate, but changed code bytes invalidate a code-specific plan. Stale responses remain viewable, never automatically rebound. Rebinding creates a new validated application record.

## 8. Bounded experiment runner

Implement an ImGui-free state machine driven by existing asynchronous debugger commands. No loops supplied by agents, arbitrary expressions with side effects, or unrestricted scripts.

Allow initial steps: require paused/live state; install temporary instrumentation; resume until designated halt/watchpoint, frame limit or time limit; bounded instruction/frame stepping when supported; capture designated evidence; evaluate simple assertions; finish and export results. Add restore-from-owned checkpoint and checked memory writes only in the later controlled-edit milestone.

State flow: Validating → Preparing → AwaitingCommand → Running → Capturing → CleaningUp → Completed/Failed/Cancelled. Every command requires acknowledgement or timeout. Re-check session, timeline and target preconditions at the point of execution, not only import.

Defaults: 10 seconds, 300 emulated frames, 100 trace events and 8 MiB collected data; first limit reached stops collection and requests pause. Track limit cause, dropped events and whether pause was confirmed. A lost connection cannot guarantee a pause: report it explicitly and never resume on a new connection. Wall-time deadlines also apply while frame counters stall. Distinguish normal counter progress from reset/wrap.

Temporary instruments carry ownership IDs. Preserve user breakpoints/tracepoints; any unrelated user halt stops the experiment with its cause. Cleanup removes only runner-owned configuration. Cancel requests pause, waits a bounded acknowledgement, then cleans up; it never resumes to restore a previous running state. Closing/replacing the source cancels the bound run.

Existing sync completion may need an additive installation acknowledgement protocol. Gate automated running on confirmed installation; if reliable acknowledgement is unavailable, permit setup export/import but disable unattended Run rather than using an arbitrary delay.

Optional controlled edits require an owned restorable checkpoint, exact expected bytes, explicit user selection, allowlisted RAM target and readback/confirmation. Restoring only the edited bytes does not roll back the game state: restore the checkpoint for comparison trials. No automatic live code patching or disc rebuilding in this plan.

Run results distinguish observation from assertion: a watchpoint firing does not prove HP identity. Record step status, halt reason, CPU/PC, capture timing, values, fingerprints, drops/timeouts and cleanup outcome. Export `results.json` plus a new evidence package; link parent response/run IDs. Tests are debugger experiments, not external unit-test commands.

## 9. Architecture and persistence

Suggested new frontend modules under `FrontEnd/src/AIInvestigation/`:

- InvestigationModel and ArtifactIdentity: immutable evidence, provenance and persisted investigation records.
- EvidenceCollectors: adapters for diff, file, code/halt and graphics; no model API calls.
- PromptTemplates, ExportItemRegistry and ActionRegistry: editable preset defaults, independently selected deterministic collectors, and supported response-action metadata.
- PackageWriter/Reader: checked output, hashes and bounded reads. Reader initially accepts standalone JSON and known local investigation attachments; reject traversal/absolute attachment paths, links escaping the package, and checksum mismatches.
- ResponseValidator and ActionPlanner: schema, semantic checks, conflicts/dependencies and preview.
- ActionApplier: existing-model mutations, IDs and reversible journal.
- ExperimentRunner and ResultsExporter: session-bound state machine and round-trip evidence.
- InvestigationPanel: ImGui presentation only; small App entry-point adapters.

Add `Debug/AddressSymbols` as a reusable typed-label model. Extend naming persistence with artifact scope without discarding existing user names; migrate legacy entries into an explicitly unscoped namespace until assigned. Persist responses and application journals with checked atomic file replacement. Live runs never auto-resume after restart; unfinished runs become interrupted records. Revalidate configuration before reinstalling it.

Keep complete files and raw memory in user-selected exports only; no hidden background upload or provider credential storage. Export supports any AI through normal attachment/copy workflows.

## 10. Milestones and acceptance gates

### M0 — Contracts and integration spike

Freeze schemas, action/capability registry, identity rules, budgets and failure semantics. Verify how current APIs report write/install/restore completion. Select JSON/hash dependencies and complete one tiny export → parse → validate test. Document protocol work required before automation.

Done when a synthetic response can be validated with no side effects, and limitations map to explicit capabilities.

### M1 — Evidence export and two presets

Build AI Prompts menu, premade entries plus Custom, editable Prompt Builder, independent export checklist, immutable collectors, Generate Prompt output and checked directory export. Start with gameplay-value and file-format presets; include the bounded SH-2 dump adapter and its raw-byte/literal attachments for follow-up code evidence. Implement small inline evidence versus attachment output, exact filename instructions, overview/focused/full scope controls and optional ZIP export with individually accessible files. Copy Prompt accompanies Save Package. Export bounded bytes plus explicit coverage, type and provenance.

Done when the user can open a preset, edit its text, uncheck a default item, enable another available item, generate the handoff, and give it to an agent outside SE. Custom must support the same checklist and generation flow. All references/hashes are valid, only selected content is exported, and no mixed-frame evidence is produced.

### M2 — Structured response import and debugger setup

Build hypotheses/action preview, labels/function names/comments/watches/breakpoints/log tracepoints, source validation, deduplication, conflict handling and reversible application. Handle read-only evidence requests through a user-reviewed follow-up Prompt Builder with parent IDs and immutable-source validation. Add history and checked persistence.

Done when imported candidates visibly appear in SE, repeat imports do nothing, stale/unsupported responses cannot execute, and user configuration survives apply/undo/restart.

### M3 — Bounded experiments and feedback loop

Implement installation acknowledgements as required, runner, cancellation, temporary ownership, stop reasons and result packages. Ship memory-write/halt and routine presets. Prove a watchpoint investigation can run, capture evidence, clean up and export a follow-up request.

Done when a real patched-emulator run completes or times out predictably, and disconnect/cancel/user halt cannot continue execution in another session.

### M4 — File interpretation and graphics evidence

Add layout annotations and allowlisted bounded image decoders/preview recipes. Add graphics collector/preset. Add optional Ghidra/decompiler evidence and read-only analysis-request adapters only when engine and warning contracts are implemented. If that work lands earlier, the adapter can ship earlier without changing the schemas or blocking the other milestones.

Done when an agent's proposed field/image layout can be previewed without modifying the source file, and invalid recipes fail before decoding.

### M5 — Controlled comparison trials

Add checkpoint-owned restore and selected checked RAM edits for one-variable experiments, explicit restoration and structured trial comparisons. Keep independent stopping bounds per trial.

Done when baseline and edited trials each start from the same confirmed checkpoint, report actual changes and restore outcomes, and never claim determinism merely because frame numbers match.

Later, separately scoped: more decompiler symbols and bounded batch experiments. Embedded AI, provider APIs and autonomous agent connections are outside this feature's scope.

## 11. Validation strategy

Test contracts with meaningful fixtures: changing live source during capture; evicted A/B frames; missing Sound RAM; pending edits; unavailable regions; malformed JSON/duplicate keys/large counters; invalid conditions; unsupported CPU/effects; out-of-range targets; unknown evidence; stale code; duplicate imports and user conflicts; failed persistence/publication.

Attachment tests verify exact filenames and range metadata, binary byte equality/hashes, inline threshold behavior, explicit sampling coverage, budget rejection for full artifacts, cancellation/write/close failures, ZIP contents and individual-file handoff. A follow-up request for evicted historical bytes must report unavailable rather than capture the live head. Text-friendly exports identify exclusions; attachment-mode prompts identify required files and never claim all attached bytes were read.

Runner tests use a fake command transport with acknowledgements delivered late, out of order, missing or from a replaced session. Verify hard bounds, unexpected halts, overflow/drop reports, cancel while running/capturing/cleaning up, and preservation of user instrumentation. Assertions cover observable behavior, not implementation details.

Headless panel tests cover editable preset text, Custom, independent checklist defaults/overrides, unavailable selected items, switching presets without data loss, stale generated output, Copy attachment instructions, selection/conflicts, disabled Run states and JSON preview errors. Verify changed prompt wording does not change selected collectors and unselected data never appears in output. Native Windows/macOS checks cover dialogs, Unicode paths and export/import. Use the real patched emulator for installation ordering, halt capture, stepping, restore and cancellation; mark fake-transport coverage separately.

End-to-end acceptance: export battle A/B → import HP candidates → apply label/watch/write-watchpoint → explicitly run → capture writer → export results → import function label/tracepoint → run bounded logging. Separately: export unknown file → import header/layout/image recipe → preview with original file unchanged. A real game is a manual acceptance fixture, not required copyrighted test content in git.

## 12. Plain-language summary

You choose a premade prompt from SE's menu, or choose Custom. A window shows editable prompt text and a checklist of data to include. Each preset checks useful items by default, but you can uncheck them or include other available data. Press Generate Prompt to produce the final text and selected attachments to take to an external AI. Large memory/code/file datasets are separate attachments named in the prompt; save them individually or together in an optional ZIP. SE itself contains no AI and sends nothing to a provider.

The AI returns both its explanation and a JSON list of suggestions. SE shows the suggestions before applying them: name this address, add this watch, break on this write, or log these registers. You can accept selected suggestions without manually entering each one.

The AI can also request a smaller follow-up dataset, such as a memory range or function. SE shows that request in the builder so you can generate another package rather than export everything at once.

Next, SE can run a small approved investigation: wait for a watched write, stop, capture the instruction and registers, and send that result back for the next round. Every run has limits and a Cancel button.

Build the prompt builder/export first, import/setup second, automated experiments third. Add file previews and controlled stat changes afterward. SH-2 dumps can be included immediately; Ghidra output becomes another selectable item as its integration becomes available.