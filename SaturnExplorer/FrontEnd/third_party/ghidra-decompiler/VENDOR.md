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
| `0030-host-stack-alias.patch` | `ScopeLocal::declareHostAlias`: the host declares a pointer into the stack frame that no Varnode shows (the address of a returned structure, in r2, which the calling convention has no input for), so the locals from that offset up are aliased and a call may write them. |
| `0031-access-past-space-end.patch` | A LOAD or STORE whose constant address and size run past the end of the space stays a LOAD or STORE instead of failing the function ("beyond end of address space", ledger A2-11). |
| `0032-allocation-failure-order-2.patch` | More allocation-failure ordering (area 6, round 2): `FuncProto::copy` and the two `ProtoStore*::clone` functions clone before they free; `HighVariable::mergeInternal` builds the merged list before it moves any Varnode; `VarnodeBank` (`setInput`, `setDef`, `makeFree`) and `ScopeInternal::renameSymbol` keep what they cannot put back; `PcodeOpBank::destroy`, `MapState::addRange`, `addSymbolInternal`, `addMapPoint` and `addSymbolWithConflict` leave nothing owned by nobody; four unreached `delete X; X = new` sites reordered (`setParamshift`, `TypeCode::setPrototype`, `Varnode::calcCover`, `circularqueue::setMax`); the cloned action tree, `newBlock*`, the virtual root, the call sites, `PackedDecode`'s buffers and `Database::attachScope` leave nothing leaked after a failure. |
| `0033-engine-printed-value-fixes.patch` | Three printed-value defects: a return in the join space (r0:r1) is consumed whole, not narrowed to its low half (r1) and printed as the 4-byte return (r0); a boolean-typed constant other than 0 and 1 is printed as the integer, not `true`; an input replaced by a subvariable flow's temporary that an operation still reads is read zero-extended from the logical input, not as an input no register holds. |
| `0034-sleigh-control-register-writes.patch` | `ldc` to SR, GBR and VBR (and `ldc.l`) call the user ops `Write_SR`, `Write_GBR`, `Write_VBR` with the value written, so a write that outlives the function stays in the pseudocode and its source stays a parameter. Changes `superh.sinc`; `sh-2.sla` and `Sh2SpecData.h` are regenerated from it. |
| `0037-engine-division-piece-and-input-names.patch` | `RuleDivTermAdd`/`RuleDivTermAdd2` keep a 33-bit-multiplier product that wraps an 8-byte multiply only when every reader is a right shift that completes a division (a scaled quotient printed the wrapped product); `RulePiecePathology` needs the concatenation to write the register the SUBPIECE reads (a stack insert dropped the untouched bytes from the return); an input piece of a register is named with its byte (`in_r3_b3`). Ledger A2-21, A2-22, A2-25. |
| `0038-sleigh-rte-restores.patch` | `rte` calls `Write_SR(_sr)` and a new user op `Rte_PC(_pc)`, so the restores of SR and PC, and the stores a handler made to the stacked words, stay in the code. `sh-2.sla` and `Sh2SpecData.h` regenerated. Ledger A2-24. |
| `0040-jumptable-tracedag-allocation-failure.patch` | More allocation-failure ordering (area 6, round 3): a recovered `JumpTable` is held until `jumpvec` owns it (and the clones of one, and `JumpBasic::clone` and its siblings, are `unique_ptr`s until they are returned); `JumpTable::recoverModel` and `setOverride` no longer free the old model before the new one exists (unreachable from the host); `TraceDAG::initialize`, `BranchPoint::BranchPoint` (`createTraces`) and `openBranch` own what they make until a list takes it. Found by the sweeps of `jumptable`'s Flow and of the new `dag` and `loopbrk` fixtures. |
| `0044-engine-round5-printed-value-fixes.patch` | Three engine defects behind ledger A2-31, A2-32 and A2-37. **(1) `Funcdata::ancestorOpUse` (`funcdata_varnode.cc`)** returns false for a CALL ("a call is never a good indication of a single op use"), a rule written for the parameters of a call, and it decides the return trial of the function's own RETURN as well: `return g(p);` is `r0 = CALL g` and a RETURN of r0, so the return was dropped (`g(p); return;`) whenever anything stood between the call and the RETURN that was not a copy. The one thing that did stand there was `nop`, which the spec made `r0 = r0` (0045): a function ending `rts ; nop` returned its call's value and the same function with any other instruction in the slot did not. A RETURN whose value is the output of a call whose prototype is locked (a host lent it) and returns a value, or a register holding a piece of it (the halves of a 64-bit result), is now a return, whatever else the value is used for (`p = g(x); if (p) *p = 0; return p;`). A call of unknown prototype is decided as before. **(2) `divTermIsDivided` (`ruleaction.cc`, 0037's guard)** accepted a right shift whose total with the term's own shift reached the width of the multiply (64 for 8 bytes): the shifts are merged before `RuleDivOpt` runs and a shift by the width is 0, so `x / 0xFFFFFFFE` (and `0xFFFFFFFC`, `0xFFFFFFF0`: multiplier 3, 5, 0x11 in the add form) printed `return 0;` and dropped the parameter. The rewrite now needs a total below the multiply's width; otherwise the unrewritten, correct form stays. **(3) `ScopeInternal::buildVariableName` (`database.cc`)** gives the byte suffix of 0037 (`in_r3_b3`) to the pieces of a register the function keeps (`unaff_r8_b3`) too; `cmp/str` on r8-r15 printed `unaff_r8`, `unaff_r8_00`, ... and the most significant byte was the register's name. |
| `0045-sleigh-nop-writes-nothing-rte-sr-after-slot.patch` | Two spec defects, ledger A2-31 and A2-36. **`nop`** was `r0 = r0` (upstream's "FIXME: intentional nop ... do this to suppress warning", for the compiler's empty-constructor warning), a write of the return register that the engine's return recovery took for the last write and its only use; the sleigh compiler now warns of a temporary written and not read instead (one, harmless: the freshness test does not read warnings). **`rte`** called `Write_SR` before its delay slot (0038), so the SR the text restored was the stacked one and a slot that set T (`rte ; sett`, a compare) was dead at the return; Mednafen's `RTE` restores SR and then runs the slot, and `sr` is restored before the slot as before (a slot that reads T sees the restored one) but `Write_SR(sr)` now follows it. `sh-2.sla` and `Sh2SpecData.h` regenerated. |
| `0046-engine-input-replaced-resets-nzmask.patch` | `Funcdata::opSetInput` resets the non-zero mask of the output of an op whose input is replaced, so a rule that rewrites an op in place (`RuleCollectTerms` moving a coefficient between the ADDs of a tree) cannot leave a mask that claims clear bits the new value has and let `RuleAndMask` drop an `& 0xfffffffc` that is not redundant (ledger A2-47). |
| `0047-sleigh-cached-branch-commits-next-address.patch` | `Sleigh::oneInstruction` puts the next address of a delayed branch's cached parse tree back to the end of the branch before applying its commits, so a second translation of the branch no longer paints the slot's context on the instruction after the slot (ledger A2-48). |
| `0048-engine-identity-write-return.patch` | `Funcdata::ancestorOpUse`: a write of the return register with the value it holds (`mov r0,r0`, `and r0,r0`, `or r0,r0`, `add #0,r0`, `xor #0,r0`, `or #0,r0`) is looked through when the RETURN is decided, so it no longer makes the function return where a `nop` does not (ledger A2-43). |
| `0049-engine-nzmask-forgotten-downstream.patch` | `Funcdata::opSetInput` also forgets the non-zero mask of every Varnode below the output whose mask was worked out from it (stopping at one that already allows every bit), so the partial sums between the two ADDs `RuleCollectTerms` changes cannot keep a mask that claims clear bits the new value has (ledger A2-52). |
| `0050-sleigh-delay-slot-pc-all-branches.patch` | A PC-relative load or `mova` in the delay slot of `bt/s`, `bf/s`, `jmp`, `jsr`, `rts`, `braf`, `bsrf` and `rte` reads the PC the hardware gives it (the branch target + 2), as patch 0005 made it for `bra` and `bsr`: the branch hands the slot a kind through the context register and the slot makes the PC in p-code from the branch register, PR, the stacked PC or T (ledger A2-4). |
| `0051-engine-replaced-varnode-consume-merged.patch` | `Funcdata::totalReplace` ORs the consume mask of the Varnode it replaces into the one that takes over its readers, so a common-subexpression merge cannot leave the byte of SR read by `& 1` and by a whole-byte compare with the consume mask of the first, and `ActionVarnodeProps` cannot replace it by a zero (ledger A2-56). |
| `0052-flow-instruction-alignment.patch` | `Architecture::instructionAlignment` (host policy; SaturnExplorer sets 2): flow halts as at bad data at an address that is not a multiple of it, so a jump table entry that is no address no longer sends flow into the middle of an instruction (ledger A7-6). |
| `0053-engine-stack-pointer-longword-aligned.patch` | `Funcdata::calcNZMask` takes the stack pointer on entry to be aligned to a longword (`~3`), not to 256, so `mov r15,r0 ; and #0xff,r0` is not `return 0;` (ledger A2-53). |
| `0054-engine-cached-masks-never-narrower.patch` | Every change to an op's op-code, inputs or output, and every new reader of a Varnode, keeps the cached non-zero and consume masks around it a superset of the truth (`Funcdata::opMasksChanged`): the changed op's output gets the mask its inputs give, the masks below and above are widened until they already hold what their neighbours give, nothing true is forgotten. Replaces the resets of 0046, 0049 and 0051 (ledger A2-58, A2-59). See "Cached masks" below. |
| `0055-engine-action-repeats-bounded.patch` | A repeating group or pool of actions stops after `Architecture::maxActionRepeats` passes (300) with the warning `Exceeded maximum repeats of <action> with changes still pending`, which the host reports and for which it withholds the signature: two rules that undo each other end as a reported, bounded failure, not as memory taken until the host cancels (ledger A2-59). |
| `0056-engine-stack-walk-down-aliases.patch` | A pointer into the frame that a loop steps down (`*--p = v`, `mov.l Rm,@-Rn` from a copy of r15 or r14) aliases the stack below its base: `AliasChecker` lowers its alias boundary 0xffff below such a base, the guard of the indexed access covers the stack below it (`LoadGuard::establishRange`), a save slot it reaches stays mapped, and the join of the entry r15 is a pointer to the frame, not the stack register (`register0x0000003c`) or a stack parameter. A local the walk wrote no longer prints its first value (ledger A3-65). |
| `0057-engine-stack-walk-down-conditional.patch` | Amends 0056: `AliasChecker::stepsDown` keys its forward walk on `(varnode, offset)`, not the varnode alone, so a pointer stepped down on only some paths of a loop (`if (c) *--p = v;`, a nested MULTIEQUAL between the step and the back edge) is still seen to step down. Without it the stepped offset was pruned at the nested join and the loop reverted to `register0x0000003c` / `return 5;` (ledger A3-72). |
| `0058-engine-pullback-ranges-exact.patch` | A pull-back through an operation (`CircleRange::pullBackBinary`, `pullBackUnary`) returns exactly the set of inputs whose result falls in the range, in the input's mask, or fails: a comparison whose range holds both 0 and 1 is the full range of the input (it was `[0,2)` widened, so `addv` against a literal-pool constant printed `1 < param_1 && ...` for `0x7fffffff < param_1 && ...`), `val - x` is the range `[val-right+step,val-left+step)`, a constant shifted by the unknown and a shift by the size or more are refused, and `INT_SEXT` places its gap by `left % step`. |
| `0060-engine-stack-walk-down-spilled.patch` | Amends 0056 and 0057: a frame address kept in a stack local and read back (across a call or not) and then stepped down in a loop aliases the stack below its base (`AliasChecker::reachesStepDown` follows the base through copies, additions, joins and INDIRECTs to the loop), and the loop's STORE is guarded as a stack store (`Heritage::protectFreeStores` traces its pointer back through joins and INDIRECTs to the free stack word). Without it the local the walk wrote kept its first value, lent when no call came between (ledger A3-80). |

MSVC and Apple clang needed no patch beyond these.

### Cached masks (the invariant of patch 0054)

The engine caches two masks on each Varnode and its rules take them as facts between the actions that work them out: the
non-zero mask (`ActionNonzeroMask`: which bits can be 1) and the consume mask (`ActionDeadCode`: which bits any reader sees).
**At every rule application, a cached mask may be wider than the truth, never narrower.** A narrower one is a wrong text with
no warning (`RuleAndMask` drops an AND, `ActionVarnodeProps` replaces a value by 0: ledger A2-47, A2-52, A2-56, A2-58); a
wider one costs a simplification until the next pass, and forgetting a mask that was still true is what let two rules undo
each other for ever (A2-59). `Funcdata::opMasksChanged` keeps it at every primitive that changes an op or gives a Varnode a
reader. A patch that adds a way to change an op without one of those primitives (a direct `PcodeOp::setInput`,
`PcodeOpBank::changeOpcode`) must call it, and a bump must re-check that `PcodeOp::getNZMaskLocal` and
`PcodeOp::getConsumeLocal` are still the steps the two actions take (`ActionDeadCode::propagateConsumed` calls the second; the
roots in `Funcdata::inputConsumeNeeded` mirror `ActionDeadCode::apply`, `markConsumedParameters` and `gatherConsumedReturn`).
The instrument is `Docs/Decompiler/tools/a2_round9/nzm_check_r9.patch` (after every rule and action, a value contradicting a
non-zero mask, a consume mask that leaves out a bit that changes a root) over `dagen.py`'s families.

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
  - 0030: `TestCallsAndTraps` ("A3-4": a callee that writes the caller's local through r2 is seen to: the read after the call is the local, not the value stored before it).
  - 0031: `TestAccessPastSpaceEnd` (a constant access past 0xFFFFFFFF decompiles and is printed; the last longword and byte of the space are folded as before), and the generator's seed 219488.
  - 0032: `SaturnExplorerDecompilerFaultTests` (its default subset pins the allocations of the patched functions by name and fails if one is gone; reverting `fspec.cc` or `variable.cc` crashes pinned cells; the leak pins need an ASan + LeakSanitizer build, `DecompilerFaultTests.cpp`'s header says how).
  - 0033: `TestArea2Round3` (an 8-byte return of r0:r1, no `== true`, no `unaff_1...`) and `SaturnExplorerSh2PrintedCTests` (the printed value of 300 register-only functions, compiled with the system C compiler, against the emulator).
  - 0034: `TestArea2Round3` (`Write_SR`, `Write_GBR`, `Write_VBR` in the text, the register a parameter), `SaturnExplorerSh2PrintedCTests` (the writes against the registers the run ends with), and the four emulator harnesses' `Sh2EmulatorHooks.h`.
  - 0037: `TestArea2Round4` (A2-21 words print no wrapped product, `x / 7` is a division; the A2-22 stack inserts return 4 bytes; `cmp/str` bytes are `in_r3_bN`) and `SaturnExplorerSh2PrintedCTests` (the division and stack fixtures and the widened register-only grammar against the emulator).
  - 0038: `TestArea2Round4` (`Write_SR(` and `Rte_PC(` keep the handler's stores to the stacked SR and PC), `SlaFreshnessTests`, and `Sh2EmulatorHooks.h` for `Rte_PC`.
  - 0040: `SaturnExplorerDecompilerFaultTests` (its default subset pins the allocations of the patched functions by name and ordinal; the leak pins need an ASan + LeakSanitizer build, as 0032's do; reverting `jumptable.cc`, `funcdata_block.cc` or `blockaction.cc` makes the pinned cells leak).
  - 0044: `TestArea2Round5` (`return g(p)` with the pop in the slot and with a nop, `p = g(x); if (p) *p = 0; return p;`: returned, lent so; `x / 0xFFFFFFFE`, `0xFFFFFFFC`, `0xFFFFFFF0` do not print `return 0;`; `cmp/str` on r8, r14 and r15 names `_b1`..`_b3`) and `SaturnExplorerSh2PrintedCTests` (the divisions against the emulator, the `unaff_rN_bK` bytes given their values).
  - 0045: `TestArea2Round5` (`rte ; sett`, `clrt`, `cmp/eq` in the SR written; the same function returns the same in any slot: `return ++*p`, a compare), `TestArea2Round3`/`4` and the hardware fixtures whose polls were `returned` through a nop (now void), `SlaFreshnessTests`.
  - 0046: `TestArea2Round7SlotValues` (the printed address of a `mova` or `mov.l` in the slot of `bt/s` and `bf/s` at an odd displacement, and the same sum as code, against the run; the `& 0xfffffffc` is in the text).
  - 0047: `SaturnExplorerSh2DecodeTests` (`RunFollowers`: the instruction after a delayed branch's slot reads its own address after the branch was translated once and twice in a row).
  - 0049: `TestArea2Round8` part 5 (eight sums of scaled terms, `c + 8x + 4y + 14x` through `shlr2 ; shll2`, agree with the run; the first prints `& 0xfffffffc`) and the printed-C fixtures `A2-52`.
  - 0051: `TestArea2Round8` part 7 (`cmp/str` on the SR byte after `addc`: the byte is compared whole) and the printed-C fixture `A2-56` (by value).
  - 0052: `TestRoutedSeeds` (a switch with an odd table entry: `halt_baddata()` at the odd address, the flow warning says it is odd, no "overlaps instruction" warning, an odd entry is refused) and the mapping oracle's corpus counts (odd addresses in functions with overlapping instructions, and functions entered at an odd address: both 0).
  - 0053: `TestArea2Round8` part 3 (`mov r15,r0 ; and #0xff,r0` keeps the AND on r15; `and #3` is still 0).
  - 0054: `TestArea2Round9` parts 1 and 2 (the A2-58 joins agree with the run; the A2-59 loops finish, settled), `TestArea2Round8` part 5 and `TestArea2Round7SlotValues` (the sums of A2-47 and A2-52), the canary checks of `TestArea3Round8` and `TestArea3Round9`, the printed-C fixtures `A2-58 ...` and `A2-59 ...`, and the mask checker over `dagen.py`'s families ("Cached masks" above).
  - 0055: `TestArea2Round9` part 3 (the bound lowered to 2: the text printed, the warning passed on, the signature withheld).
  - 0056: `TestA365WalkDown` (the leaf and its variants: bytes, words, from r15 + N, eight longwords, from r14, written then read down, read back up, over a freed save slot, no stack argument; the walks up as controls), the printed-C fixtures `A3-65` (by value, frame mode) and the generator's walk functions, and `TestA365Pins` (seed 901303).
  - 0057: `TestArea3Round10Fix` (the conditional step `if (c) *--p = v;` in r2, in r14 and in an inner loop print the local, not `register0x0000003c` / `return 5;`; the unconditional step and the step under an if/else as controls).
  - 0058: `TestArea2Round10` (`addv r0,r4` against the literal 0xAAAAAAAB, T tested by `bf`: `0x7fffffff < param_1 && ...`, agrees with the run; the immediate as control), `SaturnExplorerSh2DecompilerEngineTests` `TestPullBackRanges` (every pull-back against brute force; reverting any one of the seven comparisons, `INT_SUB`, `INT_RIGHT`, `INT_SRIGHT` or `INT_SEXT` fails it), the printed-C differential's flag-test item (`MakeFlagLit`).
  - 0060: `TestArea3Round10Fix` (`a3_80a` to `a3_80i`: the pointer kept in a stack local across a lent and an unlent call, through r14, by bytes, across two calls, moved down after the read back, copied into a second local, and with no call, lent; each prints the local, not `return 5;`; the walk up as control), and the generator's spill walks (`SE_GENERATED_PRINTEDC=only`: a constant return of every `GenSpillWalkFn` function against the run).


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
