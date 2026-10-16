# Response to the external ledger review at f0684b9

Reviewer's document: `SaturnExplorer-Ledger-Review-f0684b9.md` (same directory). Fixed on branch
`review/gpt-ledger-review-fix`, from master 979edd2 and merged with 139a6e4 (the area 6 round 3 review, whose
A6-19 and A6-20 are the same defect as finding 4). Every finding was verified against the current tree before
anything was changed. Master had moved from f0684b9 to 979edd2: A3-3 and A2-11 were fixed in between.

| # | Finding | Severity | Verdict | Row | Fix |
|---|---|---|---|---|---|
| 1 | Known callee clobbers | P2 | already fixed (A3-3); verified; oracle pinned | none | `ee154b7` (pins only) |
| 2 | Cold global initialization failure | P2 | confirmed, fixed | A6-30 | `b964b91` (test first: `dfadefb`) |
| 3 | Rosetta job runs executables it does not build | P2 | confirmed, fixed | A12-10 | `d415247` |
| 4 | Fault suite missing from the sanitizer gate | P2 | confirmed, fixed (with A6-19, A6-20) | A12-11 | `d415247` |
| 5 | A2-8 acceptance cites a guide text that did not exist | P3 | confirmed, fixed | A2-30 | `61b9357` |
| 6 | Generator admits address-error executions | P3 | confirmed (domain, not an engine defect), fixed | A11-2 | `39ceee4` |
| 7 | A5-16 CRAM byte write: wrong value | P2 | confirmed (output cannot be shown right), open | A5-50 | ledger `1f9144f` |

Ledger rows are in `REVIEW_LEDGER.md`; A6-19 and A6-20 are closed with A12-11 and cross-reference it.

## 1. Known callee clobbers (P2): no new row

The review's three replays were run on the tree at 979edd2 (RelWithDebInfo, `SaturnExplorerSh2GeneratedTests`).

- GBR: `Write_GBR(5); func_0x06009012(); return extraout_gbr;`, the caller's prototype withheld ("it uses gbr after
  calling 0x06009012, which is not known to return it"); the leaf is `void`, `Write_GBR(7)`. Not `return 5;`.
- VBR (`401E` to `402E`, `0012` to `0022`, `411E` to `412E`): the same with `extraout_vbr`, withheld.
- r8: `func_0x06009014(); return extraout_r8;`, withheld; the leaf prints as `return;` (it changes r8 for good, which
  `ReturnCheck` records and `LendEffects` now turns into a kill: A3-3, fixed fdc68ff, b1d783a, fb2a517).

Without `SE_GENERATED_CLOBBERS` the replays report only the generator's own contract complaints (`state-saved`,
`callee-saved`: the grammar's declaration that a function changes a register). With the change declared
(`SE_GENERATED_CLOBBERS=20000,20000`, `40000,40000`, `0,100`) all three pass with 0 findings.

The reviewer's second point stands, and it is the real result of this item: **the generator's oracle does not see the
class.** Mutation: the A3-3 loop in `LendEffects` that kills the r8-r14, GBR and VBR a callee was proved to change,
removed. The three replays then print `return 5;`, and the generator still says `0 finding(s)`. Check 12 (`kept-read`)
reads a register's *entry* value, not a register the caller itself set before the call, and the preservation comparison
skips the control registers. The existing `TestCallsAndTraps` cases of A3-3 in `Sh2DecompilerTests` pin the engine side
(round 3 mutation-checked them). The oracle side is now pinned by `TestCalleeClobberPins` in `Sh2GeneratedTests.cpp`
(`ee154b7`), run first in every generator run: each replay must have no finding, no `return 5;` and an `extraout_` read.

| Mutation | Result |
|---|---|
| A3-3 kill loop removed (`LendEffects`) | the three replays: `return 5;`, 0 generator findings (the gap); `TestCalleeClobberPins` fails 6 checks (GBR, VBR, r8: stale constant and no `extraout_`) |
| restored | 0 failures |

Not done: a general value check (the stale constant is found by text, for these three shapes; a counterpart for the
random modules would need the generator to evaluate the printed C, which only grammar 3 does).

## 2. Cold global initialization failure (P2): A6-30

Reproduced first. The new `cold` / `cold-disasm` families of `DecompilerFaultTests` (committed alone as `dfadefb`)
fail on 979edd2's code. A cell is a fork of a parent that never ran the engine; allocation k of the first request
fails, with the spec bundle already extracted and with an empty config directory; the stack of the failing allocation
says whether it fell in `MaterialiseSh2SpecBundle` or `startDecompilerLibrary`, so no index is a contract. Findings on the
old code (1,105 cells `cold`, 1,101 `cold-disasm`):

