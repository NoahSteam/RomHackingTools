# M0 verification record — contracts and foundations

Scope: PLAN.md §11 "M0 — Contracts and foundations", Revision 5 (commit `2f94527`). M1 has not been
started. Everything below is on `master`; commit subjects are listed in §7.

Environment of record: Linux x86-64, GCC 13.3 and clang, CMake 3.28 + Ninja, SDL2 present, so the
SDL2 desktop `SaturnExplorerFrontEnd` links with the new sources. **Not** built here: the MSBuild
solution, Windows/MSVC, macOS/Apple clang, the Emscripten web build (see §6).

## 1. M0 deliverables

| # | Deliverable | Where | Proof |
|---|---|---|---|
| 1 | `Sha256Hex` out of `Decompiler/SpecBundle`, built on every desktop build | `FrontEnd/src/Sha256.{h,cpp}`; `SE_DESKTOP_SHARED_SRC` in `CMakeLists.txt`; `src/**` glob in `FrontEnd.vcxproj` | `SaturnExplorerSha256Tests` (FIPS known answers incl. the million-`a` vector, streaming == one-shot across every block-boundary split). `SaturnExplorerSh2DecompilerEngineTests` `TestSha256KnownAnswers` is **unchanged** and passes through `sfe::decomp::Sha256Hex`. The decompiler's own Step 2 now hashes through it too (upstream commit `7c7bf36`). |
| 2 | JSON dependency | nlohmann/json 3.11.3, `FrontEnd/third_party/nlohmann-json/` (header, `LICENSE.MIT`, `VENDOR.md` with hash, rationale and rejected alternatives); wired in CMake (include dir, licence copied beside the binary) and `FrontEnd.vcxproj` (include dir, `CopyFileToFolders`) | `SaturnExplorerJsonTests`: (a) duplicate keys rejected — adjacent, non-adjacent, nested, in array elements, and via an escaped spelling (`"a"` vs `"a"`), with the JSON path in the message; (b) integers exact from -2^63 to 2^64-1 (2^53+1, 2^63, INT64_MIN, UINT64_MAX), 2^64 and below INT64_MIN refused as *out of range* (not rounded), reals refused; (c) limits at both sides of every boundary — bytes, depth (arrays and objects, plus a 2-million-bracket depth bomb), array length, object members, string and key length in UTF-8 bytes. |
| 3 | `FrontEnd/src/AIInvestigation/` contracts and logic | see §2 | see §2 |
| 4 | Synthetic end-to-end test | `FrontEnd/tests/AIInvestigationEndToEndTests.cpp` | §3 |
| 5 | `PROTOCOL_GAPS.md` | `Docs/AIInvestigation/PROTOCOL_GAPS.md` | Every v25 claim tied to file:line and re-checked against the files (§1 of that document); the wire layout is matched against `SeLiveProtocol.h` v25 and `se_export.c` offsets. Not implemented, by design. |
| 6 | This document | | |

## 2. The contracts (deliverable 3)

| Item | Module | Tests |
|---|---|---|
| `CanonicalTarget` (§5.3) | `CanonicalTarget.{h,cpp}`, `Encoding.{h,cpp}`, shared rule in `Sh2Address.{h,cpp}` | `CanonicalTargetTests`, `AIEncodingTests` |
| `SourceKey`, `EvidenceCapture` (§5.1) | `SourceKey.{h,cpp}`, `EvidenceCapture.{h,cpp}` | `SourceKeyTests` |
| Debugger-write interval classifier (§5.1) | `WriteInterval.{h,cpp}` | `WriteIntervalTests` |
| Encoding helpers (§5.3) | `Encoding.{h,cpp}` | `AIEncodingTests` |
| `ActionRegistry` schema v1 (§6.3, §7.1-7.2) | `ActionRegistry.{h,cpp}`; generated `Docs/AIInvestigation/Generated/{response.schema.json,prompt-actions.txt}` | `ActionRegistryTests` |
| `ResponseValidator` (§6.1-6.4) | `ResponseValidator.{h,cpp}` | `ResponseValidatorTests` |
| Package manifest model and layout (§5.4) | `PackageManifest.{h,cpp}` | `PackageManifestTests` |
| JSON value tree, reader, canonical writer | `Json.h`, `JsonReader.cpp`, `JsonWriter.cpp` | `JsonTests` |

Highlights of what the tests pin:

