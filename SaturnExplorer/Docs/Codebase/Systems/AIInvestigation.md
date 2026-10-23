# AI investigation contracts

Inspected revision: `ff6a3b1fb1c54e4c0344613094ea65292b0023c2` (2026-10-20). Implementation statements below are verified from source unless marked **planned**. Paths are relative to `SaturnExplorer/`.

## Purpose and current boundary

This is the deterministic foundation for exchanging captured debugger evidence and structured proposals with an external agent. **At this revision it is M0 contract/validation code, not an available investigation UI.** It does not capture evidence from `App`, write package directories, apply proposals, run experiments, or contact an AI provider. The module is compiled into desktop shared sources but no application source outside it includes it. [AIInvestigationLintTests.cpp](../../../FrontEnd/tests/AIInvestigationLintTests.cpp) enforces that isolation; [CMakeLists.txt](../../../CMakeLists.txt) lists the sources in `SE_AI_CORE_SOURCES`.

The [plan](../../AIInvestigation/PLAN.md) describes later Prompt Builder, Responses, persistence, application and experiment features. Those descriptions must not be mistaken for shipped behavior. [M0_VERIFICATION.md](../../AIInvestigation/M0_VERIFICATION.md) agrees with the observed boundary. Provider calls and embedded inference are explicitly outside the planned scope, too.

## Responsibilities and source locations

All implementation is in `FrontEnd/src/AIInvestigation/`, namespace `sfe::ai`.

| Responsibility | Source and important symbols |
|---|---|
| Canonical, capability-checked Saturn locations | [CanonicalTarget.h](../../../FrontEnd/src/AIInvestigation/CanonicalTarget.h), [CanonicalTarget.cpp](../../../FrontEnd/src/AIInvestigation/CanonicalTarget.cpp): `CanonicalTarget`, `ResolveTarget`, `ResolveTargetJson`, `CanonicalizeBusAddress`, `RangeFits`, `HasCaps` |
| Wire encoding and identifiers | [Encoding.h](../../../FrontEnd/src/AIInvestigation/Encoding.h), `Encoding.cpp`: `ParseHex32`, `FormatHex`, `IsUuid` |
| Source identity independent of current view | [SourceKey.h](../../../FrontEnd/src/AIInvestigation/SourceKey.h), `SourceKey.cpp`: `SourceKind`, `SourceKey`, `ValidateSourceKey`, `SameContent`, `SourceKeyToJson`, `SourceKeyFromJson` |
| Owned evidence contracts and consistency checks | [EvidenceCapture.h](../../../FrontEnd/src/AIInvestigation/EvidenceCapture.h), `EvidenceCapture.cpp`: `EvidenceCapture`, `CaptureData`, `OwnedCapture`, `ValidateCapture`, `Availability`, `GuardUnchanged` |
| Interpretation of debugger edits between captures | [WriteInterval.h](../../../FrontEnd/src/AIInvestigation/WriteInterval.h), `WriteInterval.cpp`: `WriteCheckpoint`, `LedgerEntry`, `LedgerSegment`, `ClassifyWriteInterval`, `ClassifySinceSourceStart`, `CountOutcomes` |
| Package metadata, provenance and path syntax | [PackageManifest.h](../../../FrontEnd/src/AIInvestigation/PackageManifest.h), `PackageManifest.cpp`: `Manifest`, `ManifestSource`, `ManifestItem`, `ManifestRange`, `ValidateManifest`, `ManifestFromJson`, `ResolveEvidenceRef`, `IsValidPackagePath` |
| Single action vocabulary for schema, prompt and checking | [ActionRegistry.h](../../../FrontEnd/src/AIInvestigation/ActionRegistry.h), `ActionRegistry.cpp`: `ActionSpec`, `ActionRegistry`, `FindAction`, `SemanticKey`, `BuildResponseSchemaText`, `BuildPromptActionList` |
| Deterministic response validation and preview | [ResponseValidator.h](../../../FrontEnd/src/AIInvestigation/ResponseValidator.h), `ResponseValidator.cpp`: `ValidateResponse`, `ValidateResponseJson`, `ValidationResult`, `ValidatedAction`, `BuildPreview`, `IdentityOf`, `CompareIdentity` |
| Strict JSON tree, parsing and canonical serialization | [Json.h](../../../FrontEnd/src/AIInvestigation/Json.h), [JsonReader.cpp](../../../FrontEnd/src/AIInvestigation/JsonReader.cpp), `JsonWriter.cpp`: `JsonValue`, `JsonLimits`, `ParseJson`, `WriteJson`, `ToCanonicalJson` |

