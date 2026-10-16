# Saturn Explorer decompiler ledger verification

Reviewed master: `f0684b9d771d0bbf9f41c689bfaa6fa71ef3b830` (15 October 2026).
Repository: https://github.com/NoahSteam/RomHackingTools/tree/f0684b9d771d0bbf9f41c689bfaa6fa71ef3b830

Six reviewers covered all 58 ledger entries, processing related entries in pairs. The ledger has 37 fixed rows, 16 open rows, and 5 accepted rows. Review only: no production changes, commits, branches, or pushes.

## Findings

### 1. P2 — Known callee clobbers can produce a silently wrong constant return

Adjacent to A2-10 and A3-2. `LendEffects` overrides the ABI model only to preserve selected caller-saved registers. It never overrides the ABI's unconditional preservation of r8–r14, GBR or VBR when the callee's own analysis shows those registers change.

Sources:
- [Sh2Decompiler.cpp:182–208](https://github.com/NoahSteam/RomHackingTools/blob/f0684b9d771d0bbf9f41c689bfaa6fa71ef3b830/SaturnExplorer/FrontEnd/src/Decompiler/Sh2Decompiler.cpp#L182-L208)
- [superh.cspec:62–73](https://github.com/NoahSteam/RomHackingTools/blob/f0684b9d771d0bbf9f41c689bfaa6fa71ef3b830/SaturnExplorer/FrontEnd/third_party/ghidra-decompiler/processors/SuperH/superh.cspec#L62-L73)

Reproducer (the generated-test executable takes a private scratch directory):

```sh
SE_GENERATED_WORDS="4F22 E005 401E B004 0009 0012 4F26 000B 0009 E107 411E 000B 0009" \
SE_GENERATED_FUNCS="0/0/12,12/12/1a" \
SE_GENERATED_RETURNS="1,0" \
SaturnExplorerSh2GeneratedTests <scratch>
```

Caller saves PR, sets GBR=5, calls a known leaf that sets GBR=7, reads GBR into r0, restores PR, and returns. Both signatures are known; there are no warnings. Current output:

```c
func_0x06009012();
return 5;
```

Actual return is 7. Representing an unknown post-call value and withholding the unsafe signature would also be an acceptable conservative result.

VBR variant: replace `401E→402E`, `0012→0022`, and `411E→412E`. Same wrong result.

An r8 variant saves/restores r8 in the caller but calls a helper clobbering r8:
```text
WORDS="2F86 4F22 E805 B005 0009 6083 4F26 68F6 000B 0009 000B E807"
FUNCS="0/0/14,14/14/18"
RETURNS="1,0"
```

These helpers use nonstandard calling effects. The finding is specifically that **known analyzed effects contradict the model**; it does not establish that all ABI assumptions for unknown callees must be removed. The design does not exclude hand-written code.

Oracle: GBR and VBR cases each pass with **0 findings**. The oracle checks whether a return exists, not its value; its preservation comparison skips GBR/VBR/MACH/MACL (`Sh2GeneratedTests.cpp:2944–2969`). The r8 case reports only the generator grammar's callee-saved contract violation. Coordinator independently replayed GBR and confirmed the output and clean oracle result.

### 2. P2 — Allocation failure during first library startup leaves inconsistent global state

Adjacent to A6. [SaturnArchitecture.cpp:98–117](https://github.com/NoahSteam/RomHackingTools/blob/f0684b9d771d0bbf9f41c689bfaa6fa71ef3b830/SaturnExplorer/FrontEnd/src/Decompiler/SaturnArchitecture.cpp#L98-L117) sets `started=true` before spec materialization and `ok=true` before `startDecompilerLibrary` completes. Global registration can throw after those flags change. `Sh2Decompiler` is still in Idle, so its exception boundary does not mark the process-global engine damaged.

A fresh-process harness reused the unchanged FaultTests allocator, mailbox fixture and production objects, but injected before `Warm()`. With an already materialized config, allocation #100 (56 bytes) fails in:
```text
AttributeId::initialize → startDecompilerLibrary → SaturnArchitecture::StartLibrary
first: Failed("out of memory"), phase entries=0
second: Failed("No print languages registered")
```

The second request reenters partially initialized global state. Earlier faults can instead leave permanent `Unavailable` with an empty explanation. No crash was observed. Coordinator independently reproduced allocation #100.

Expected: distinguish safe pre-engine failures from interrupted nontransactional global initialization; refuse subsequent service with a useful restart reason where necessary. Apply consistently to Decompile and Disassemble.

Oracle: the existing fault matrix warms the global engine before injected cells (`DecompilerFaultTests.cpp:493–547`), so it cannot detect this class.

### 3. P2 — Rosetta job runs two executables it does not build

[verify workflow:150–165](https://github.com/NoahSteam/RomHackingTools/blob/f0684b9d771d0bbf9f41c689bfaa6fa71ef3b830/.github/workflows/saturn-explorer-verify.yml#L150-L165) omits `SaturnExplorerSh2DecodeTests` and `SaturnExplorerSh2GeneratedTests` from its explicit build list but selects both with CTest. They are independent targets, not dependencies of the listed targets.

A clean Linux configuration's dependency graph confirms both are excluded from that build closure. Selecting the two unbuilt tests returns CTest exit 8, missing executable / Not Run. The same clean-build target mismatch applies to the Rosetta workflow; native macOS execution was not available.

### 4. P2 — Sanitizer CI omits the allocation-failure regression matrix

The sanitizer workflow build list and `run-decompiler-sanitizers.sh` omit `SaturnExplorerDecompilerFaultTests`. Ordinary unsanitized CTest is insufficient: the fault suite's `LeakCheck()` returns zero when built without ASan (`DecompilerFaultTests.cpp:996–1006`).

Sources:
- [sanitizer build list](https://github.com/NoahSteam/RomHackingTools/blob/f0684b9d771d0bbf9f41c689bfaa6fa71ef3b830/.github/workflows/saturn-explorer-decompiler.yml#L99-L106)
- [sanitizer runner](https://github.com/NoahSteam/RomHackingTools/blob/f0684b9d771d0bbf9f41c689bfaa6fa71ef3b830/.github/scripts/run-decompiler-sanitizers.sh#L62-L77)

Synthetic harness verification: provide successful stubs for the currently enumerated binaries and a fault-suite stub that writes an invocation marker and exits 42. The actual runner never invokes the fault suite, and its checker reports success. This proves omission, not a newly triggered engine leak.

Expected: build and run the instrumented deterministic fault regression subset as a gate, capturing its status/log and preserving the explicitly accepted leak policy.

### 5. P3 — A2-8 acceptance claims a user-guide disclosure that is missing

The ledger's accepted A2-8 rationale says “the guide says so” about excluded hardware behavior. `DECOMPILER_GUIDE.md:74–79` does not disclose CPU address/illegal-instruction/illegal-slot exceptions, interrupts, Sleep/Standby effects, or TAS.B bus locking/atomicity.

Add the promised concise Limits explanation and keep the acceptance rationale consistent. This finding requests documentation, not an expanded CPU model.

### 6. P3 — Generator admits excluded address-error code and reports a false production failure

The fresh 20,000-seed mixed-grammar run (200001–220000) completed with one failing seed, **219488**. Independent triage reduced it to:

```sh
SE_GENERATED_WORDS="2448 8F01 2415 2432 000B 0009" \
SE_GENERATED_FUNCS="0/0/c" SE_GENERATED_RETURNS="0" \
SaturnExplorerSh2GeneratedTests <scratch>
```

This is `tst r4,r4; bf/s rts; mov.w r1,@-r4; mov.l r3,@r4; rts; nop`. When r4 is zero, the unconditional delay slot makes it FFFFFFFE, then the long store is misaligned and crosses the end of the address space. Mednafen raises a CPU address error (`sh7095.inc:2718–2725`); that exception behavior is already excluded by A2-8. The original full seed also performs misaligned long reads in normal generated executions.

SE explicitly returns Failed("Trying to construct memory range beyond end of address space: ram") and withholds the signature. This is **not** a new silently wrong pseudocode result. The validation problem is that the generator models these exceptional executions as finished normal runs and requires successful decompilation.

Controls: change predecrement store `2415→2416` (decrement by four), or long store `2432→2431` (word), and the minimal case passes.

Root cause: `Sh2GeneratedTests.cpp:395–403` constrains widths by alignment only for r6/r7; `MakeMem:522–524` updates r4/r5 pointers without tracking their alignment. Preserve the seed and teach the generator to identify the excluded instruction/access precisely or generate aligned normal-execution cases. Do not broadly suppress failed statuses or this engine error string.

## Existing open issues

All nine A5-9–A5-17 rows were independently checked against Mednafen 1.32.1 at `f0ee9d595db68ad5247ba5ac6a8367fdced9c3fc`. One original fixture per row reproduces the recorded output; **all nine also pass the generator with 0 findings**. These are existing backlog, not new discoveries.

- A5-9–A5-15: address-error, lane/width, DSP, keyed BSC, MINIT/SINIT, split access and bus side-effect omissions.
- A5-16: missing register effects/aliases and CRAM byte-write semantics. Consider splitting its CRAM subcase and raising it to P2: current output propagates an incorrect preserved high byte. It goes beyond a missing descriptive note.
- A5-17: cosmetic even-byte SMPC naming; the correct no-op caution is already present.
- For any A5-11 fix, retain the qualification that PPD/PDD program/data loads and address advances happen **only while the DSP is stopped**.

Other open rows remain open: A10-1/2/3 (build-tool sanitizer issues), A2-4 (flagged delay-slot PC limitation), and A12-4/5/7 (Windows launch argument, warnings, halt-test timing). Windows-specific claims were source-reviewed, not rerun on Windows.

## Coverage and validation

| Reviewer scope | Outcome |
|---|---|
| A2-1–A2-10 | No new runtime semantics regression; missing A2-8 disclosure found. |
| A5-1–A5-8, A4-1 | Existing fixes upheld; adjacent differential and map probes passed. |
| A5-9–A5-17 | All existing open cases reproduced. |
| A4-2–A4-6, A3-1–A3-2, A11-1 | Original witnesses upheld; known-callee clobber issue found. |
| A6-1–A6-9 | Default fault pins pass; cold-start omission found. |
| A1-1–A1-3, A10-1–A10-3, A12-1–A12-7 | Fixes consistent in reviewed source/Linux checks; two CI gaps found. |

Completed:
- Main decompiler, engine, runner, panel, model and SLA freshness suites passed. A transient unusable shared test executable required a separate relink of the same objects before the main suite completed; no production change.
- Semantics suite passed.
- Decode runs: 3.9 million cases (seed 77) and 6.5 million (seed 20261015); zero unexpected differences. Expected A2-4 differences remain explicitly accounted for.
- Exhaustive 65,536-word decoder comparison and 5,632 translation-history cells passed; all eight A2-4 shapes flagged.
- Default fault matrix: 25/25 jobs, all 31 pins resolved.
- 108 standalone hardware-map checks; 13 adjacent differential fixtures; historical regression replays and four manually inspected post-poll MAC probes.
- A separate 3,000 grammar-2 seeds from 200001 passed.
- Fresh 20,000-seed mixed-grammar run: completed, one failing seed (219488), triaged as the excluded address-error generator case above; the other 19,999 seeds produced no findings.

Limits: Linux only; no native Windows/macOS/Rosetta execution, private game corpus, new sanitizer build, or complete mutation rerun. The full warm allocation sweep was stopped and is not counted. Passing existing oracles does not establish semantic correctness for the demonstrated blind spots.

## Copyable prompt for Claude

Please address the following review findings in NoahSteam/RomHackingTools. They were verified at master `f0684b9d771d0bbf9f41c689bfaa6fa71ef3b830`. Read `SaturnExplorer/CLAUDE.md`, the decompiler design/contracts, review program, ledger, and prior round records first. Reproduce before changing code; preserve repository review conventions, record the fixes, and mutation-check new regression tests.

1. **Known callee clobbers override ABI preservation (P2).** `Sh2Decompiler.cpp:182–208` only lends preserved effects for the default caller-saved set. It leaves r8–r14/GBR/VBR unaffected even when the callee's analyzed effects say otherwise. Replay:
   ```sh
   SE_GENERATED_WORDS="4F22 E005 401E B004 0009 0012 4F26 000B 0009 E107 411E 000B 0009" \
   SE_GENERATED_FUNCS="0/0/12,12/12/1a" SE_GENERATED_RETURNS="1,0" \
   SaturnExplorerSh2GeneratedTests <scratch>
   ```
   Caller sets GBR=5; leaf changes GBR=7; caller returns GBR. Current output calls the leaf then returns5, both signatures known and no warning; actual result7. Generator passes with0 findings. VBR variant substitutes 401E→402E,0012→0022,411E→412E. Also cover r8 with WORDS="2F86 4F22 E805 B005 0009 6083 4F26 68F6 000B 0009 000B E807", FUNCS="0/0/14,14/14/18", RETURNS="1,0". Correct known per-callee effects or conservatively invalidate/withhold; do not silently retain a contradicted ABI promise. Extend the oracle to catch the class: it currently checks return existence and skips several special registers in preservation comparison.

2. **Cold global initialization failure (P2).** `SaturnArchitecture::StartLibrary:110–113` commits started/ok before materialization/registration finishes. Injecting an allocation failure in AttributeId::initialize before any DecompilePhase entry makes the first request Failed("out of memory") and the next Failed("No print languages registered"). With the existing FaultTests allocator and an already materialized config, fresh-process index100 (56bytes) reproduces this; the index is a reproduction aid, not a robust test contract. Earlier failures can leave Unavailable with an empty reason. Add cold-process fault coverage before Warm(), including spec materialization, global registries/capabilities, spec paths and first translator. Prevent reuse of partially initialized globals and preserve a useful error/restart reason across both Decompile and Disassemble. Do not blindly retry nontransactional registration.

3. **Rosetta CI target mismatch (P2).** In `.github/workflows/saturn-explorer-verify.yml:150–165`, build Sh2DecodeTests and Sh2GeneratedTests before the CTest regex selects them. Verify every selected executable exists in a clean build and keep both tests gating.

4. **Fault suite missing from sanitizer gate (P2).** Add SaturnExplorerDecompilerFaultTests to the ASan/UBSan/LSan build/run/log/status gate. Its ordinary unsanitized LeakCheck is a no-op. Run its deterministic regression subset, honor the existing accepted-leak policy, and prove a failing fault-suite run fails the harness.

5. **A2-8 missing disclosure (P3).** Add the promised user-facing Limits text for address/illegal-instruction/illegal-slot exceptions, interrupts, Sleep/Standby and TAS.B locking/atomicity. Match the ledger's acceptance rationale. No CPU-model expansion is requested.

6. **Generator alignment/domain gap (P3).** The fresh 20,000-seed run from200001 fails only seed219488. Minimal replay: WORDS="2448 8F01 2415 2432 000B 0009", FUNCS="0/0/c", RETURNS="0". On r4=0, bf/s executes its predecrement word-store slot, leaving r4=FFFFFFFE before a long store. Real hardware raises a CPU address error, already excluded by A2-8; SE safely fails and withholds the signature. The oracle currently treats exceptional executions as normal completed runs. Preserve the seed and classify excluded accesses precisely, or generate valid aligned cases; do not blanket-suppress statuses or error text. Controls 2415→2416 or2432→2431 pass. This is an oracle-domain issue, not a newly wrong successful decompilation.

Keep the 16 already-open rows distinct from these new findings. A5-9–A5-17 all still reproduce. Consider splitting/retriaging A5-16 CRAM's wrong-value subcase as P2; when fixing A5-11 retain the DSP-stopped condition for PPD/PDD.

Report each finding's fix and SHA, exact regression and mutation evidence, checks actually run, and any platform/sanitizer/corpus verification still unavailable.
