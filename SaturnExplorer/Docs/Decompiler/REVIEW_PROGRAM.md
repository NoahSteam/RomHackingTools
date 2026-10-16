# SH-2 decompiler review program

How the twelve review areas of the decompiler are worked through by agents in a review/fix
cycle, and what "done" means for each. The record of each round stays in
`Docs/FunctionalityVerification/15_DECOMPILER_STEP5.md`; this document is the standing brief.

## Why a program

Area 5 (hardware-register semantics) took four rounds. Each round a reviewer probed by hand
and found instances; the fix round pinned the instances and added a heuristic for the next
reviewer to probe. The cycle converged only once the area had a machine-checkable oracle (the
access ledger, the all-volatile reference run) and a corpus (two save states). The rule for
every other area follows from that: **build the oracle first, then review the oracle.** A
reviewer's job is to find what the oracle cannot see, not to find bugs one at a time.

## Phase 0: what is missing (two or three agent sessions, in parallel)

| Item | Why | Branch |
|---|---|---|
| **Conservation patch.** The engine records how every flow-time LOAD and STORE ended: printed, folded to a direct read or write of ordinary memory, removed as a dead store, removed with an unreachable block, replaced by a recovered switch's lookup. The oracle checks every flow access is accounted for exactly once. | Replaces the exemptions (`pointerFromOrdinaryMemory`, the removed-block record parsed from warnings) that kept area 5 open. | `program/conservation` |
| **Generator with emulator check.** Random SH-2 functions from a small grammar (loads, stores, branches both ways, loops, literal pools, jump tables, calls, mailboxes, register accesses, delay slots), executed on the vendored p-code emulator for the access trace, compared with the ledger. Seeded, shrinkable; a short run in `ctest`, a long run nightly. | Finds new classes without a human reviewer. Is area 11's instrument. | `program/generator` |
| **CI additions.** A Linux job with ASan, UBSan (vptr) and LeakSanitizer over the decompiler suites; a job that rebuilds the vendored tree from the pinned upstream plus `patches/` and diffs it; a nightly job for the generator. Windows, macOS and Rosetta already run in `saturn-explorer-verify.yml`. | Areas 10 and 12 stop being review topics. | `program/ci` |

The game corpus (work RAM of two save states) is not in the repository and stays a local run,
recorded in the round's section.

### Continuous integration

`.github/workflows/saturn-explorer-decompiler.yml` adds three Linux jobs. The Windows, macOS and
Rosetta jobs stay in `saturn-explorer-verify.yml`.

| Job | Runs | Proves |
|---|---|---|
| `sanitizers` | Every push and pull request touching `SaturnExplorer/`. gcc, Debug, `-fsanitize=address,undefined`; builds and runs the Sh2Decompiler, Sh2DecompilerEngine, Sh2DecompilerFixture, DecompilerPanel, DecompilerRunner and Sh2Semantics tests with leak detection on, and the SlaFreshness test informationally. | No AddressSanitizer or LeakSanitizer report and no UBSan error (vptr included) in the decompiler suites, game code included. The only UBSan lines tolerated are the two upstream SLEIGH shifts, `address.hh:605` and `slghpatexpress.cc:979`, matched by file and line in `.github/scripts/check-sanitizer-logs.sh` (a suppressions file would hide a new shift anywhere in the same file). A Ghidra bump that moves them fails the job until the patterns are re-confirmed. The job is slow: the runner test is the long pole under ASan (about 6 minutes locally), and the step summary lists each binary's time. SlaFreshness runs `se-sleighc`, upstream's SLEIGH compiler, as a child, and that compiler is itself not clean under the sanitizers (an invalid downcast to `LabelSymbol` at `slgh_compile.cc:2340`, a load of an out-of-range enum at `semantics.hh:88`, a few leaked bytes), so the test fails under them; its log is kept under `informational/` and does not gate. Logs are uploaded as the `sanitizer-logs` artifact. |
| `vendored-tree` | Same triggers. Sparse-clones Ghidra at `PINNED_COMMIT`, runs `vendor.py` in a copy of the vendored directory, and diffs the copy against the committed tree (`sh-2.sla` excluded; it is a build product checked by `SaturnExplorerSlaFreshnessTests`). | The committed engine, spec files, `sources.cmake` and licence files are exactly the pinned upstream plus `patches/`: no hand edit, no unrecorded patch, no stale file. It also checks that `VENDOR.md` names the same commit. |
| `generator-nightly` | 02:37 UTC every night (on the default branch) and on manual dispatch (inputs `seeds`, `seed`). Builds `SaturnExplorerSh2GeneratedTests` in RelWithDebInfo and runs it with `SE_GENERATED_SEEDS=20000`; uploads the log as `generator-log`. `SE_GENERATED_SEED=<n>` replays one seed. | The generator finds no violation over 20000 seeds. Until `program/generator` merges, the job finds no such target, prints a notice and skips its build and run steps; once the target exists they are hard steps and a violation fails the job. |

## The cycle

Two agents per area, never the same agent reviewing its own fix.

- **Reviewer.** Adversarial. Its only output is findings in the fixture format below, each with
  severity, the current output, the expected output, and which oracle accepts or rejects it. A
  finding without a fixture is a question, not a finding. Alternate model families between
  rounds.
