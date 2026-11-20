# The link model: pseudocode to SH-2 instructions (area 7 redesign)

Status: **design, for external review. No product code is changed by this document.** It follows the policy in
`REVIEW_PROGRAM.md`, "Area 7: contract and acceptance": seed-specific P3 fixes are stopped, and a design is approved
before any rewrite. Line numbers and counts are those of master `c58849ae` unless marked; where they matter they are
rechecked against master `c66e781e` (marked "now"). A first independent review's recommendations are folded in
(section 7 records them as proposed decisions, and the edits they asked for are made in sections 2 to 6).

## 1. Problem

A printed token has one address (`Token::addr`), a line has one default (`Line::firstAddr`, its **lowest** token
address), and the way back from an instruction is ordered by rules over those two (`InstructionLineIndex::Ordered`).
That one number is asked to answer three different questions:

| | Question | Who needs it |
|---|---|---|
| (a) | Which instruction **performed** this statement's effect (wrote the bytes, branched, called)? | a breakpoint, a click |
| (b) | Which instructions **contributed** the value shown? | "where does this number come from" |
| (c) | Which address is a **useful click target**, and in what order are lines offered for an instruction? | the panel |

The engine answers none of them directly. A printed op's address (`PcodeOp::getAddr()`) is the instruction's for an op
the flow made, but for an op the engine made later it is only **where the engine put it**: the start of a block
(`Heritage` MULTIEQUALs, `heritage.cc:3758`), the end of a predecessor (`Merge::trimOpInput`, `merge.cc:861`), the end
of a common dominator (`cseElimination`, `Merge` `domCopy`), or the address of whatever op a rule was rewriting. So
six product passes run after the printer and move, add and flag token addresses, and the oracle rebuilds the same
answers from the instruction words to check them. Both are value reconstructions: the product walks the flow's raw
p-code asking "may this store hold the value this line shows" (`StoreShowsValue`, `FlowLastWriters`, `FlowNumberIn`),
and the oracle walks SH-2 words asking the same (`WrittenValue`, `RegisterBefore`, `RegisterMakers`).

| | Lines today | Growth |
|---|---|---|
| Product: value walkers and mapping passes (`Sh2Decompiler.cpp` 6313 to 11290; no other user) | about 4,980 | file 8,796 to 14,341 in two weeks (14,823 now) |
| Oracle (`FrontEnd/tests/MappingOracle.h`), 87 switchable rules | 5,945 | 2,327 to 5,945 over 107 commits |

They serve a small population: the lines the passes touch are a few hundred per corpus image (A7-25 counted 119 to 321
copies at a call or a return, A7-22 30 to 80 numbers at a join) out of 13,716 and 38,931 lines with an address.

Examples from the ledger (`REVIEW_LEDGER.md`):

- **Effect taken for placement (A7-19, A7-11, A7-15; P2).** `xStack_18 = param_1;` or `cRam0604e898 = 0;` is a copy
  the engine put at the end of a block, at a call or at a return. The token carried the branch, the `jsr` or the
  `rts`; the store that wrote the variable was on no line. `AddFrameStoreTokens` (1,750 lines) finds "the store whose
  value reaches the statement" by walking blocks and comparing values.
- **Lowest address taken for the click (A7-35, A7-23; P2).** `xRam26002204 = 0;` named two instructions, and the
  lowest was `mov.l r0,@r7` storing **3**; a click went there. The store that wrote the 0 was on no line. Nothing in
  the model says which of a line's addresses did the effect, so the lowest wins.
- **Contributor taken for placement (A7-22, A7-26; P2).** `iVar3 = 0;` is a MULTIEQUAL of constants collapsed to a
  COPY that keeps the block's first address, a literal load of a callee's address. The makers (`mov #0,r8` in two
  delay slots) were on no line. `AddJoinConstantTokens` finds them by walking each path back; the oracle's own walk
  for the same question is the most frequent tag in the ledger (`map-const-join`, 62 mentions), most of them false alarms.
- **One flag standing for a role (A7-25, A7-57, A7-53).** A copy shown at an instruction that may alias it shares that
  instruction with the instruction's own statement. `Token::materialised` and five ordering rules in `Ordered` decide
  which comes first; the oracle read the product's own flag to check it (A7-30) and cannot tell a conversion's token
  from the copy's "without the token's op, which the result does not carry" (A7-53, accepted).

Since `c58849ae` the tail has gone on in the same way: A7-340 and A7-341 refined value walks (`AddFrameStoreTokens`,
now at line 8809, and `RetargetInputOperandToStore`), and A7-342 changed the fallback wording that step 3 below also
changes. `Sh2FlowDecisions.h` (120 lines) and its decision-point tests were added and are not in step 4's deletion
list; they are kept.