## Entry points and execution flow

There is no UI entry point yet. A caller supplying owned evidence and a manifest can use the following pure pipeline, demonstrated by [AIInvestigationEndToEndTests.cpp](../../../FrontEnd/tests/AIInvestigationEndToEndTests.cpp):

1. Construct `CaptureData`, including bytes and hashes; move it into `OwnedCapture`; call `ValidateCapture` before using it. Acquisition of real source state is **planned M1**.
2. Construct a `Manifest` with named sources, items and ranges. `ValidateManifest` checks internal consistency, including references, paths, ranges and attachment metadata. `ManifestToJson`/`ManifestFromJson` round-trip the model. Package filesystem operations are **planned M1**.
3. Pass response text, the manifest and `ValidateOptions` to `ValidateResponse`.
4. `ExtractResponseJson` accepts bare JSON or exactly one JSON-labelled backtick block; `ParseJson` enforces parse limits and exact integer representation.
5. The internal `Checker::Run` checks schema fields, IDs, references and each action against `ActionRegistry`. Conditions/formats use existing `ConditionValidate`/`FormatValidate`. `Checker::DeriveFingerprint` resolves covering code evidence; `Checker::CheckDependencies` produces a stable dependency-first order and rejects cycles. Canonical semantic keys detect duplicate proposals.
6. `BuildPreview` turns `ValidationResult` into display-oriented rows, including issues, target, proposed adapter, dependencies and code fingerprint. It applies nothing. `IdentityOf` and `CompareIdentity` distinguish re-importing the same action from conflicting payloads with the same IDs.

`ValidationResult::ok` means no issues were found. An evidence/investigation ID mismatch stops normal checking with `needsRebind`; callers can explicitly rerun using `ValidateOptions::rebound`. That records rebinding and resolves references against the chosen manifest. There is no automatic target correction or application path.

## Important types, state and lifetime

- `SourceKey` separates process-local counters (`sourceId`, `deriveSerial`, session, timeline epoch, connection generation) from persistent content identity (`kind`, per-region/file SHA-256). `SameContent` compares the latter; it does not treat process counters as restart-stable identity. Memory keys require a frame of at least 1. See `ValidateSourceKey` in [SourceKey.cpp](../../../FrontEnd/src/AIInvestigation/SourceKey.cpp).
- `OwnedCapture` owns a `const CaptureData`; byte blobs use `shared_ptr<const vector<uint8_t>>`. Producers must not retain a mutable alias and mutate shared bytes after publication. This module provides the owned record and validation, not a live acquisition lock. `GuardUnchanged` compares source, derive serial, connection generation and frame before/after a future acquisition.
- `ValidateCapture` hashes captured bytes and compares region sizes to `Traits`. It checks unavailable register/call-stack items have reasons and carry no corresponding valid data; file captures cannot carry emulator state. An unavailable item is not filled from newer live state.
- `Manifest` is a value model. `ManifestFromJson` validates it, whereas direct construction is the caller's responsibility. `ValidateResponse` consumes an existing manifest; it does not read or verify attachment files on disk. Path syntax and metadata consistency do not establish actual filesystem containment, symlink safety or file hash correctness; the package reader is still **planned**.
- `ValidatedAction::spec` points into the process-stable registry. `ResolvedRef` points into the supplied manifest; do not keep it beyond that manifest's lifetime or modify/reallocate its containers underneath it.
- Calls return values and diagnostics, with no worker, mutex or application-owned mutable state in this module. The registry and generated strings use function-static immutable data. Future asynchronous collection/application lifetimes are not implemented here.

## Interactions and existing adapters

Canonical locations reuse [Sh2Address.cpp](../../../FrontEnd/src/Sh2Address.cpp) (`CanonicalAddress`, `ClassifyPartition`) and [Debug/MemoryCompare.h](../../../FrontEnd/src/Debug/MemoryCompare.h) (`RegionId`, `Traits`, region capability bits). This keeps bus mirrors and the region table aligned with the debugger/decompiler rather than creating another memory map. Capture call-frame confidence uses `Debug/CallStack.h::FrameConfidence`; hashing uses [Sha256.h](../../../FrontEnd/src/Sha256.h).

