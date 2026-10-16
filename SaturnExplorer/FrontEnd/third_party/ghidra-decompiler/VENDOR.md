# Vendored: Ghidra decompiler + SuperH SLEIGH spec

- **Upstream:** https://github.com/NationalSecurityAgency/ghidra
- **Commit:** `918d44ebc2962a53d23eeee52b8f44c7816e7815` (`master`, `application.version=12.3`,
  "DEV"; the tree the feasibility spike was run against). Pinned in `vendor.py`.
- **License:** Apache-2.0 — `LICENSE` and `NOTICE` are upstream's, copied verbatim, and must ship
  with any distribution (Docs/Decompiler/DESIGN.md §8).
- **Modified:** only by the patches in `patches/`, applied by `vendor.py`. Each patched file
  carries a "Modified for SaturnExplorer" line in its licence header (Apache-2.0 §4(b)).

Why it is here, and how it is used, is in `Docs/Decompiler/DESIGN.md` and `PLAN.md`.

## What's included

| Path | Contents |
|---|---|
| `cpp/` | From `Ghidra/Features/Decompiler/src/decompile/cpp`: the 84 sources of the Makefile's CORE + DECCORE + SLEIGH groups plus `sleigh_arch`, `inject_sleigh` and `libdecomp`; the SLEIGH compiler's `slgh_compile`, `slghparse`, `slghscan` (the bison/flex outputs upstream checks in, so neither tool is needed); and the 94 headers those reach. Not the Java-process bridge (`ghidra_*`), BFD, the XML/raw load images, the console or the tests. |
| `processors/SuperH/` | From `Ghidra/Processors/SuperH/data/languages`: `sh-2.slaspec`, `superh.sinc`, `superh.pspec`, `superh.cspec`, `superh.ldefs`. |
| `processors/SuperH/sh-2.sla` | **Generated**, checked in: `se-sleighc` output for `sh-2.slaspec`. |
| `sources.cmake` | **Generated** by `vendor.py`: the library and compiler source lists. |
| `patches/` | The upstream patches, with a README giving each one's reason and the test that pins it. |
| `vendor.py` | Copies the file set from a Ghidra checkout and applies `patches/`. |
| `CMakeLists.txt`, `SaturnExplorerGhidraDecomp.vcxproj` | The two builds of the library (the `.vcxproj` for `RomHackingTools.sln`). |

The embedded copy of the runtime files is `FrontEnd/src/Decompiler/Sh2SpecData.h`, also generated.

## Patches