- **Fixer.** Reproduces each fixture first, fixes the root cause (not the shape), widens the
  oracle so the class is caught, mutation-checks every new test (revert the fix, the test
  fails; restore, it passes), reruns the matrix, records the round, commits. The next round's
  reviewer verifies the previous fix.
- **Coordinator.** Keeps the findings ledger, deduplicates, merges green branches to master,
  writes the next reviewer brief from the open rows and the generator's failures, declares an
  area done.

Branches: area agents work on `review/<area-number>-<name>` and Phase 0 agents on `program/*`;
only the coordinator commits to master, after the matrix passes. (This narrows the repository's
"commit directly to master" convention while several agents run at once.)

The matrix, run whole before any merge: the full `ctest`; the game corpus with every ledger
count zero; ASan, UBSan and LeakSanitizer on the five decompiler suites; the generator's short
run; a timing comparison against the previous master.

## Fixture format

A finding is a `Fixture` as `Sh2DecompilerTests.cpp` already defines it: the SH-2 words from a
base address, the offset and value of a literal-pool word (further literals go in the words),
and the request settings (volatile ranges, lent prototypes). With it: the expected ledger
(`Ledger(r, {...})`), expected text fragments or notes, and the oracle verdict. Reviewers put
fixtures in the ledger row; fixers move them into the suite verbatim.

## Findings ledger

`Docs/Decompiler/REVIEW_LEDGER.md`: one row per finding. `id` is `A<area>-<n>`. Status is
`open`, `fixed <commit>`, or `accepted: <one-line reason>`. A row never leaves the table.

## Areas, oracles, and what done means for each

Done, per area: two consecutive review rounds with no P1 or P2; every P3 fixed or accepted with
a reason in the ledger; the generator run overnight once on that area's grammar with no
violation; no open P1 or P2 rows.

| # | Area | Oracle | Corpus / generator |
|---|---|---|---|
| 1 | Snapshot, cache and engine-input invalidation | A model test: random sequences of edit, scrub, Refresh, reconnect, source replacement and prototype change, checked against a from-scratch recomputation. Invariant: a shown result's key equals the key of the current inputs, callers included. | Generated sequences over the runner and panel |
| 2 | SH-2 semantics and SLEIGH | Exhaustive decode of all 65,536 words against Capstone and Mednafen's decoder; differential execution of generated sequences on the p-code emulator against Mednafen's SH-2 core; the printed C of register-only generated functions compiled with the system C compiler and run beside the emulator (`SaturnExplorerSh2PrintedCTests`: the pseudocode's value against the code's). | Generator, every delay-slot and PC-relative shape; the register-only grammar |
| 3 | Calling conventions and prototypes | From the emulator trace of generated caller/callee pairs: a lent prototype never drops a register the callee reads before writing, never claims a return not written on every path; nothing incomplete is lent. | Generated pairs |
| 4 | Control flow and boundaries | Every instruction executed on random inputs is in the body or named in a warning; tables, shared epilogues, fallthrough, data in code, flow out of captured memory. | Generator, game corpus |
| 5 | Hardware-register semantics | The conservation oracle; the map audit against Mednafen (`HARDWARE_REGISTER_AUDIT.md`) and its width table (every register, width and direction; `FrontEnd/tests/HardwareWidthTable.golden.txt`, which is printed from the rules and so only pins them), checked against the case labels Mednafen's handlers decode (`Docs/Decompiler/tools/derive_handler_cases.py` -> `FrontEnd/tests/MednafenHandlerCases.txt`, compared cell by cell in `SaturnExplorerHardwareWidthTableTests`, with the lane mask of each label, every misaligned on-chip address, and a note required for every write that acts whatever is written); the emulator's access trace. Inferred volatility for polled memory once the oracle is in. | Generator with mailboxes and registers, game corpus |
| 6 | Worker lifecycle and exhaustion | Fault injection as a matrix: allocation k fails for every k across a run; cancel at every phase boundary and every thousand ops; under ASan and LSan. Invariant: no crash, no damaged engine without a report, no result for a stale generation. | Fixtures, the largest game functions |
| 7 | Pseudocode-to-assembly mapping | Every token address lies in the body; a line several instructions contribute to is marked; navigation round-trips. | Game corpus |
| 8 | Trust indicators | A state model of stale, unsupported, truncated, unimplemented, unavailable and too-large, driven by area 1's sequences. | Generated sequences |
| 9 | BIOS and external files | Loaders fuzzed with truncated and malformed inputs; concurrent instances; link handling. | Generated files |
| 10 | Vendored patches | The reproduction job; one review per patch against pinned upstream; upstream bugs filed. | CI |
| 11 | Validation and test strength | Mutation score per oracle; no skipped tests; the generator covers its grammar. | The suites themselves |
| 12 | Platforms and release | The CI matrix, a release checklist on a tagged build. | CI |

Priority 1 areas (1 to 6) run first, as two or three parallel tracks; Priority 2 areas (7 to 10)
are mostly single sessions once CI exists; 11 is the generator and the mutation audit; 12 is CI.

## Done, overall

All twelve areas done; the CI matrix green for a week with the nightly generator running; one
final cross-area review round with no P1 or P2; the release checklist passing on a tagged
build. The nightly run and the matrix stay on afterwards, so a regression is found by a machine
before a reviewer sees it.