* **Mirror folding / agreement / rejection** (`CanonicalTargetTests`): every A2 case from
  `Docs/Decompiler/PLAN.md` (`0x26004000`, `0x06104000`, `0x07F04000` -> `0x06004000`; `0x66004000`,
  `0x46004000`, `0xC6004000`, `0xFFFFFE00` rejected by name; `0x00300000` not an LWRAM repeat); every
  region's first and last byte through both bus partitions; `{region, offset}` and a mirror agreeing and
  disagreeing; an offset never wrapping like a mirror; a bus address in the VDP1 frame buffer window
  refused; and a sweep of the whole 4 GiB address space (stride 0x1003) plus every region boundary in
  every partition against `CanonicalAddress`.
* **`"0x"` upper-case hex** (`AIEncodingTests`, `CanonicalTargetTests`): lower-case digits, `0X`,
  missing prefix, more than eight digits, signs, separators, whitespace and numbers-for-strings are all
  refused; addresses are written with eight digits, offsets and lengths with as few as they need;
  decimal strings are exact to 2^64-1 and refuse leading zeros and signs.
* **Interval classifier** (`WriteIntervalTests`), every rule of §5.1: accepted writes in the interval,
  pending in either capture, unconfirmed in either, any outcome (applied / dropped / unconfirmed / lost)
  still `detected`; complete tracking with no writes `none_detected`, also with an empty ledger, a source
  with no server, and a capture against itself; `unknown` for different sessions, epochs and connections
  (a reconnect), a capture with no tracking, a server older than v23, a ledger that is incomplete, does
  not cover the captures, belongs to another session or connection, or has a hole; a state load in the
  interval; capture order contradicting ledger position. **Server counters**: a rise the ledger does not
  explain (applied or dropped) is a gap even with an empty ledger; a fall or a u32 wrap is a reset; a rise
  the ledger does explain is `detected`. **A/B symmetry**: every case runs in both argument orders and must
  agree, plus a sweep of 768 combinations. The sweep found a real asymmetry in the first draft (the
  reported reason depended on which capture held the pending vs unconfirmed write); fixed.
  Single-source packages (`ClassifySinceSourceStart`) and outcome counting are covered.
* **Registry** (`ActionRegistryTests`): the nine types in §7.1's order; every row has an adapter, status,
  milestone, capabilities, semantic key, code-specific flag; the rows equal the plan's §7.2 table (watch
  types, memory-watchpoint sizes 1/2/4, `kCapBreakWrite` for write kinds only, no `cpu` on execution
  breakpoints or data watchpoints, no `condition` on data watchpoints, nothing but `writeToLog` on
  tracepoints); a **sample valid action is generated from each row alone and must validate**, each
  required parameter must be required, a mistyped one refused, an unknown one refused, and every parameter
  accepted; the checked-in generated `response.schema.json` and `prompt-actions.txt` equal the generator's
  output (`ActionRegistryTests <dir> --write` regenerates them); the schema has one `oneOf` branch per row
  with exactly its parameters.
* **Validator** (`ResponseValidatorTests`, 335 check sites, several in loops): extraction (bare, one fenced block among prose,
  CRLF, longer fences, other-labelled blocks ignored; zero, two, unterminated, unlabelled, tilde and inline
  fences rejected); malformed JSON, duplicate keys, oversized counters, depth bomb; `experiment` rejected as
  *unsupported in schema v1* at the top level, on a hypothesis and on an action; unknown fields everywhere
  except namespaced `extensions`; missing fields; evidence-id mismatch **stops** the import, rebinding is
  explicit and recorded, and every reference is re-resolved against the chosen package; unknown evidence
  items and ranges; bus/region mismatch, unmapped, non-memory partition, frame-buffer bus address, odd
  instruction addresses, wrong region capabilities; name and comment grammar (64 / 256 characters counted as
  characters, control and separator characters); unsupported CPU / condition / effect each refused *with
  the reason*; unparseable conditions and formats; 256 tracepoints accepted and 257 refused; code-specific
  actions without covering evidence, with a data range, a range in another region, one byte outside the
  range, and a stale `expected_code_sha256`; dependency order, cycles and unknown dependencies; duplicate
  semantic keys (a mirror and the canonical address are the *same* item) and duplicate label names;
  `payload_sha256` equals the SHA-256 of the canonical re-serialisation and ignores key order and spacing;
  same ids with another payload is a conflict, a re-import is not; the preview.
* **Manifest** (`PackageManifestTests`): layout constants, `<yyyymmdd-hhmmss>-<preset>-<short id>` names,
  23 rejected path shapes (absolute, drive, traversal, backslash, empty segment, trailing dot/space, outside
  `attachments/` and `source/`, ...), 44 consistency faults each detected (provenance by source, a
  configuration item naming no capture and carrying its own stamp, attachment size/hash agreement, code
  ranges), strict JSON round trip, evidence-reference resolution.

