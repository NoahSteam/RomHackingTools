# The link model: pseudocode to SH-2 instructions (area 7 redesign)

Status: **design, for external review. No product code is changed by this document.** It follows the policy in
`REVIEW_PROGRAM.md`, "Area 7: contract and acceptance": seed-specific P3 fixes are stopped, and a design is approved
before any rewrite. Line numbers and counts are those of master `c58849ae`.

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
| Product: value walkers and mapping passes (`Sh2Decompiler.cpp` 6313 to 11290; no other user) | about 4,980 | file 8,796 to 14,341 in two weeks |
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
    uint8_t flags = 0;          // kLinkCandidate, kLinkBadData, kLinkPartial (writes some of the displayed bytes)
    uint16_t span = 0xFFFF;     // token index in its line; 0xFFFF = the statement as a whole
};
enum class StatementKind : uint8_t { None, Store, Assign, Branch, Call, Return, Label };
struct Statement
{
    uint32_t firstLine = 0, lineCount = 0;   // a statement can wrap
    uint32_t linkBegin = 0, linkCount = 0;   // into DecompileResult::links, grouped by line
    StatementKind kind = StatementKind::None;
};
```

`DecompileResult` gains `std::vector<InsnLink> links` and `std::vector<Statement> statements`; `Line` gains
`int32_t statement` (-1 for a declaration or a brace). Flat vectors and indices only: no `std::optional`, no
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
| Several effects (a word built by two stores, a store on two paths) | the first in text order; asking again cycles | "1 of 2 instructions that write this" |
| No effect, values only (a register assignment) | the instruction that computed the assigned value, else the first value link | "Computed at ..." |
| Candidates only | the first, marked | "One of 3 possible origins; the decompiler merged them" |
| Nothing | none | "No instruction is named for this line" (today's A7-52 text) |
| Bad data | the word | today's A7-12 text |

The way back orders lines by role: `Effect`, `Condition`, `Value`, `Address`, then `Placement`; text order within a
role. A line that only shows a copy placed at the instruction therefore comes after the instruction's own statement
by construction, which is what `materialised` and `Ordered`'s five loops approximate. When no line links to an
instruction in the body, the nearest linked instruction of the same run is used as now, but the note reads "no line
names 0x...; showing the nearest mapped line (0x...'s)". It no longer says "was folded into the expression of another
instruction", which the product does not know. User notes and the halt hint (`DecompilerPanel.cpp` 1676, 1864, 1906)
use `ReverseLookup` without `Placement` links.

## 3. Evidence

The source is the final p-code and the flow record of the **last** flow generation (patch 0096's hook). One fact
separates the two kinds of op and the product already has it: `op->getTime() < flow.opsEnd` means the flow made the op
(rewritten in place or not, since `opSetOpcode` keeps the `SeqNum`); a later time means the engine made it.

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
| Op of a cloned block (`cloneOp`: new time, same address) | reads as engine-made though it is an instruction's own | Slot flag yes (0075); identity no |

**Stores.** For an engine-made statement on a memory variable, walk the SSA from its output: backwards through
engine-made ops in the same storage (COPY, MULTIEQUAL, INDIRECT; PIECE and SUBPIECE with the byte range narrowed) to
flow stores (the statement shows again a value a store wrote: A7-11, A7-15, A7-25), and forwards through
non-printing ops to a flow store (the statement is the store's value put on a path ahead of it: A7-19, A7-35, where
the shared store's COPY survives as a copy inside one variable). Bounded: at most 64 ops and 8 links, more is
`Unknown`. A link found this way is proven; it names the store by identity, so "the store of another value" (A7-23,
A7-35, A7-41, A7-44, A7-84) cannot be chosen. Where a PIECE joins two stores (`CONCAT11(in_r2,in_r1)`, A7-77), both
are `Effect` links with `kLinkPartial`.

**What is lost, and the optional engine patch.** When a folded store's COPY is destroyed (its readers were given its
input and it went as dead), only the `AccessRecord` remains: instruction, address, width, no successor. Then the
candidates are the records whose bytes overlap the variable's, marked `kLinkCandidate`, or nothing. A small patch
would keep identity at the points it is destroyed, in the manner of 0075. It would record, in a vector on `Funcdata`
cleared with the function like the access fates:

1. a `PcodeOp` flag `placed`, set at the eight sites that address a new op to a block boundary (`heritage.cc` 3289,
   3365, 3758; `merge.cc` 616, 861, 1328; `funcdata_op.cc` `cseElimination`; `funcdata_block.cc` 179), so the host
   need not infer "first instruction of a join" from the flow graph;
2. `OriginRecord { survivor time, origin time, pc, slot, kind }` appended by `cloneOp` (Clone), `cseElimination`
   (Twin) and, if step 2's census asks for it, by `RulePropagateCopy` when it forwards a folded store's value or a
   constant (Forwarded).

About 120 engine lines, appends only where those rules fire. It is **not** required for the contract: without it the
affected lines say "candidates" or "unknown", which the policy accepts. Whether it is worth its upkeep is a question
for the reviewer (section 6).

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
| 1 | Shadow model: `links` and `statements` filled from identity only (rows 1 to 5 of section 3's table); old fields untouched; census of printed ops by kind | nothing | +400 product, +150 test | every flow store, branch, call and return line has one proven `Effect`; each disagreement with `firstAddr` is listed by class |
| 2 | Store walk for engine-made statements; `Placement` | shadow of `AddFrameStoreTokens`, `MarkMaterialisedReads` | +350 | on the store pins the new `Effect` set holds the old click target or the difference is an old defect; the "old named a store, new says unknown" lines are counted |
| 2b | (if the count says so) the engine patch | | +120 engine, patch record | the count falls; vendored lint and reproduction job pass |
| 3 | `ForwardLookup`, `ReverseLookup`, `LinkCursor`; the panel, hover text and menu use them; the fallback label; old fields derived from links | `LineInstructions`, `LineOriginText`, `InstructionLineIndex` | +350, -200 | panel tests; new contract checks (section 5) run beside the old oracle |
| 4 | Delete, one commit each, diffing dumps before and after: (i) `MarkMaterialisedReads` and the A7-145 block; (ii) `RetargetTwinExtensions`, `RetargetHoistedConversions`, `RetargetInputOperandToStore`, `MoveDrawnTokens`; (iii) `AddJoinConstantTokens`, `MovtNumber`; (iv) `AddFrameStoreTokens`; (v) the value walkers, then unused | the passes | -345, -620, -330, -1,750, -1,890 | the pins of each pass's rows hold under the contract; a pin that asserted an order or a maker now accepted by class is rewritten and listed |
| 5 | Oracle: remove the value reconstruction and the rules tuned to it; keep structure, coverage and the contract; rebase the mutations | 87 rules | 5,945 to about 2,400 | every mutation kind is reported; SE_A7_RULE_OFF list empty |
| 6 | Remove the derived old fields; acceptance per the policy (two clean rounds, the overnight run) | `Token::addr` and friends | -150 | policy's done criterion |

Steps 1 and 2 change nothing a user sees and can merge early. Step 3 is the first visible one. A pass is deleted only
in step 4, after its shadow has run beside it on the corpus and the pins. Net: about -4,000 product lines and -3,500
oracle lines. Effort: about 12 to 16 working days on one track, of which steps 1 to 3 are half.

## 5. The oracle contract

Checks, all on the pure functions the panel calls:

1. **Valid.** Every link is even, in the body, and decodes; or carries `kLinkBadData` and is in `badDataAt`.
2. **Effect is an effect.** By decoding the instruction word, independently of the engine: a `Store` statement's
   `Effect` link is an instruction that writes memory, and for a variable at a known address the engine's access
   record of that instruction (`accessFates`, `accesses`) overlaps the variable's bytes; a `Branch`'s is a branch, a
   `Call`'s a call or jump, a `Return`'s an `rts` or `rte`. A delay slot's statement links the slot, not the branch.
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

Mutations stay meaningful because each check has one that only it reports: a link moved into the middle of an
instruction or out of the body (1); a store's effect moved to the next non-store, to a store of another variable, or
a slot's to its branch (2); the effect swapped with a value link, or a `Placement` promoted (3); one line dropped
from a reverse answer (4); a fallback relabelled `Exact` (5); today's `tokdrop`, `tokcall`, `toksilent` (6). One gap
is deliberate: a store's effect moved to **another store of the same variable** passes check 2. The pins of A7-23,
A7-35, A7-41, A7-44 and A7-84 hold that case by example, and the reviewer is asked below whether one value check
should stay for it.

Classification of the 261 area 7 rows (`tools/a7_linkmodel/classify_a7.py`, a key-word count, not a new triage):

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
- **The engine patch** is the 89th and touches the merge and CSE code. It is kept optional and append-only.
- **Two models coexist** from step 1 to step 6, and the generator's pin tables are written in the old terms.
- **Restarts and scratch copies.** Links are built from the last flow generation only (0096); a jump-table scratch
  function has no hook and must contribute nothing.

Cost at run time: links are built in one pass over the printed tokens plus a bounded walk per engine-made statement,
replacing five passes that each walk the flow's blocks per printed op. The reverse index is built once per result as
now. No work is per instruction except the oracle's check 1, already there. Memory: 8 bytes per link, about the size
of the empty tokens it replaces.

Questions for the reviewer:

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