The registry describes future adapters; the fact an underlying model exists does **not** mean a validated response currently mutates it:

| Action types in schema v1 | Existing model or planned adapter |
|---|---|
| `rename_function` | `FunctionNames::Rename` exists in [Debug/CallStack.h](../../../FrontEnd/src/Debug/CallStack.h) |
| `add_watch` | `WatchList::Add` in [Debug/WatchList.h](../../../FrontEnd/src/Debug/WatchList.h) |
| `add_execution_breakpoint` | `BreakpointManager::EnableExecution`, then `SetCondition`, in [Debug/BreakpointManager.h](../../../FrontEnd/src/Debug/BreakpointManager.h) |
| `add_memory_watchpoint` | `BreakpointManager::AddMemory` |
| `add_log_tracepoint` | `ExecutionActions::Add` after `LogAt` duplicate checking, in [Debug/ExecutionActions.h](../../../FrontEnd/src/Debug/ExecutionActions.h) |
| `add_address_label`, `add_comment` | **Planned:** address-symbol and extracted comment models. Assembly comments currently belong to `AssemblyPanel::mComments`. |
| `request_evidence`, `request_code_evidence` | **Planned:** Prompt Builder follow-up using a retained parent source |

`ActionRegistryTests::TestAdapterBehaviour` and compile-time signature assertions verify existing adapter behavior. In particular `EnableExecution` preserves an existing breakpoint, whereas `ToggleExecution` removes it; `AddMemory` may return an existing ID; `ExecutionActions::Add` does not deduplicate. Future application code must account for these semantics and ownership.

See [Debugger.md](Debugger.md), [MemoryAndHistory.md](MemoryAndHistory.md), [StateSources.md](StateSources.md) and [Decompiler.md](Decompiler.md) for the actual application systems.

## Constraints and invariants

- Treat response text as bounded structured data. The parser rejects duplicate keys, floating-point/exponent numbers and out-of-range integers. Only `JsonReader.cpp` includes the vendored nlohmann tokenizer; the rest uses `JsonValue`. See [JSON dependency contract](../../../FrontEnd/third_party/nlohmann-json/VENDOR.md).
- Schema v1 rejects `experiment`, unknown fields and unsupported action parameters. Namespaced `extensions` remain inert data. Schema generation is explanatory; SE validates with its own code, not a JSON Schema runtime.
- Bus addresses resolve only through the shared SH-2 rules. The VDP1 framebuffer is an app-derived device image, addressable by region/offset but refused as a bus target. Explicit region offsets do not wrap. Actions additionally require their registry capabilities and alignment.
- Code-specific actions need a cited code range covering the target. Fingerprints derive from the manifest's evidence range, not from a fresh live read or a trusted agent-provided hash. Actual attachment verification must precede trusting such a manifest in the future reader.
- `ClassifyWriteInterval` orders captures by `captureSerial`, not frame number, since rewind can reduce the displayed frame. Accepted, pending or unconfirmed writes mean `detected`; only a complete consistent interval can mean `none_detected`; gaps, state loads, reconnects and unexplained/reset counters yield `unknown`. Outcomes are reported separately by `CountOutcomes`. The classifier takes supplied checkpoints/ledger; actual ledger recording is **planned M1**.
- Generated action/schema references must stay synchronized with `ActionRegistry`; never edit generated text as a second definition of the contract.

## Extension points and implementation routing

| Requested change | Inspect first; preserve |
|---|---|
| Add a response action or parameter | `ActionRegistry.cpp::Build`, `ResponseValidator.cpp::Checker`, registry/validator tests. Update the single row, semantic key and any cross-parameter rule; regenerate both checked-in references. |
| Add source kind or evidence item | `SourceKey`, `EvidenceCapture`, `Manifest`, their validators and tests. Establish persistent identity and explicit unavailable state. |
| Build Prompt Builder/package export | **New integration work, planned M1:** implement real capture boundaries/collectors/reader/writer around these models; consult plan rather than searching for an existing panel. |
| Apply proposals or persist undo/ownership | **New integration work, planned M2/M3:** existing registry adapters are navigation references, not an application engine. |
| Automate experiments over emulator transport | **Planned M4:** read [PROTOCOL_GAPS.md](../../AIInvestigation/PROTOCOL_GAPS.md) and actual protocol/driver implementation. Proposed generation acknowledgments, stamped events and halt attribution there are not current guarantees. |
| Add pseudocode evidence | Reuse the actual decompiler capture/identity contracts documented in [Decompiler.md](Decompiler.md); the plan's `ICodeAnalysisProvider` is a planned interface, not a current type to call. |