## 3. "Done when" criteria

> **A synthetic response validates and previews with no side effects.**

`SaturnExplorerAIInvestigationEndToEndTests`. It builds three owned captures (two compare snapshots A/B
with one HWRAM byte changed, one paused live head carrying code bytes), validates each (`ValidateCapture`:
every key's hashes equal the bytes), diffs A/B, classifies the interval (`none_detected`, given B first),
builds a manifest from the captures (diff range naming **both** A and B, code range hashed from the live
capture's bytes, a configuration item with its own stamp), round-trips it through `manifest.json`, then
validates a four-action response against it and builds the preview. While it runs, a populated
`WatchList`, `BreakpointManager`, `ExecutionActions` and `FunctionNames` are fingerprinted before and
after, and so are the capture bytes, the manifest, and the presence of `saturn_functions.txt` /
`assembly_comments.txt` in the working directory; none changes. A package whose code differs refuses the
same response (`FingerprintMismatch`) and the preview marks that action invalid.

"No side effects" is also pinned structurally: `SaturnExplorerResponseValidatorTests` links **no**
application model (it would not link if the validator referenced one), the validator takes the manifest by
const reference and returns values, `BuildPreview` leaves its input as it was, and validating twice gives
identical results. `SaturnExplorerAIInvestigationLintTests` reads the module's sources and enforces: no AI
vocabulary or client, no network / ImGui / platform / file-I/O includes or calls, no C++17 headers or
features, every `.cpp` in `CMakeLists.txt`, `FrontEnd.vcxproj` compiling `src\**\*.cpp` with the JSON
include path and licence, the JSON header included by exactly one file, and **nothing outside the module
including it** (M0 wires nothing into the application). The lint test was exercised by injecting a
violation of each kind; each was caught.

> **Every v1 action has a specified adapter, required capability and implementation milestone. Adapters
> onto existing models are verified against the code; models that do not exist yet are listed as such,
> with the milestone that adds them.**

| Action | Adapter | Status | Region capabilities | Code-specific | Applied in |
|---|---|---|---|---|---|
| `add_address_label` | `AddressSymbols::Add` | **planned, M2** (new `Debug/AddressSymbols`; no header exists — a `__has_include` guard in `ActionRegistryTests` fails the build the day one appears) | `kCapWatch`, bus-addressable | no | M3 |
| `rename_function` | `FunctionNames::Rename` | exists (keyed by **raw** address today — verified by test; M2 re-keys, adds UUIDs, per-scope file) | `kCapSh2Code`, even | **yes** | M3 |
| `add_comment` | comment store | **planned, M2** — *there is no comment model class*: the map is private to `AssemblyPanel` (`mComments`) and edited inline | none; `kCapSh2Code` region makes it code-specific | **when on `kCapSh2Code`** | M3 |
| `add_watch` | `WatchList::Add` | exists (no dedup, session-local ids — verified; UUIDs/persistence M2) | `kCapWatch` | no | M3 |
| `add_execution_breakpoint` | `BreakpointManager::EnableExecution` (+ `SetCondition`) | exists (leaves an existing breakpoint and its condition alone — verified; `ToggleExecution` removes it and is never an adapter) | `kCapSh2Code`, even | **yes** | M3 |
| `add_memory_watchpoint` | `BreakpointManager::AddMemory` | exists (dedups on (address, kind, size) and returns the existing id — verified; ownership check is M3) | `kCapWatch`; plus `kCapBreakWrite` for write kinds | no | M3 |
| `add_log_tracepoint` | `ExecutionActions::Add` after a `LogAt` key check | exists (no dedup — verified) | `kCapSh2Code`, even | **yes** | M3 |
| `request_evidence` | Prompt Builder follow-up | planned, M3 (Builder is M1) | none (ranges are parameters) | no | M3 |
| `request_code_evidence` | Prompt Builder follow-up | planned, M3 | `kCapSh2Code`, even | no (read-only) | M3 |

"Verified against the code" is two layers, both in `ActionRegistryTests`: `static_assert`s on the exact
member-function types of every existing adapter (a rename or reshape stops the build), and run-time checks
of the behaviours the registry's text depends on.

> **`CanonicalTarget` agrees with the decompiler's A2 rule in tests.**

By construction and by test. The rule is no longer written twice: `ClassifyPartition`, `PartitionName` and
`CanonicalAddress` moved from `Decompiler/MemorySnapshot` into `Sh2Address.{h,cpp}`, which both call;
`decomp::` keeps the names. `MemorySnapshotTests` (A2's own cases) pass unchanged on the shared code.
`CanonicalTargetTests` sweeps the address space against `CanonicalAddress` as described in §2 and, when the
decompiler is built, checks that `decomp::CanonicalAddress` and `decomp::ClassifyPartition` are the shared
functions.

## 4. Validation run

```
cmake -S SaturnExplorer -B build -G Ninja && cmake --build build -j4 && ctest --test-dir build --output-on-failure
100% tests passed, 0 tests failed out of 89
```

89 tests: the 73 that existed at the base commit (all unchanged and passing), 5 added upstream by the
decompiler's Step 2 work, and the 11 new ones (Sha256, Json, AIEncoding, CanonicalTarget, WriteInterval,
SourceKey, ActionRegistry, ResponseValidator, PackageManifest, AIInvestigationEndToEnd, AIInvestigationLint).
After rebasing onto the decompiler's Step 2 work (which landed during M0) the suite was rebuilt and re-run in
full before each push.

Beyond the suite, run during development and not committed: every new AIInvestigation source compiled with
`-Wall -Wextra -Wpedantic -Wshadow -Wconversion` under both GCC and clang with no warnings of ours; the
seven new unit-test programs rebuilt with AddressSanitizer + UndefinedBehaviorSanitizer and run clean; and
deliberate mutations of the validator (no fingerprint comparison, no capability check, no tracepoint cap,
no cycle detection, evidence-id mismatch ignored, no alignment check, no duplicate-key detection) each made
tests fail. The first mutation run showed three as "passing" because the test binary *crashed* on a
validator that wrongly accepted (a test indexed `issues[0]` of an empty list); the tests were made
crash-proof and all mutations are then caught.

## 5. Deviations from PLAN.md, and decisions the plan left open

No contract of the plan was impossible, and nothing the code contradicts needed redesign. Where the plan
was silent or loose, this is what was decided (each is a one-line change if you disagree).

**Deviations (the code does something the plan did not say):**

1. **Decompiler files touched.** The plan says `CanonicalTarget` *shares* the decompiler's A2 rule. At the
   base commit the rule did not exist yet; it landed in Step 2 mid-M0, as functions inside
   `Decompiler/MemorySnapshot`, compiled only with `SE_ENABLE_DECOMPILER`. To share rather than copy,
   it was moved to `Sh2Address.{h,cpp}` (behaviour-preserving; `decomp::` names kept via
   using-declarations; decompiler tests unchanged). Touches `MemorySnapshot.{h,cpp}` and four CMake source
   lists.
2. **PLAN.md edited once**, in its own commit: §9.1 records that the client rejects a control block over 64
   bytes (`LiveDriver.cpp:969-972`) — a verified fact the plan's control-block additions run into.
3. **Generated reference files are checked in** (`Docs/AIInvestigation/Generated/`) and compared by a test,
   so the registry's output is reviewable and drift fails CI. The plan says the registry *emits* these; it
   does not say they are committed.

**Decisions where the plan was silent or ambiguous:**

4. *Bits 28-27.* A2 folds only the HWRAM repeat. An address in the cached/cache-through partition at or past
   `0x08000000` is **unmapped**, not folded.
5. *The VDP1 frame buffer* has no bus address (`AddressSpace::DeviceImage`). Its window is refused as a bus
   address ("use `{region, offset}`"); it resolves only as region + offset, with no `bus_address` in the
   written target. Actions that need a canonical address (label, watch, breakpoints, ...) therefore refuse it.
6. *Hex strictness.* One to eight upper-case digits for 32-bit values; leading zeros accepted; lower-case
   digits, `0X`, bare hex are errors, never normalised (the plan's "never fixed silently" attitude).
7. *Reals are refused everywhere*, including inside `extensions`: no schema field is non-integral, and a
   canonical form of a double is one more thing to get wrong.
8. *`extensions` keys are namespaced* as lower-case dotted names with at least two segments
   (`com.example.tool`); allowed on the response, hypotheses and actions; part of each action's payload hash.
9. *Evidence reference grammar* is `<item id>` or `<item id>/<range id>` (from the plan's `diff/range-12`).
   A code-specific action's evidence is the first code-kind range, in the action's own references and then
   its hypothesis's, that covers the target's instruction (2 bytes; 1 for a comment) in the same region.
   `expected_code_sha256` on a non-code-specific action is refused as meaningless.
10. *Hypothesis fields are all required*: non-empty single-line `claim` and `prediction`; `evidence_refs`
    required but may be empty; `id` unique.
11. *Whole-response duplicate rules* beyond the plan: two actions with one semantic key, or two labels with
    one name, are errors.
12. *`add_address_label`'s "data-capable region"* is read as bus-addressable with `kCapWatch` (every
    bus region has it today); the existing-function-name conflict is an apply-time check (M3).
13. *`request_evidence` / `request_code_evidence` parameters* are not specified by the plan beyond §5.6's
    prose; defined as: optional `evidence_id` (default this package), `source` (the manifest's source id,
    standing for "evidence_id and SourceKey"), `ranges` or a target, `representation`, `max_bytes`, `reason`.
14. *Write-tracking inputs.* The plan names the inputs informally; the classifier's are defined in
    `WriteInterval.h`: capture serial (the order — frame numbers are not an order), session / epoch /
    connection, ledger position, pending and unconfirmed counts, state-load count, server-counter state
    (`NotApplicable` for a savestate, `Unsupported` for a pre-v23 server, `Known`), and a ledger segment with
    a completeness flag. Positive evidence (accepted, pending, unconfirmed) is `detected` even where the
    rest of the tracking has gaps.
15. *The web build* does not get the new sources (nothing in it uses them; the menu is hidden there).

## 6. Open — not verified, not done

* **MSBuild / MSVC / macOS were not run.** `FrontEnd.vcxproj` globs `src\**\*.cpp`, so the new sources are
  in the Windows build with no per-file edit; the JSON include path and licence copy were added and the lint
  test checks them textually. Constructs that deserve a first look from the Windows CI: aggregate
  initialisation of structs with default member initialisers (`ParamSpec`, `ManifestFile`; C++14, accepted
  by v142), `__has_include` in `ActionRegistryTests` (guarded), and compile time of the single translation
  unit that includes `json.hpp`. CI already runs the full `ctest` on both OSes, so every new test runs
  there; `AIInvestigationLintTests` is POSIX-only (it walks the tree) and is skipped on Windows.
* **Not in M0, by plan:** acquiring a capture (`begin_capture` / `end_capture`, register files, frame
  identity), the debugger-write ledger itself, `evidence.json`'s schema (M0 freezes the manifest), package
  writing and reading and the publish step, `GameScope`, persistence and UUID-bearing stores, any menu,
  panel or migration.
* **Protocol work** is specified, not implemented (`PROTOCOL_GAPS.md`).
* **Coverage gap:** the conditional-capability rule (`kCapBreakWrite` only for write kinds) cannot be driven
  through the validator with today's regions, because every bus-addressable region has `kCapBreakWrite`; it
  is pinned in the registry table and exercised by the rule's code path only when a region without it exists.

## 7. Commits (oldest first)

```
SaturnExplorer: move SHA-256 out of the decompiler's SpecBundle into a shared utility
SaturnExplorer: vendor nlohmann/json and add a strict JSON reader for AI investigation
SaturnExplorer: share the A2 partition/fold rule between the decompiler and AI investigation
SaturnExplorer: AI investigation encodings and CanonicalTarget
SaturnExplorer: SourceKey, EvidenceCapture contract and the debugger-write interval classifier
SaturnExplorer: AI investigation ActionRegistry, package manifest model and ResponseValidator
SaturnExplorer: AI investigation end-to-end and static-rule tests
SaturnExplorer: wire specification for the AI experiment runner's protocol additions
SaturnExplorer: AI plan records the live client's 64-byte control block cap
SaturnExplorer: M0 verification record
```

## 8. Questions for M1 (none blocks starting)

1. **Web build:** add `FrontEnd/src/AIInvestigation/*` to the web source list in M1, or guard the call sites?
   Adding them is free (portable code) and avoids `#ifdef`s in `App.cpp`; recommended.
2. **Checkpoint derivation:** M1 must produce `WriteCheckpoint` / `LedgerSegment` exactly as
   `WriteInterval.h` defines them. In particular `pendingWrites` (not exposed by the driver today),
   `ServerCounters` from `hasPokeInfo`/version, and a per-process `captureSerial`. Confirm the ledger lives
   in `Core` or `FrontEnd` (it must see Hex Editor writes at `Context::WriteVram`, pokes and LST edits).
3. **Hypothesis strictness** (decision 10) and **hex strictness** (decision 6): keep strict, then relax
   after trying a few real agents, or relax now?
4. **Labels on images** (decision 5): confirm that no label/watch may target the VDP1 frame buffer.
5. **`evidence.json`** needs its schema frozen in M1 before the collectors write it (captured facts,
   observations, derived interpretations in separate fields — §5.4).