- 167 cells: the first request is out of memory and every later one `Unavailable` with an empty explanation (a failure in
  the materialisation, which `StartLibrary` had already committed).
- 6 cells: the next request is `Failed("No print languages registered")` (allocations 63 to 106 of the first request;
  the review's index 100, 56 bytes, is among them).
- 882 cells: every allocation of `startDecompilerLibrary` (66 to 506 and 107 to 547) is reported as a plain out of memory
  with the engine not damaged.
- `Disassemble` marks the engine damaged for an out of memory in the materialisation.

Fix (`b964b91`): `StartLibrary` is Fresh / Ready / Refused / Interrupted. A failure in the materialisation (filesystem
work, no engine global written) leaves it Fresh: the request fails and the next starts again. A failure inside
`startDecompilerLibrary`, which appends to process-global tables and can neither be re-run nor finished, makes it
Interrupted for good; every call, the first included, answers `Decompiler unavailable: the engine's library did not
finish starting (out of memory); restart Saturn Explorer to decompile again`, as `Unavailable` for Decompile and as one
line for Disassemble, without entering the engine. Not retried (the reviewer's warning: the registration is not
idempotent). `Disassemble` calls `StartLibrary` outside its damage boundary. The guide's troubleshooting table has the text.

Regression: 0 of 1,105 and 0 of 1,101 cells fail; 882 cells in each family are refused with the reason (310
`AttributeId::initialize`, 562 `ElementId::initialize`, 4 `CapabilityPoint::initializeAll`, 6 elsewhere); `SE_FAULT_EXIT=static`
(shutdown and exit through static destruction after the refusal) passes. The sweep fails if it reaches none of the three
steps. About 30 s in the default subset (RelWithDebInfo), a few minutes under ASan.

| Mutation (each rebuilt, `SE_FAULT_ONLY=cold`) | cold | cold-disasm |
|---|---|---|
| blind retry after the interrupted start (stage back to Fresh) | 883 fail | 883 fail |
| a failed materialisation committed with no explanation | 167 fail | 167 fail |
| the stage committed before the materialisation | 173 fail | 173 fail |
| the refusal without the restart text | 883 fail | 883 fail |
| Disassemble lets the exception reach its damage boundary | 0 | 167 fail |
| fix reverted (the reproduction above) | 1,056 fail | 883 fail |

Not done: the other global of the same kind, `SleighArchitecture`'s translator map, is covered by the warm families
(a failure inside the engine marks it damaged); nothing here changes the vendored library (no patch 0039).

## 3. Rosetta job target mismatch (P2): A12-10

Confirmed: `macos-x86_64-decompiler` built an explicit list without `SaturnExplorerSh2DecodeTests` and
`SaturnExplorerSh2GeneratedTests` and selected them with ctest. Fix (`d415247`): the job's `env` has one list,
`SE_ROSETTA_TESTS`; the build step turns it into `SaturnExplorer<name>Tests` targets, the run step into the `-R`, and a
step in between runs `.github/scripts/check-selected-tests-built.sh`, which asks ctest for the selected tests
(`--show-only=json-v1`) and fails naming a test with no executable, or a listed name that selects no test. The `lipo` check
also covers the two. The `native` Windows step that names the same tests builds everything first and is unchanged.

Evidence (Linux, the script is shell and Python only): with all built, `All 3 named tests are selected and built.`; with
`SaturnExplorerSh2DecodeTests` moved away, `SaturnExplorerSh2DecodeTests MISSING (not built)`, `Selected-tests check
FAILED.`, exit 1; a bogus name fails with `no CTest test named ...`. The 12 names resolve to 12 built tests in a full
build. Both workflow files parse (`yaml.safe_load`).

## 4. Fault suite missing from the sanitizer gate (P2): A12-11, with A6-19 and A6-20

Confirmed, and A6-19 / A6-20 (area 6 round 3 review) say the same, so one fix (`d415247`):

- `saturn-explorer-decompiler.yml` builds `SaturnExplorerDecompilerFaultTests`; `run-decompiler-sanitizers.sh` runs its
  default subset after the runner test, `ASAN_OPTIONS=detect_leaks=1`, with `SE_FAULT_FULL`, `SE_FAULT_ONLY`, `SE_FAULT_LEAKS`
  and the others cleared from the environment so the gate cannot be weakened. The test keeps its own accepted-leak table
  (A6-7) and its two allowed UBSan sites (`address.hh:605`, `slghpatexpress.cc:979`), the same two the checker allows.
- `check-sanitizer-logs.sh` has an expected-binary list (ten entries) and fails on a missing log or exit status, on an empty
  log, and on a log without the binary's own pass line (`all passed`, or `fault matrix: 0 of N job(s) failed`) (A6-20).
- `.github/scripts/test-sanitizer-harness.sh`, run as the first step after checkout, proves the gate with stub binaries:
  11 cases (clean; the two accepted UBSan lines; fault stub exit 42; `1 of 25 job(s) failed`; an ASan report; an LSan
  report; an unaccepted UBSan error; the fault stub omitted; an empty fault log; an empty log of another binary; a log
  without its pass line). The new scripts get all 11 right. **The scripts at 979edd2 get 19 outcomes wrong (the self-test counts the
  verdict and, for the fault stub, whether it was invoked)**: the fault stub is never invoked and every failing case passes, the review's
  reproducer (an empty `SaturnExplorerSh2DecompilerTests.log` with `.rc` 0) included.
- A6-19's other points: `SE_FAULT_BACKTRACE` is a feature test (`<execinfo.h>`, `<dlfcn.h>`, `<cxxabi.h>`) instead of
  `__GLIBC__`, so macOS names its pins; a platform that cannot name them fails the pin census (`FAIL:pin-identity-unchecked`,
  `SE_FAULT_NO_IDENTITY=1` accepts it); where the matrix cannot run it exits 77 and ctest reports it skipped
  (`SKIP_RETURN_CODE 77`), not passed.

**Real run, ASan + UBSan + `detect_leaks=1`** (RelWithDebInfo with the CI's flags, `run-decompiler-sanitizers.sh` narrowed to
the fault suite with `SE_SANITIZER_ONLY`, then `check-sanitizer-logs.sh`): `fault matrix: 0 of 34 job(s) failed, 552 s` on a
machine at load 24 (the two cold families took 250 s and 280 s of it); the checker passes. Its first run failed, and that is a
finding of this round: four `cold` cells whose failure fell in the first translator build of the process left leaks the accepted
table did not name (a partly decoded `OpTpl` of the `.sla`, `SleighBase::decode`: new accepted row `A6-30 first translator`, Init
only, once per process, the engine refused afterwards, like A6-7) or refused because the Init-only bound looked at whether any
later phase had been entered instead of the phase the failing allocation fell in (the second pass's Init follows the first pass's
Flow; now this run's own phase log is used). The leaks listed by `SE_FAULT_LEAKLIST=1` for those cells are at A6-7's sites except the
`.sla` decode. Only the default subset was run; the full cold sweep (about 5,000 cells per family) was not, so further sites in the
first translator build may exist. The 32 other jobs, A6-7..A6-15's pins among them, passed unchanged; one run of the generator
under ASan/UBSan (the pins and 300 seeds) shows only the two accepted UBSan lines.

## 5. A2-8 acceptance cites a guide text that did not exist (P3): A2-30

Confirmed: `DECOMPILER_GUIDE.md`'s Limits said nothing about address errors, illegal instruction or illegal slot,
interrupts, `sleep` or `tas.b`. `61b9357` adds one Limits entry with five sub-points (the tas.b text matches the SLEIGH:
a read, T = (byte == 0), a write with bit 7 set), and the A2-8 acceptance now cites it. No model change.

## 6. Generator alignment / domain gap (P3): A11-2

The engine side of seed 219488 was A2-11 (patch 0031) and is fixed; the seed passes. What remained is the oracle's
domain. `39ceee4`:

- Generation: r4/r5 alignment is tracked (lowered by `@Rm+` / `@-Rn` of a smaller width; an advance in a loop body keeps it;
  the returned `mov.x @Rm,r0` respects it). `SE_GENERATED_ALIGN=off` restores the old programs, bit for bit.
- Harness: a run whose trace holds a misaligned word or longword access (ordinary, perturbed, or a replay) is excluded
  from the trace-based checks and counted (`excluded.address-error.runs`, `.perturbed-runs`, `.functions-without-runs`),
  its seeds listed in the summary. Nothing else is suppressed: a Failed status or an engine error string is still a finding.
- Pins (`TestDomainPins`, first in every run): `6145 6242 000B 0009` has 3 excluded runs, its control `6146 6242 ...` 0;
  seed 219488's shape on r1 (`2118 8F01 2125 2132 000B 0009`; r1 is 0 in the first input mode, where r4 is a scratch
  pointer in every run) excludes the r1 = 0 run; seed 219488 itself has 0 findings with alignment untracked (8 excluded
  runs) and tracked (0).
- 3,000 grammar-2 seeds from 200001: 232 seeds / 934 runs excluded tracked, 329 / 1,496 untracked. The rest is classified, not
  prevented (a pointer a caller advanced and passes to a callee, a `mac` inserted after the items around it were drawn,
  GBR and literal displacements).

| Mutation (generator rebuilt, pins only) | Result |
|---|---|
| address errors never recognised | pins 1 and 3 fail |
| recognised, not left out of the checks | pins 1 and 3 fail |
| every word and longword access excluded | control pin and the seed-219488 tracked pin fail |
| no alignment lowering after `@Rm+` / `@-Rn` | seed-219488 tracked pin fails (8 runs) |
| the returned load ignores alignment | seed-219488 tracked pin fails (8 runs) |

Generator matrix: 3,000 grammar-2 seeds from 200001 (the review's range): 2,999 seeds pass, 1 fails: seed 201436, `cache-read-pointer-unsaid` (a 1-byte read of VDP2 register at 05F800F0 through the cached partition, printed through `&REG` with no cache-read note). It fails identically with `SE_GENERATED_ALIGN=off` (the old generator's programs), so it is not caused by this round; it is not one of this round's rows and is reported to the coordinator. 232 seeds have a run excluded as an address error (934 runs, 117 functions left without a run).

## 7. A5-16 CRAM byte write: A5-50 (open, P2)

Not fixed (area 5's fixer is in that code). Added as a row with the review's argument. Checked against the Mednafen copy
in this container: `vdp2.c` `RW_W8` writes the CRAM entry with `*DB` and no byte mask (VRAM's branch masks), and
`SH7095_BSC_BusWrite_u8` builds `DB` as `(SH7095_DB & ~mask) | (V << shift)`, so the lane the SH-2 did not write holds the last
bus value, not the old entry. Current output on the fixture (`SE_GENERATED_WORDS="D103 E003 8011 6011 000B 0009 0009
0009 25F0 0000"`): `iRam25f00000 = CONCAT11(iRam25f00000._0_1_,3); return (int4)iRam25f00000;`. The output cannot be shown
right, so P2 as the review asks; the A5-16 (4) acceptance stays in its row, the new row supersedes it for the value.

## Matrix

| Check | Result |
|---|---|
| whole `ctest` (RelWithDebInfo, 97 tests) | 100 % passed (166 s); includes `SaturnExplorerDecompilerFaultTests` (103 s), `Sh2GeneratedTests` with the new pins, `Sh2PrintedCTests` |
| cold-start families, default subset | 0 failed of 1,105 and of 1,101 cells |
| hwram008 corpus (`SE_GAME_HWRAM=.../hwram008.bin`, `SaturnExplorerSh2DecompilerTests`) | every count 0: 0 accesses dropped, 20 missing and said, 0 functions without a reference, 0 ended dead; exit 0 |
| workflow YAML | `yaml.safe_load` of both files; the shell steps run locally (self-test, selected-tests check) |
| generator, 3,000 grammar-2 seeds from 200001 | 2,999 seeds pass, 1 fails: seed 201436, `cache-read-pointer-unsaid` (a 1-byte read of VDP2 register at 05F800F0 through the cached partition, printed through `&REG` with no cache-read note). It fails identically with `SE_GENERATED_ALIGN=off` (the old generator's programs), so it is not caused by this round; it is not one of this round's rows and is reported to the coordinator. 232 seeds have a run excluded as an address error (934 runs, 117 functions left without a run). |

## What was not verifiable here

- macOS and Rosetta (finding 3, A6-19's `backtrace` gate on macOS): no runner; the macOS pin names under clang are unseen.
  If `dladdr` names differ, the census now fails loudly (`FAIL:pin-identity-unchecked` or a stale pin), which is the intent.
- Windows (`SKIPPED`/77 path is compiled only on non-POSIX).
- The sanitizer job itself on GitHub: the local run is RelWithDebInfo with the CI's flags, not Debug, only for the fault suite
  (and the generator), not the other eight binaries; `SaturnExplorerDecompilerPanelTests` did not link in that build
  (undefined `typeinfo for sfe::ContextBackend`, unrelated to this round, perhaps the build type; not investigated).
- No private game corpus other than hwram008.
- `act`-style execution of the workflows: not available.