The counter-example is the delay slot (A7-10): one engine patch (0075, completed by 0079) preserved the slot's
identity where it is known, and the class closed. The proposal applies that method to the rest.

## 2. The data model

Printed spans stay as they are, and so does the panel's cycling through several lines. What changes is that an
address stops being a property of a token and becomes a **link with a role**.

```cpp
enum class LinkRole : uint8_t
{
    Effect,     // performs what the statement shows: the store that writes the displayed bytes, the branch, the call, the return
    Condition,  // sets what a branch tests (the cmp or tst of an if)
    Value,      // computed, loaded or made a value the span shows
    Address,    // computed the pointer an access goes through
    Placement,  // where the engine put a copy; the instruction does nothing of this statement
};
struct InsnLink                 // 8 bytes; plain data, C++14
{
    uint32_t addr = 0;          // canonical
    LinkRole role = LinkRole::Value;
    uint8_t flags = 0;          // kLinkCandidate, kLinkBadData, kLinkPartial (writes some of the displayed bytes), kLinkHalt (where flow stopped; not an effect)
    uint16_t span = 0xFFFF;     // token index in its line; 0xFFFF = the statement as a whole
};
enum class StatementKind : uint8_t { None, Store, Assign, Branch, Call, Return, Halt, Label };
struct Statement
{
    uint32_t firstLine = 0, lineCount = 0;   // a statement can wrap
    uint32_t linkBegin = 0, linkCount = 0;   // into DecompileResult::links, grouped by line
    StatementKind kind = StatementKind::None;
};
```

`DecompileResult` gains `std::vector<InsnLink> links` and `std::vector<Statement> statements`; `Line` gains
`int32_t statement` (-1 for a declaration or a brace). A line can hold more than one statement: the printer emits
`for (init; cond; step)` on one line (pinned at `Sh2DecompilerTests.cpp:7631`). Such a line's `statement` is its
first; `Statement` also carries `uint16_t spanBegin, spanEnd` (token range on its first line, `0xFFFF` = the whole
line), so the three parts of a `for` header are three statements with their own links, and `ForwardLookup` of the line
offers them in text order. Flat vectors and indices only: no `std::optional`, no
`variant`, nothing MSVC's C++14 lacks. `Token::addr`, `Token::materialised`, `Token::haltsFlow`, `Line::firstAddr`,
`Line::noOrigin` and the empty tokens that carry extra addresses become derived values during the migration and are
deleted at its end. A link is **proven** unless `kLinkCandidate` is set; a candidate is one of several plausible
origins that the evidence cannot separate.

Three pure functions in `Sh2Decompiler.h` replace `LineInstructions`, `LineOriginText` and `InstructionLineIndex`. The
panel and the oracle call the same ones, so the oracle checks what the user gets.

| Function | Result |
|---|---|
| `ForwardLookup(result, line)` | The targets in order, and why the first is first: `Effect`, `Candidates`, `ValueOnly`, `BadData`, `Unknown` |
| `ReverseLookup(result, insn)` | Every line with a link to the instruction, each with its role; or `NearestMapped` with the neighbour used; or `Outside` |
| `LinkCursor` | The cycling state the panel keeps today in `mLastInsn`, `mLastInsnIndex`: next line of the same answer |

What the panel shows:

