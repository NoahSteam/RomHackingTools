import sys,re
C=sys.argv[1]
p='/home/user/RomHackingTools/.claude/worktrees/a3fix144/SaturnExplorer/Docs/Decompiler/Decompiler_REVIEW_LEDGER_PATH'
p='/home/user/RomHackingTools/.claude/worktrees/a3fix144/SaturnExplorer/Docs/Decompiler/REVIEW_LEDGER.md'
s=open(p).read()
FIX='area 3 fixer (A3-144 to A3-153)'
upd={
'A3-144': ("`Sh2DecompilerTests` `TestA3144Fix` (LOOP L2, LOOP L4, LOOP2 nowb: the slots' writes printed, the stale `*(int4 *)*pxStack_20` absent, the frame ledger and `ReferenceOk` empty); the printed-C pins `A3-144 LOOP L2`, `L3`, `L4`, `LOOP2 nowb`, `LOOP2 wb1` (`SaturnExplorerSh2GeneratedTests`: `crashed` or `mismatch` on d88e06e1); the frame124 chain144 stream's `loop` draws (A3-147)",
  "fixed "+C+" (patch 0076: `ActionDeadCode::keepFrameWritesForLoads` holds the frame's writes while a LOAD whose pointer may be a frame address stays a LOAD (`frameLoadStays`: a join, the guard of a STORE not pending or of a call between the pointer and the stack pointer; the guard of a STORE still pending in a straight chain of slots is passed, since 0072 settles it); the hold is an address force marked as one (`Varnode::frameload_force`), let go at each pass of dead code. Traced: 0072's guard on the slot made the reload a join of the loop, so the LOAD through it never folded; it had a LoadGuard, made by `reprocessFreeStores` after the frame was heritaged, which never got its COPYs and kept nothing (`LoadGuard::sunk` now says which guard did). No hold for a save, a write a later version of its bytes is built from, a write overwritten in its block before any read, or a local the scope proved unaliased. A3-130 to A3-132's pins pass; corpora: lending and every ledger count unchanged, the texts that change read in the record)"),
'A3-145': ("`TestA3144Fix` (IDX2 idxdir, IDX L2 chain, IDX3 other L2: the fills printed, evaluated against the run); the printed-C pins `A3-145 IDX2 idxdir rel`, `IDX L2 chain`, `IDX L4 chain`, `IDX3 other L1`, `IDX3 other L2` (`mismatch` or `crashed` on d88e06e1); the chain144 stream's `idx` draws",
  "fixed "+C+" (patch 0076, as A3-144: the LOAD through the slot's reload stays a LOAD (the reload is the indexed STORE's guard, which is no pending STORE's), and the frame's writes are held while it lives; the fills are printed before the indexed store)"),
'A3-146': ("`TestA3144Fix` (`A3-146 BYTE byte`: the reads after the store stay reads, `return (iStack_3c + iStack_38 ^ uStack_34) + iStack_30;`; `A3-146 control`, the same store through r4: folded, `return (param_2 + param_3 ^ param_4) + param_2;`)",
  "accepted: not a wrong value. The engine takes the store through the partly overwritten slot for a write of the frame: the reads of S after it are not folded to the fills (the control, the store through a parameter pointer, folds them); read as C the text returns 0x70 for r4 = 0xC4, as the run. What is wrong is the C (`*xStack_24` of a local typed `xunknown4`: not compilable, A3-137's family) and the driver's `PseudoEval`, which does not model a pointer into the frame built from a frame address (`ORACLE REJECTS` for a right text). The chain144 stream draws the shape for the frame ledger (A3-147)"),
'A3-147': ("`Sh2GeneratedTests`: `GenFrame144Fn` (frame124_<seed>.chain144: `loop` wb/nowb, `idx` rel/dir/other, `byte` c0..c3, 1 to 4 slots) in the printed-C cases and the frame ledger, pin `frame124 chain144 draws`; `HiddenReturnInput` (A3-152)",
  "fixed "+C+" (the generator's frame124 stream draws the loops, indexed stores and partly overwritten slots of A3-144 to A3-146 on a stream of its own; the printed-C differential compares the loop and idx draws (word accesses only: a halfword of a word the engine prints as a local of its own reads the host's other half in frame mode, A2-110's limit), the frame ledger checks all three, the byte ones only there (their text is no C, A3-146). With 0076 reverted the first 1,000 seeds of the stream are findings in the printed-C differential (the record). The frame ledger cannot see A3-144 or A3-145: their read is a LOAD the engine never resolves to a frame offset, recorded `folded into ordinary memory` with no offset, so no record says which frame word it reads, and the writes it lost are 'gone unread' with no later recorded read of their bytes, which the ledger must take for a dead store; to count them it would have to take such a LOAD for a read of the whole frame and every dead frame store before it for a loss)"),
'A3-148': ("the printed-C pin `A3-148` (`frame16.py` seed 21 fixture 41, `pxRam0600f000`: `not-compilable` before)",
  "fixed "+C+" (the harness declares a global the text names by its address (`[a-z]+Ram<8 hex>`) as a static of the type of the first value the text assigns to it (`static __typeof__(axStack_44) pxRam0600f000;`), after the locals; a name only read stays undeclared (`not-compilable`: memory the harness did not write has no value). A3-141 stays open)"),
'A3-149': ("`frame16.py` seeds 41 and 42 (the review's driver runs); none in the suite",
  "accepted: the ledger fails closed (it reports a loss that is none, never hides one). The two reads of the word are made dead by algebra (`x - x`) and destroyed with the op that used them, not forwarded, so the write's record stays 'gone unread' while the code reads it: telling a read whose value cancelled from one the engine lost needs the reads' values traced through the rules that remove them. 2 fixtures in 2,340, no corpus function, no wrong text"),
'A3-150': ("the printed-C pins `A3-150 frame124_4602253.chain`, `frame124_4605918`, `frame124_4511454` (`narrow-c`; `mismatch` before)",
  "fixed "+C+" (the harness: a text that both readings reject and that reads a field of a variable (`FLD`) is read a third time, as C with the fields signed (`(int4)xStack_10._2_2_` sign-extends; a CONCAT stays the unsigned value C makes of it, `CONCAT12(...) >> 8` a logical shift); and in the engine's reading a cast of a CONCAT to an unsigned type is the zero extension at every size (`(uint4)CONCAT11(...)`; the engine prints a sign extension of a piece as a cast to a signed type). The values were right by hand; 3805863 and 4606911 agree or are `narrow-c` too)"),
'A3-151': ("`Sh2GeneratedTests` `TestRound13FixPins`: the pin `A3-151 grammar 2 seed 4503481` (the run's store at 0x0602002F, the swap of r5's start plus 15, is no store through r4; a store through r4 itself is still demanded)",
  "fixed "+C+" (the oracle: `BitwiseOfOthers` also takes an address that is the byte or halfword swap of another start value the run read, stepped after the swap (`swap.b r5,r0 ; ldc r0,gbr ; mov.b r0,@(15,gbr)`); A3-123's check undid the swap only before the step)"),
'A3-152': ("`TestA3144Fix` (V1, V3 and the fuzzer's program 2691: withheld, 'its return is computed from r0 on entry'); the printed-C pin `A3-152 oracle` (`HiddenReturnInput`)",
  "fixed "+C+" (the host's return rule: a store of part of a word that held r0's entry value itself leaves a value computed from r0 there (`ReturnCheck::Store`), which Clobber's rule for a word that may still hold what it held took for r0 left alone; V3 is withheld as V1. The printed-C differential's `void-return-lent` (A2-34 read the rts slot only) now runs each of the first rows again with r0 to r3, r8 to r11, the parameter registers past the signature's and the T flag changed, and a `void` lent text whose r0 changes with one of them is `return-dropped` (r0 left as it came in excused, A3-14); on d88e06e1 V3 is that finding)"),
'A3-153': ("`Sh2GeneratedTests` `TestRound13FixPins`: the pin `A3-153 grammar 2 seed 4700578` (`excluded.call-result-passed.return-dropped`)",
  "fixed "+C+" (triage finished: function 1's r0 is a word it loads (`mov.l @r5,r0`, taken on the path `mov #1,r0 ; tst r0,r0 ; bf`), which the return rule lets a void signature drop (A3-5: memory), and function 0 passes it on untouched to its return: A3-19's accepted shape, as A3-112 accepts a call result used. The generator counts a call's r0 passed on untouched, when every callee is lent `void`, as `excluded.call-result-passed.return-dropped`; no wrong signature)"),
}
lines=s.split('\n')
for i,l in enumerate(lines):
    m=re.match(r'^\| (A3-1(4[4-9]|5[0-3])) \|',l)
    if not m: continue
    rid=m.group(1)
    if rid not in upd: continue
    # split cells on unescaped |
    cells=re.split(r'(?<!\\)\|',l)
    # cells[0]='' ; cells[1..8]; cells[9]=''
    assert len(cells)==10, (rid,len(cells))
    fx,st=upd[rid]
    fx=fx.replace('|','\\|'); st=st.replace('|','\\|')
    if st.startswith('accepted'):
        cells[5]=' '+cells[5].strip()+' Pinned since the fixer: '+fx+' '
    else:
        cells[5]=' '+fx+' '
    cells[6]=' '+st+' '
    cells[8]=' '+FIX+' '
    lines[i]='|'.join(cells)
open(p,'w').write('\n'.join(lines))
print('ok')