## Testing

Targets and CTest registrations are in [CMakeLists.txt](../../../CMakeLists.txt). Tests are standalone executables under `FrontEnd/tests/`:

| Tests | Behavior covered |
|---|---|
| [CanonicalTargetTests.cpp](../../../FrontEnd/tests/CanonicalTargetTests.cpp), [AIEncodingTests.cpp](../../../FrontEnd/tests/AIEncodingTests.cpp) | Mirrors/partitions, bus vs region agreement, capabilities, framebuffer exclusion, strict hex/UUID/counter encoding |
| [SourceKeyTests.cpp](../../../FrontEnd/tests/SourceKeyTests.cpp) | Source identity and JSON, capture hash/size consistency, availability and guard checks |
| [WriteIntervalTests.cpp](../../../FrontEnd/tests/WriteIntervalTests.cpp) | Detection/unknown conditions, counters, outcomes, source-start classification and A/B symmetry |
| [PackageManifestTests.cpp](../../../FrontEnd/tests/PackageManifestTests.cpp) | Package path grammar, provenance, metadata consistency and JSON/reference resolution |
| [ActionRegistryTests.cpp](../../../FrontEnd/tests/ActionRegistryTests.cpp) | Every row validates, adapter signatures/behavior, schema and prompt reference freshness |
| [ResponseValidatorTests.cpp](../../../FrontEnd/tests/ResponseValidatorTests.cpp) | Extraction, binding, references, code fingerprints, action parameters, duplicates, dependencies, preview, identities and determinism |
| [JsonTests.cpp](../../../FrontEnd/tests/JsonTests.cpp), [Sha256Tests.cpp](../../../FrontEnd/tests/Sha256Tests.cpp) | Strict parsing/exact integers/limits and known-answer hashing |
| [AIInvestigationEndToEndTests.cpp](../../../FrontEnd/tests/AIInvestigationEndToEndTests.cpp) | Synthetic captures → manifest → response → preview, observing unchanged debugger models and capture bytes |
| [AIInvestigationLintTests.cpp](../../../FrontEnd/tests/AIInvestigationLintTests.cpp) | Current M0 source isolation, no UI/network/file-I/O/provider integration, C++14 and build wiring; POSIX hosts only |

From `SaturnExplorer/`, after configuring/building as in [BUILD.md](../../../BUILD.md):

```sh
ctest --test-dir build --output-on-failure -R 'SaturnExplorer(AI|ActionRegistry|CanonicalTarget|ResponseValidator|SourceKey|WriteInterval|PackageManifest|Json|Sha256)'
```

Regenerate registry reference files intentionally with the built `SaturnExplorerActionRegistryTests` executable, passing `Docs/AIInvestigation/Generated --write`; its normal CTest invocation checks freshness. No application/UI/emulator end-to-end AI workflow exists to test at this revision. This investigation read test implementations and registrations; it does not claim a fresh test execution.

## Coverage and related documents

Verified here: all M0 module interfaces and principal validation paths, test registration, representative contract tests, and absence of application call sites. Not investigated as implemented behavior: later milestone UI, acquisition, persistence, proposal application and experiments, because those implementations do not exist at the inspected revision. Detailed proposed wire layouts remain in the plan/protocol documents and require a fresh comparison with transport code when implemented.

- [PLAN.md](../../AIInvestigation/PLAN.md): proposed milestones and user workflows; distinguish its design baseline from this inspected revision.
- [M0_VERIFICATION.md](../../AIInvestigation/M0_VERIFICATION.md): historical execution/verification record, not proof that its test run was repeated here.
- [PROTOCOL_GAPS.md](../../AIInvestigation/PROTOCOL_GAPS.md): proposed experiment-runner transport contracts.
- [Generated response contract](../../AIInvestigation/Generated/response.schema.json), [generated action list](../../AIInvestigation/Generated/prompt-actions.txt).
- [Knowledge base index](../README.md), [feature map](../FEATURE_MAP.md).