| Patch | Purpose |
|---|---|
| `0001-cancel-flag.patch` | Host-owned cancellation flag and `CancelError` check-points (PLAN.md A4). |
| `0002-sleigh-reset-null-caches.patch` | `Sleigh::clearForDelete` nulls the cache pointers it deletes, so a `reset` interrupted by an allocation failure cannot make the translator's destructor delete them again at exit. |
| `0003-translator-ownership-on-init-failure.patch` | `Architecture::restoreFromSpec` assigns the translator before `initialize()` instead of holding it in a `unique_ptr`, which deleted `SleighArchitecture`'s shared translator when `initialize()` failed. |
| `0004-funcdata-frees-active-output.patch` | `Funcdata`'s destructor frees the return-recovery state (`ParamActive`) that an analysis abandoned mid-way -- a cancel -- still owns; it leaked one per such cancel. |
| `0005-sleigh-delay-slot-pc.patch` | `bra` and `bsr` give their delay slot the PC it executes at (branch target + 2), so a PC-relative load or `mova` there reads the right literal (PLAN.md A3). Changes `superh.sinc`; `sh-2.sla` and `Sh2SpecData.h` are regenerated from it. |
| `0006-sleigh-sh2-semantics.patch` | Fixes six SH-2 semantics errors in `superh.sinc`, checked against Mednafen's SH-2 core. `bsr`/`bsrf`/`jsr` now write PR before their delay slot. `MAC.L` had a wrong carry; it is rewritten as a 64-bit accumulate with 48-bit saturation. `MAC.W` now does word loads, sign-extends, and saturates at 32 bits. `XOR.B` was an AND. `mov.x Rm,@-Rn` now reads Rm before Rn is decremented. `DIV1` now reads Rm after Rn is shifted. `sh-2.sla` and `Sh2SpecData.h` are regenerated. |
| `0007-cspec-wide-return-order.patch` | `superh.cspec` put a 64-bit return's high half in r1. Ghidra's `piece1` is the most significant piece, and the big-endian SH ABI returns the high half in r0, so the join is now `piece1="r0" piece2="r1"`. `Sh2SpecData.h` is regenerated. |
| `0008-cspec-return-address.patch` | `superh.cspec` declares PR as the return address, so `jmp @rN` with rN copied from PR is a return, not an unrecovered jump table. `Sh2SpecData.h` is regenerated. |
| `0009-keep-unused-loads.patch` | `Architecture::keepUnusedLoads` (off by default; SaturnExplorer sets it): an unused load is held until its pointer is a constant, then the address's volatility decides. Without it a read whose address folded late, or never, was removed: a dummy hardware read was lost. |
| `0010-load-not-duplicated.patch` | `Architecture::loadsNotDuplicated` (off by default; SaturnExplorer sets it): a LOAD is printed once, in place and unconditionally. It becomes explicit when its expression has several uses, when another memory operation (store, load, user op, call) lies between it and where it would be printed, or when it would be printed in the right operand of `&&` or `||`. |
| `0011-ptrsub-char-constant-cast.patch` | `RulePtrsubCharConstant` checks that a type is a pointer before casting it to one (a UBSan vptr report; behaviour unchanged). |
| `0012-load-in-place.patch` | Under the same policy: a LOAD cannot be moved past another LOAD (for-loop recovery moved the loop's step or initialiser statement to the end of its block), and a LOAD whose implied use is in another block becomes explicit (it printed inside one side of a branch though it is made before it). |
| `0013-jumptable-loads.patch` | Under the same policy: a switch's guard is not folded into the switch when the switch block holds a load other than the table lookup (the read would print for the guarded-out values), and two loads from one pointer are two values, so a guard on one does not bound a switch on the other and the guard's read is not dropped. |
| `0014-keep-unused-loads-late.patch` | Under `keepUnusedLoads`, `lastChanceLoad` holds an unused load in every heritage pass, not only the first: a load whose consumer, a store to a global, is removed as overwritten in a later pass was removed with it. |
| `0015-proto-param-replace-order.patch` | A prototype parameter's replacement is allocated before the old one is freed (an allocation failure there was a double free at teardown); and a switch's table-lookup flag (0014) goes only on the loads between the switch variable and the jump, not on a selector read from memory. |
| `0016-jumpassisted-leak.patch` | A failed assisted jump model (tried when an indirect jump's address is a volatile read) is freed instead of leaked. |
| `0017-access-fates.patch` | `Funcdata` records how each LOAD and STORE the flow made ended (folded into a variable of ordinary memory, removed as dead, removed with an unreachable block, or the read of a recovered switch's table), so the host's oracle can account for every memory access instead of excusing some by a heuristic. |
| `0018-hold-every-load.patch` | Under `keepUnusedLoads`, `lastChanceLoad` holds every load, not only the ones unused when it runs: a load whose use a rule folds away later (`xor r0,r0`, `cmp/eq r3,r3`, an AND with 0) or whose only consumer is dead in the same pass was removed with its use, a register or marked read included. A recovered switch's table lookup is released from the hold. `ConditionalExecution` no longer removes an op that reads a volatile Varnode (a read in transit between its LOAD and its user op) with the block it is in. |
| `0019-sleigh-sr-rte.patch` | `ldc`/`ldc.l` to SR keep only T, S, I3-I0, Q and M (`0x3F3`); `rte` masks the stacked SR the same way and restores it before its delay slot, not after. Changes `superh.sinc`; `sh-2.sla` and `Sh2SpecData.h` are regenerated from it. |
| `0020-sleigh-cache-lru.patch` | `DisassemblyCache::getParserContext` makes a hit the newest entry (least recently used, not oldest allocated). A delayed branch whose tree was cached but whose slot's was not took its own tree for the slot and was built from the slot's parse. |
| `0022-sleigh-scan-64bit-literal.patch` | The SLEIGH compiler's scanner parses numeric literals as 64-bit on every platform (`std::stoull`), so the spec's 64-bit constants build on Windows. |
| `0021-flow-instructions.patch` | `Funcdata` records every instruction the last flow translated (`getFlowInsns()`: the address, and whether flow halted there for bad or unimplemented data), taken from `FlowInfo::visited` as `followFlow` returns and cleared with the function; likewise the indirect jumps whose destinations it recovered (`getFlowTables()`). A restart in the actions follows the flow again, so the last flow is the function's; the host reads the body and the jumps from it, not from the p-code that survived the actions. |
| `0023-allocation-failure-order.patch` | An allocation failure unwinds cleanly: the printer's emitter and encoder are replaced allocation first, the blocks of a collapsed set (a recovered switch) change owner only after the allocation that can fail, the engine's `ostringstream` throws instead of cutting text short, and the cheap leaks on the way out are closed (`Architecture`, `OptionDatabase`, `DisassemblyCache`, `Varnode`, `PcodeOp`, `Comment`, `Datatype`, `BlockCopy`). |
| `0024-switch-lookup-used.patch` | `JumpBasic::foldInOneGuard` (under `loadsNotDuplicated`) does not fold a guard into a switch when the switch's table lookup has a use outside the table's own calculation: the register the entry was loaded into, read after the switch. `noInterveningStatement` lets a COPY or SUBPIECE of the lookup through whatever uses its output, and the folded switch then printed the lookup ahead of every index. |
| `0025-sleigh-trapa-stack.patch` | `trapa` gives r15 its 8 bytes back after the call (the handler's `rte` pops them), so the stack after a trap is where the caller left it. The emulator never runs the op (it ends an instruction at a call), so the differential run still sees the two pushes. Changes `superh.sinc`; `sh-2.sla` and `Sh2SpecData.h` are regenerated from it. |
| `0026-cspec-call-clobbers.patch` | `superh.cspec`: a call kills r0, r1, r4-r7, MACH, MACL and SR (T), as the SH ABI has them caller-saved, and keeps VBR; before, a register in neither list read after a call as the value before it. `Sh2SpecData.h` is regenerated. |
| `0027-switch-default-is-case.patch` | `JumpBasic::foldInOneGuard` does not fold a guard into a switch when the guard's target is also one of the switch's destinations and a value the destination reads differs between the guard's edge and the switch block's (a `mova`'s r0): the default path took the switch block's value, and a reachable block was removed as unreachable. |
| `0028-funcproto-set-effects.patch` | `FuncProto::setEffects`: the host sets one prototype's side-effect overrides (the registers a callee was proved to keep are unaffected by a call to it, where patch 0026 has the model kill them for every callee). |
| `0029-jumptable-signed-guard.patch` | `JumpBasic::calcRange` does not assume the switch value is non-negative: a table bounded only by a signed compare (`cmp/gt`, `cmp/ge`) is no longer recovered as 0 to the bound; the jump is reported as unresolved and the signature withheld (ledger A4-7). |
| `0031-access-past-space-end.patch` | A LOAD or STORE whose constant address and size run past the end of the space stays a LOAD or STORE instead of failing the function ("beyond end of address space", ledger A2-11). |

MSVC and Apple clang needed no patch beyond these.

## Builds

- CMake: `SaturnExplorerGhidraDecomp` (object library), `se-sleighc` (host tool), `se-regen-sla`
  (regenerates the checked-in spec artifacts), all only when `SE_ENABLE_DECOMPILER` is on.
- MSBuild: `SaturnExplorerGhidraDecomp.vcxproj`, referenced by `FrontEnd.vcxproj`. It globs
  `cpp\*.cc` minus the three compiler files, which works because `vendor.py` puts nothing else
  there.
- Both link every engine object into the consumer (CMake: an OBJECT library; MSBuild:
  `UseLibraryDependencyInputs`). The engine's print languages and other capabilities register
  through static objects nothing references by name, so an ordinary library link may drop
  them, and the first `Architecture` then throws "No print languages registered".
- Both: C++14, optimised in every configuration, MSVC `/EHsc /W3 /bigobj` with `_WINDOWS`
  (upstream's own Windows define), clang/gcc with the sign-compare and similar warnings off.

## Bumping Ghidra — checklist

Each step's failure stops the bump; nothing is committed until the last step passes. Order
matters: the spec artifacts are generated from the patched `superh.sinc`, so `vendor.py` (which
applies 0005 to 0008) must run before `se-regen-sla`.

- [ ] **Check out** the new Ghidra commit. A sparse clone is enough:
  ```
  git clone --filter=blob:none --sparse https://github.com/NationalSecurityAgency/ghidra.git
  cd ghidra && git sparse-checkout set --no-cone /Ghidra/Features/Decompiler/src/decompile/cpp/ \
      /Ghidra/Processors/SuperH/data/languages/ /LICENSE /NOTICE
  git checkout <commit>
  ```
- [ ] **`vendor.py`**: update `PINNED_COMMIT` and the commit at the top of this file, then
  `python3 -I vendor.py <ghidra-checkout>`. It stops, changing nothing, if a patch no longer
  applies. Then re-base that patch by hand and re-prove it:
  - 0001: `Docs/Decompiler/spike/cancel_spike.cpp` and `DecompilerRunnerTests`;
  - 0002 to 0004: the ASan run of `DecompilerRunnerTests`;
  - 0005: `TestDelaySlotPcRelative`;
  - 0006: `SaturnExplorerSh2SemanticsTests` (runs the instructions on the p-code emulator against a
    reference model);
  - 0007: `TestPrototypeEligibility` (a 64-bit return and its caller);
  - 0008: `TestFlowUncertainty` (a return through `jmp @r1`).
  - 0009: `TestHardwareVolatility` (unused reads through a late-folding and an indexed pointer).
  - 0010: `TestHardwareVolatility` (the access ledgers: every access printed once, in order, unconditionally);
  - 0011: `Sh2DecompilerTests` under UBSan with vptr.
  - 0017: `TestHardwareVolatility` (every reference-run access of a fixture is printed, folded, or removed with said code) and `TestGameCode` (0 dropped).
  - 0021: `TestFlowReachedBody` (a jump into a second function and from there into a third: both warned, the dead first instruction in the body), and the generator's `outside-body` check.
  - 0022: `TestHardwareVolatility` (`switchUsed`: the table read of a switch whose entry register is used after it stays under the guard), and the generator's `folded-into-switch` check.
  - 0025: `TestCallsAndTraps` (a register saved before a `trapa` and restored after it comes back; a word at r15 is the caller's) and `TestTrapaStack` in `SaturnExplorerSh2SemanticsTests` (the emulator enters the handler with the two words pushed). `SaturnExplorerSh2DecodeTests` does not see it; that is expected.
  - 0026: `TestCallsAndTraps` (r0, r1, r4-r7, T, MACH and MACL after a call are `extraout_`; VBR is not), and the generator's lent-prototype checks.
  - 0024: `TestHardwareVolatility` (`switchUsed`: the table read of a switch whose entry register is used after it stays under the guard), and the generator's `folded-into-switch` check.
  - 0027: `TestDefaultAlsoCase` (the default that is also a case: the write under the join's test stays, the control still folds), and the generator's replay of ledger A4-4 (seeds 68620 and 107923).
  - 0028: `TestCallsAndTraps` ("A3-2": a lent callee that keeps r4-r7, MACH, MACL or SR leaves them the caller's own; one that changes them does not), and the generator's `effects-ignored` check.
  - 0029: `TestSignedGuardTable` (a table bounded only by `cmp/gt` or `cmp/ge` is an unresolved jump, warned, withheld; `cmp/hi`, `cmp/pz` with `cmp/gt` and a zero-extended index are still switches), and the generator's `table-case` check for index -1 (grammar 2's signed bound checks).
  - 0031: `TestAccessPastSpaceEnd` (a constant access past 0xFFFFFFFF decompiles and is printed; the last longword and byte of the space are folded as before), and the generator's seed 219488.


  If upstream's Makefile source groups changed, update the lists in `vendor.py`.
  If `LICENSE` or `NOTICE` changed, check that the About dialog's wording still holds.
- [ ] **Build** with `SE_ENABLE_DECOMPILER=ON` on clang/gcc, and MSVC through both
  `build-release.bat` and `build-release-msbuild.bat`. A new MSVC error inside a vendored file is a
  new `patches/` entry plus a row above, never an edit in place.
- [ ] **`se-regen-sla`**: `cmake --build build --target se-regen-sla` regenerates
  `processors/SuperH/sh-2.sla` and `FrontEnd/src/Decompiler/Sh2SpecData.h` together. Commit both:
  the app embeds the header, and `SpecBundle` rewrites a user's materialised copy when its hash
  changes.
- [ ] **`SaturnExplorerSlaFreshnessTests`**: it proves the checked-in `.sla` and header match what
  this `se-sleighc` produces from the checked-in spec.
- [ ] **Engine tests**:
  - `SaturnExplorerSh2DecompilerEngineTests`;
  - `SaturnExplorerSh2DecompilerTests` (fixture 3, hardware names, BIOS names, prototypes);
  - `SaturnExplorerSh2DecompilerFixtureTests`;
  - `SaturnExplorerDecompilerRunnerTests`;
  - `SaturnExplorerDecompilerPanelTests`.
- [ ] **Review goldens**: the decompiler tests print each fixture's pseudocode. Save
  `ctest -V -R 'Sh2Decompiler|DecompilerPanel'` output before and after the bump and diff it. Accept a change only when it reads as correct for the bytes. A literal changing, or a
  flagged slot no longer flagged, is a regression even if a test was loosened to pass. Record the
  bump, with the CI run, in `Docs/FunctionalityVerification/`.