| Case | Click on a line | Text |
|---|---|---|
| One proven effect | goes to it | "Stored by 0600C14A. Value from 0600C140, 0600C144." |
| Several effects (a word built by two stores, a store on two paths) | the first in text order; for partial stores of one word, address order; asking again cycles | "1 of 2 instructions that write this" |
| No effect, values only (a register assignment) | the instruction that computed the assigned value, else the first value link | "Computed at ..." |
| Candidates only | the first, marked | "One of 3 possible origins; the decompiler merged them" |
| Nothing | none | "No instruction is named for this line" (today's A7-52 text) |
| Bad data | the word | today's A7-12 text |

The way back orders lines by role: `Effect`, `Condition`, `Value`, then `Placement`; text order within a role.
(`Address` is folded into `Value` in the first version: no ledger class turns on telling them apart.) `Placement` is
never a click target. It orders the way back, and its text is shown only as a hover line on a line that also has a
proven `Effect` ("also shown here because the store at 0600C152 may alias it"); on its own it would put an
unexplained address in the cycle, which is A7-25's and A7-57's complaint. A line that only shows a copy placed at the instruction therefore comes after the instruction's own statement
by construction, which is what `materialised` and `Ordered`'s five loops approximate. When no line links to an
instruction in the body, the nearest linked instruction of the same run is used as now, but the note reads "no line
names 0x...; showing the nearest mapped line (0x...'s)". It no longer says "was folded into the expression of another
instruction", which the product does not know. User notes and the halt hint (`DecompilerPanel.cpp` 1676, 1864, 1906)
use `ReverseLookup` without `Placement` links.

## 3. Evidence

The source is the final p-code and the flow record of the **last** flow generation (patch 0096's hook). The hook
fires on every `followFlow`, so links are built only after the final one, and the model asserts that the flow
generation it reads is the function's. One fact separates the two kinds of op: `op->getTime() < fd.flowOps` means the
flow made the op (rewritten in place or not, since `opSetOpcode` keeps the `SeqNum`); a later time means the engine
made it. The definition is the engine's own `Funcdata::flowOps` (`funcdata.hh:159`, set at `funcdata_op.cc:1204`), not
the host's separate `opsEnd` (`Sh2Decompiler.cpp` around 6253 at `c58849ae`); step 1 asserts once that the two agree,
including ops made between `generateOps` and the hook such as the artificial halt RETURN (`flow.cc:621`).

**Artificial halts first.** A RETURN is not always a return. `FlowInfo::artificialHalt` (`flow.cc:613`) makes a
`CPUI_RETURN` marked with a halt type (`PcodeOp::getHaltType`, `op.hh:183`): `halt`, `badinstruction`,
`unimplemented`, `noreturn` or `missing`. `checkForFlowModification` (`flow.cc:689`) puts a `noreturn` one at a
non-returning **call's** address, and `PrintC::opReturn` prints the printed kinds as `halt()`, `halt_baddata()`,
`halt_unimplemented()` or `halt_missing()`, not as a return (the A7-6 fixture prints `halt_baddata()` at an odd
location). So a RETURN is classified by its halt type **before** the op-code rule below:

| Halt type | Statement | Link |
|---|---|---|
| none | `Return` | `Effect` at the `rts`/`rte` (by op-code, or through the Clone record for a cloned return) |
| `badinstruction` | `Halt` | `kLinkBadData` at the word, as today's A7-12 text; never `Effect` |
| `halt`, `unimplemented`, `missing` | `Halt` | `kLinkHalt` at the address where flow stopped; never `Effect`, and never counted as a return |
| `noreturn` | not printed (`notPrinted`); the call's own statement keeps its `Effect` at the `jsr`/`bsr` | none for the halt op |

Step 1's fixtures include a non-returning call, a bad-data halt (A7-6), a missing and an unimplemented halt, with
ordinary and cloned returns as controls; the census reports halt statements by type.

**Time alone is wrong for cloned ops, and op-code beats time for effects.** `CloneBlockOps::buildOpClone`
(`funcdata_block.cc:1008`) gives a clone a new time at the same address, and `ActionReturnSplit` runs it on shared
epilogues, which SH-2 code has everywhere; `CloneBlockOps::cloneExpression` (from `RuleConditionalMove`,
`ruleaction.cc` around 9716) and `nodeSplit` are other clone sources. A cloned STORE, CALL, RETURN or folded-store
COPY read by time as engine-made would be walked back to a different flow store: a wrong primary, P2. So a STORE,
CALL, BRANCH or non-halt RETURN op is an `Effect` at `InstructionOf(op)` **whatever its time**, and a cloned COPY that was a
folded store needs the Clone record of the engine patch below (which is why that record is in the first version).

| Printed op | Link | Recoverable today? |
|---|---|---|
| Flow op: STORE, hardware write, branch, call, return | `Effect` at `InstructionOf(op)` | Yes, exact. `SeqNum` address plus the slot flag (0075, 0079) |
| Flow STORE folded to a COPY that survives (`RuleStoreVarnode`) | `Effect`, same op; `AccessRecord.time` (0017) says it was a store, with address and width | Yes, exact |
| Flow op with a register output, printed as `v = expr` | `Effect` of an `Assign` at its instruction | Yes |
| Flow op printed inside an expression | `Value` on its span; `Address` when it feeds only a pointer; `Condition` for the boolean of a branch | Yes |
| Engine-made op inside an expression (a cast, a PTRADD) | `Value`, candidate: its address is the op the rule was rewriting, usually the same instruction, not always (A7-175's narrowing made at a `bf`) | Hint only |
| Engine-made op writing a memory variable (guard copy at a call, trim copy at a block's end, collapsed INDIRECT) | `Placement` at its own address; `Effect` at the flow stores it is connected to **by definition and use**, not by value | Yes where the store's COPY survives; see below |
| Engine-made COPY of a constant (collapsed MULTIEQUAL) | `Placement`; makers unknown | No: the `mov #imm` COPYs died in constant propagation |
| Survivor of two identical ops (`cseElimination`) | its own; the twin's address is gone | No |
| Op of a cloned block (`cloneOp`, `buildOpClone`, `cloneExpression`: new time, same address) | STORE, CALL, BRANCH, RETURN: `Effect` by op-code, whatever the time. A cloned folded-store COPY: `Effect` through the Clone record | Slot flag yes (0075, `funcdata_op.cc:1056`, `funcdata_block.cc:1026`); identity by op-code, or by the Clone record |
| Two flow stores merged into one (`RuleDoubleStore`, `double.cc` around 3533) | both instructions, `Effect` with `kLinkPartial` (the new STORE has the later one's address) | Needs the merged pair recorded; until then the later is proven and the earlier is a candidate |
| One store split in two (`prefersplit.cc:411`, `subflow.cc:2944`) | each keeps its instruction | Yes |

**Stores.** For an engine-made statement on a memory variable, walk the SSA from its output: backwards to flow
stores (the statement shows again a value a store wrote: A7-11, A7-15, A7-25), and forwards to a flow store (the
statement is the store's value put on a path ahead of it: A7-19, A7-35, where the shared store's COPY survives as a
copy inside one variable). Bounded: at most 64 ops and 8 links, more is `Unknown`.

**What makes a link proven.** Def-use reachability alone is not proof that a store writes the displayed value. A
walk link is **proven** only if every edge on its path preserves the displayed bytes, from these:

| Edge | Allowed for a proven link |
|---|---|
| COPY in the same storage, same size | yes |
| INDIRECT whose effect op is a call or guard that does not write these bytes (input 0 passes through) | yes |
| PIECE / SUBPIECE | yes, with the byte range narrowed to the displayed bytes; a store that writes only some of them is `kLinkPartial` |
| MULTIEQUAL, walking **backwards** | yes, and the result is one `Effect` per input path (several effects, section 2); a path whose input is not a flow store makes the whole answer candidates |
| MULTIEQUAL, walking **forwards** (a store after a join) | no: the store may write another arm's value. The link is `kLinkCandidate` |
| INDIRECT whose effect op may write these bytes, any arithmetic, CAST, INT_ZEXT/SEXT, a load | no: the walk stops; what it found so far is `kLinkCandidate` |

So a statement on one arm of an `if` is never given the other arm's store as a proven effect: the only path between
them crosses a join forwards. Where the proof is incomplete the panel says "candidates", which the policy accepts;
it never names a proven primary it cannot prove. A link proven this way names the store by identity, so "the store of
another value" (A7-23, A7-35, A7-41, A7-44, A7-84) cannot be chosen. Where a PIECE joins two stores (`CONCAT11(in_r2,in_r1)`, A7-77), both
are `Effect` links with `kLinkPartial`.

**What is lost, and the optional engine patch.** When a folded store's COPY is destroyed (its readers were given its
input and it went as dead), only the `AccessRecord` remains: instruction, address, width, no successor. Then the
candidates are the records whose bytes overlap the variable's, marked `kLinkCandidate`, or nothing. A small patch
would keep identity at the points it is destroyed, in the manner of 0075. It would record, in a vector on `Funcdata`
cleared with the function like the access fates:

1. `OriginRecord { survivor time, origin time, pc, slot, kind }` appended by `cloneOp` and `buildOpClone` (Clone).
   **In the first version**: it is a correctness matter (the cloned effects above), not a convenience;
2. the same record from `cseElimination` (Twin), from `RuleDoubleStore` (Merged pair), and, if step 2's census asks
   for it, from `RulePropagateCopy` when it forwards a folded store's value or a constant (Forwarded). **Behind a
   threshold**: built only if more than 2% of today's store-named lines become candidates or unknown in step 2.

The first draft also proposed a `placed` flag set at "eight sites" that address a new op to a block boundary. There
are at least ten more (`condexe.cc` 203, 700; `coreaction.cc` 732, 5344, 5409; `funcdata_op.cc` 1598;
`funcdata_varnode.cc` 451, 460, 604, 1634), so a list in the engine would rot. **`placed` is derived on the host
instead**: an engine-made op whose address is its block's first or last instruction. The constant-origin hook in
`RulePropagateCopy`, which sits on the hottest rule, is not built in the first version (section 7, question 1).

The Clone part is about 40 engine lines and the threshold part about 80, appends only where those rules fire. Without
the threshold part the affected lines say "candidates" or "unknown", which the policy accepts.

**Delay slots, frame stores, conversions.** Slot identity is already preserved (0075, 0079) and `InstructionOf` stays
the only reader of it. Frame stores are the store rule above in the stack space, with 0068's and 0076's records
unchanged. A conversion the engine made or hoisted gets `Placement` at its address and `Value` links from the op that
defines its input, which is what `RetargetHoistedConversions` and `RetargetTwinExtensions` compute by walking the
flow.

## 4. Migration

Each step is a branch that passes `ctest`, the 20,000-seed matrix, the fault matrix and the corpus with the
**existing** oracle and pins, until step 6 replaces the oracle. The comparison tool throughout is the token dump
(`DumpMappingTokens`, `SE_GAME_TOKEN_DUMP`, `tools/a7_round2/corpus.sh`) extended with a `K` record per link, and a
script that diffs old answers (first address, address set, `Ordered`) against new ones per line and per body
instruction, over the four corpus dumps and the generator's pins.

| Step | Change | Replaces | Size (lines) | Accept when |
|---|---|---|---|---|
| 0 | Baseline: dumps of both images alone and lent, the pin list, the diff script | nothing | +200 scripts | dumps reproduce |
| 1 | Shadow model: `links` and `statements` filled from identity only (rows 1 to 5 of section 3's table, the op-code rule for effects, the Clone record); old fields untouched; the census below | nothing | +400 product, +40 engine, +150 test | the census's go thresholds; every flow store, branch, call and return line has one proven `Effect`; each disagreement with `firstAddr` is listed by class |
| 2 | Store walk for engine-made statements; `Placement` | shadow of `AddFrameStoreTokens`, `MarkMaterialisedReads` | +350 | on the store pins the new `Effect` set holds the old click target or the difference is an old defect; the "old named a store, new says unknown or candidates" lines are counted against the 2% threshold |
| 2b | (if the count passes the threshold) the Twin, Merged and Forwarded records | | +80 engine, patch record | the count falls below the threshold; vendored lint and reproduction job pass |
| 3 | `ForwardLookup`, `ReverseLookup`, `LinkCursor`; the panel, hover text and menu use them; the fallback label. **The old fields stay authoritative**: the old oracle still judges them, and the new functions are added beside them, not derived into them | `LineInstructions`, `LineOriginText`, `InstructionLineIndex` | +350, -200 | panel tests; new contract checks (section 5) run beside the old oracle |
| 4a | Delete (i) `MarkMaterialisedReads` and the A7-145 block; (ii) `RetargetTwinExtensions`, `RetargetHoistedConversions`, `RetargetInputOperandToStore`, `MoveDrawnTokens`; (iii) `AddJoinConstantTokens`, `MovtNumber`; one commit each, diffing dumps before and after | the passes | -345, -620, -330 | the pins of each pass's rows hold under the contract; pins are rewritten only under section 7's rule (question 7) and listed |
| 5 | Oracle: remove the value reconstruction and the rules tuned to it; keep structure, coverage, the contract, the narrowed value check (8) and the execution witness (9); rebase the mutations, including the two-arm join mutation, which must be rejected. **Before step 4b**, so a deleted walker is never the only check of its class | 87 rules | 5,945 to about 2,400, with a ceiling of 2,800 | every mutation kind is reported; SE_A7_RULE_OFF list empty |
| 4b | Delete (iv) `AddFrameStoreTokens` and (v) the value walkers, **split by statement kind** (stores at a call, at a return, at a block's end, frame stores; then each walker once unused), one commit each with dumps diffed | the rest | -1,750, -1,890, in about six commits | as 4a |
| 6 | Remove the old fields; acceptance per the policy (two clean rounds, the overnight run) | `Token::addr` and friends | -150 | policy's done criterion |

Steps 1 and 2 change nothing a user sees and can merge early. Step 3 is the first visible one. A pass is deleted only
in step 4a or 4b, after its shadow has run beside it on the corpus and the pins. Net: about -4,000 product lines and
-3,100 oracle lines. Effort: the first draft said 12 to 16 working days on one track; given the pin tables, plan for
more, and re-estimate after step 1's census.

**Step 1's census**, per corpus image (alone and lent) and per grammar over 20,000 seeds. The thresholds are
provisional degradation budgets, to be adjusted once the first census exists, not derived from data. Zero
unexplained changes on a P2-pinned row takes precedence over every percentage:

| Measure | Threshold |
|---|---|
| Printed ops by flow, engine-made and cloned, crossed with op-code | report only |
| Store statements with a surviving flow op or folded COPY | go at 95% or more. 85 to 95%: go only with a written per-class account of the missing stores and step 2b scheduled, not optional. Below 85%: rework section 3 before step 2 |
| Engine-made statements where the bounded walk finds a store, candidates, or nothing | go if candidates or nothing is 2% or less of the lines that name a store today; 2 to 10% requires step 2b; above 10%, stop and rework |
| Lines on a P2-pinned row where the new `Effect` differs from today's click target | zero unexplained |
| Clone-made effect ops | any non-zero count confirms the Clone record (already in step 1) |
| Lines with more than one statement | report only |
| Walk bound hits (64 ops, 8 links) | under 0.1% |
| Functions over the flow record's cap (20,000 ops, `RecordFlow`'s users) | report only; such a function's links are identity-only and its engine-made statements say "unknown" |
| Build time and memory on the largest functions (area 6's stress inputs) | within 5% of today |

## 5. The oracle contract

Checks, all on the pure functions the panel calls:

1. **Valid.** Every link is even, in the body, and decodes; or carries `kLinkBadData` and is in `badDataAt`.
2. **Effect is an effect.** By decoding the instruction word, independently of the engine: a `Store` statement's
   `Effect` link is an instruction that writes memory, and for a variable at a known address the store's target
   overlaps the variable's bytes. The target is derived from the instruction word wherever it is PC-relative,
   GBR-relative with a known GBR, or absolute through a literal; the engine's access record (`accessFates`,
   `accesses`) is used only for a register-indirect target, so the oracle does not grade the product with the
   product's own evidence; a `Branch`'s is a branch, a
   `Call`'s a call or jump, a `Return`'s an `rts` or `rte`; a `Halt`'s link is `kLinkBadData` or `kLinkHalt` and
   never `Effect`. A delay slot's statement links the slot, not the branch.
3. **Effect first.** `ForwardLookup` of a statement with a proven effect returns it first; `ReverseLookup` of an
   instruction returns the lines it is the effect of before any other.
4. **Reverse is complete.** Every line with a link to an instruction is in its reverse answer, and `LinkCursor`
   reaches each in `lines.size()` steps. Every address `ForwardLookup` offers leads back to the line.
5. **Fallback is labelled.** An answer without a link is `NearestMapped` or `Outside`, never `Exact`, and the note
   text says so.
6. **Coverage**, as today: every ledger access and every call of the final p-code is linked from some line;
   a statement with no link says so.
7. **Panel round trip**, headless through `DecompilerPanelTests`' harness on the fixtures and the pinned modules: a
   click, **Show in Pseudocode** back, repeated until the cycle closes, visits each line once and shows the texts of
   checks 3 and 5. The pure checks 1 to 6 run on every generated function; only this one draws, so the Windows Debug
   job is not loaded per instruction.

Rules that become unnecessary: `firstAddrWrong`, the address-set round trip (`roundTrip`, `AuditMapping`), the
`materialised` order in `backWrong`, and every rule that recomputes a value: `storeValueOther`, `numberStoreOther`,
`constAtJoin`, `copyAt*`, `copyMergeAtBranch`, `storeAfterUse`, `storeDead`, `storeTwin`, `conversionAtBranch`,
`numberAtSlotBranch`, with `WrittenValue`, `RegisterBefore`, `MemoryBefore`, `SlotBefore`, `RegisterMakers`,
`PossibleBits`, `EdgeImpossible`, `TBefore` (lines 430 to 2090).

8. **One value check, narrowed: another store of the same variable.** The gap of checks 1 to 7 is the P2 class of
   A7-23 and A7-35. `storeValueOther` stays, limited to straight-line code within one block, where the value is
   decidable from the words alone and the check has had no false alarms; its join walks (the source of most of the 62
   `map-const-join` false alarms) are removed.
9. **Execution witness, across joins** (generated functions). Check 8 does not reach a store on another arm of a
   join, and pins protect examples, not fresh shapes. The generator already runs each function on the emulator and
   compiles and runs the printed C on the same inputs (the printed-C oracle). The witness instruments each printed
   statement on a memory variable to log (line, address, value) and records the emulator's store events (instruction,
   address, value). For every statement with a **proven** `Effect` link, on every run where the statement executes,
   the linked instruction must have executed in that run and written the logged bytes. It is bounded (the runs the
   generator makes anyway), independent of the engine's records and of the product's walk, and it rejects the
   two-arm mutation below. Candidate links are not checked by it.

The oracle keeps a size ceiling (2,800 lines after step 5) so that it does not grow back into a second
reconstruction; a new rule that would cross it needs a design note.

Mutations stay meaningful because each check has one that only it reports: a link moved into the middle of an
instruction or out of the body (1); a store's effect moved to the next non-store, to a store of another variable, or
a slot's to its branch, or a halt's link promoted to `Effect` (2); the effect swapped with a value link, or a `Placement` promoted (3); one line dropped
from a reverse answer (4); a fallback relabelled `Exact` (5); today's `tokdrop`, `tokcall`, `toksilent` (6); a
store's effect moved to **another store of the same variable** in straight-line code (8); and a statement's proven
effect moved to the store on **another arm of a join** writing a different value to the same bytes, with coverage
and reverse links kept (9), which checks 1 to 8 all pass. Step 5's acceptance requires that last mutation to be
rejected on the generated grammars.

Classification of the 261 area 7 rows (264 now; A7-340 to A7-342 are value-walk refinements and a wording fix, in the first class) (`tools/a7_linkmodel/classify_a7.py`, a key-word count, not a new triage):

| Class (the conflation) | Rows | P2 | Product | Oracle | Not analysed |
|---|---|---|---|---|---|
| Effect taken for placement (store on no line, wrong store named, copy at a branch or call) | 103 | 31 | 67 | 26 | 10 |
| Contributor taken for placement (number at a join, register merge, hoisted conversion) | 82 | 9 | 25 | 52 | 5 |
| Click target and order (lowest address, way back, hover) | 56 | 4 | 30 | 23 | 3 |
| Other (case labels, bad data, coverage, features, routed) | 20 | 1 | 13 | 3 | 4 |

| Outcome under the link model | Which rows |
|---|---|
| **Fixed by construction** | Effect and click rows on the product side (about 97): the store is named by identity, the default is the effect, the order is by role. Includes every "store of another value" P2 |
| **The rule goes** | The 104 oracle-side rows: false alarms and gaps of the value walks (62 `map-const-join` mentions, 24 `map-store-value`), which are no longer made |
| **Accepted P3 by class** | Contributor rows on the product side (about 25) where makers died in propagation: candidates or unknown ("missing links for absorbed intermediates", "display order among equally valid candidates"); the `map-store-dead` rows ("a store that stays visible though a later store overwrites it"); A7-7, A7-16 as now |
| **Stay real bugs, checked** | Slot identity (A7-10, A7-37: any creator of ops that does not pass the flag on); case labels (A7-24, A7-27, A7-29); bad data and odd addresses (A7-6, A7-12); coverage; overlapping decodes (A7-273's class); the unrecovered jump table drawn at a `jmp` (A7-322) |
| **Not mapping** | A store printed after the call it precedes (A7-32, A7-59, A7-74, A7-323): with the store named by identity, a wrong order is the text's, area 3 or 5 |

No area 7 row is open at P2 today; the open ones (about 25) are P3 and fall in the last three outcomes.

## 6. Risks, cost and questions

Risks:

- **Coverage of identity is unmeasured.** How many store statements keep a surviving store op is not known until
  step 1's census; the design assumes most, from the ledger's shapes. If many lines become "unknown" that name a
  store today, step 2b is needed, or the old pass stays longer.
- **A register merged under a memory variable's name** (A7-21, A7-23, A7-44, A7-52) prints like a store and no
  instruction stores it. The walk follows definitions, not names, so it finds no store and the line is an `Assign`
  with value links. The contract's "represented store" must mean a statement whose op writes the variable's storage.
- **Address hints of rule-made ops** stay as lenient `Value` candidates; a wrong hint is a P3 the model does not
  remove.
- **The engine patch** takes the next free patch number when it lands and touches the clone, merge and CSE code. It
  is append-only; its Clone part is in the first version and the rest behind step 2's threshold.
- **Cloned effects** (section 3) would give a wrong primary if read by time; the op-code rule and the Clone record
  close it, and step 1's census counts them.
- **The flow record's cap.** `RecordFlow`'s users give up above 20,000 ops; such a function gets identity-only links
  and "unknown" for engine-made statements, which the census reports.
- **Inlining.** `flow.cc` 1130 and 1158 clone ops with the callee's sequence numbers. The product is not known to
  enable inlining; step 1 asserts it is off, and if it is ever turned on, an inlined op is linked as `Value` only.
- **Two models coexist** from step 1 to step 6, and the generator's pin tables are written in the old terms.
- **Restarts and scratch copies.** Links are built from the last flow generation only (0096), asserted against the
  function's generation; a jump-table scratch function has no hook and must contribute nothing.

Cost at run time: links are built in one pass over the printed tokens plus a bounded walk per engine-made statement,
replacing five passes that each walk the flow's blocks per printed op. The reverse index is built once per result as
now. No work is per instruction except the oracle's check 1, already there. Memory: 8 bytes per link, about the size
of the empty tokens it replaces.

Questions for the reviewer (answered in section 7):

1. Is "unknown" or "candidates" acceptable for a number at a join (`iVar3 = 0;`), where the product today names the
   `mov #imm` makers on the paths (A7-22)? If not, the constant-origin part of the engine patch is required.
2. Should the engine patch (`placed` flag, clone and twin origins) be in the first version, or only if step 2's count
   of "store known today, unknown tomorrow" passes a threshold, and what threshold?
3. Should one value check remain for "another store of the same variable", limited to straight-line code where it
   has had no false alarms, or are the pins enough? A third option is a witness from executing the generated program.
4. Is `Placement` worth showing ("shown here because the store at 0600C152 may alias it"), or should such links only
   order the way back?
5. Are `Address` and `Condition` wanted in the first version, or `Effect`, `Value` and `Placement` only?
6. For several effects on one statement, is text order the right first answer, or the last store executed?
7. May step 4 rewrite pins that assert a display order or a maker the policy now accepts by class, or must every old
   pin hold unchanged?

## 7. Review recommendations (proposed decisions)

An independent review of this document (read-only, against `c66e781e` and the ledger; nothing built) recommended
**approve with changes**. The model is sound: roles per link replace the one overloaded address that `Ordered`'s five
loops and `Token::materialised` approximate, and naming the store by definition and use removes the mechanism of
A7-23, A7-35, A7-41, A7-44 and A7-84. It stops the tail only where identity survives, which step 1's census measures,
and it named two things that would restart the tail elsewhere: effects read by time alone (cloned ops) and an oracle
that grades the product with the product's evidence. The edits above address both. Its answers to section 6's
questions, recorded here as **proposed decisions pending approval**:

1. **Joins.** Accept candidates (`Placement` plus "merged constant"); A7-22 was P2 only because a wrong instruction was
   named as proven. Do not build the constant-origin hook in `RulePropagateCopy` in the first version. Step 1's census
   counts the lines that lose a maker they have today (A7-22 counted 30 to 80 per image).
2. **The engine patch.** Split it. The Clone record goes in the first version (correctness). `placed` is derived on
   the host, not flagged at a list of sites. Twin, Merged and Forwarded records are built only if more than 2% of
   today's store-named lines become candidates or unknown in step 2.
3. **A value check.** Keep `storeValueOther` for straight-line code in one block (section 5, check 8); drop the join
   walks. The first review filed an execution witness as a follow-up; the second review (below) showed it is
   required: it is check 9, and it is in place before step 4b deletes the old oracle's walks.
4. **`Placement`.** Use it to order the way back; show it only as a hover line on a line that also has a proven
   `Effect`; never a click target.
5. **Roles in the first version.** `Effect`, `Condition`, `Value`, `Placement`. `Condition` is needed so a click on
   `if (...)` can tell the `cmp` from the `bt`. `Address` is folded into `Value`.
6. **Several effects.** Text order. For partial stores of one word, address order. "Last store executed" is
   path-dependent and would bring back a walk; text order among stores on separate paths is an accepted class.
7. **Old pins.** May be rewritten only when they assert an order or a maker in an accepted class; each rewrite is
   listed with its row id and both answers. No pin from a P2 row may be weakened: its click target must still be in
   the new `Effect` set.

The 12 to 16 day estimate is likely optimistic given the pin tables; re-estimate after step 1. Nothing in area 7's
product code changes until these decisions are approved.

**Second review (PR #73).** Two changes were required and are made above:

- **Artificial halts.** A halt is a `CPUI_RETURN` (`FlowInfo::artificialHalt`), including one at a non-returning
  call's address, so the op-code rule alone would claim a return effect that does not exist. Section 3 now classifies
  a RETURN by its halt type first (`Halt` statements, `kLinkBadData` or `kLinkHalt`, never `Effect`), check 2 says so,
  and step 1 has non-returning-call, bad-data, missing and unimplemented fixtures with ordinary and cloned returns as
  controls.
- **A wrong store across a join.** With check 8 limited to straight-line code, a primary moved to the other arm's
  store passed every check. Section 3 now defines the edges a proven link may cross (a store after a join, walking
  forwards, is only a candidate), and section 5 adds the execution witness (check 9) with the two-arm mutation that
  it must reject before the old walks are deleted.

It also asked that the 85 to 95% survival band be defined (step 1's census now does) and noted that the plan can
reduce the heuristic tail but does not yet show that it ends it; the census and check 9's results on fresh seeds are
the evidence that will settle that.
