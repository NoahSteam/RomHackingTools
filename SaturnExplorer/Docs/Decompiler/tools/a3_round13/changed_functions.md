| # | Image | Function | Kinds of change | What the new text does | Against the code | Verdict |
|---|---|---|---|---|---|---|
| 1 | 007 | 06022A98 | decl, sig, stmt | spills of r4/r5 kept as locals (`iStack_30 = param_1`), `param_1` uint1 to int4, the call result kept in `xStack_28` | prologue spills at r14+0/+4, the byte read of the spilled word at r14+3 is `(xunknown1)` of the spill; same calls, same order | right, noisier |
| 2 | 007 | 060226CC | decl, sig, stmt | gains `param_4` (the spilled r7), `param_2 & 0xffff` explicit | r7 is stored at r14+12 and never read: an input the code takes, as an unread spill | right, more precise |
| 3 | 007 | 06022304 | decl, sig, stmt | `xStack_20._3_1_ = (xunknown1)param_1` before the full spill `xStack_20 = param_1` | the code spills r4 first and reads byte 3 (the low byte, big-endian) of the spill; the text reads the same byte | right, harder to read |
| 4 | 007 | 06021B10 | decl, stmt | `return xStack_28` for `return xVar1`, `pxStack_2c = param_1` | `mov.l r0,@(4,r14)` after the call, returned from the slot | right, noisier |
| 5 | 007 | 06021C20 | sig, stmt | lent to withheld ("a return not to its caller"); `xStack_24 = 0xfffffffd` kept, `return xStack_24` | the local at r14+4 holds -3 and is returned from the slot; the hand of `&aiStack_18` to a lent writer in a loop is the cause | right, precision (A3-111) |
| 6 | 007 | 06024720 | decl, stmt | `pxStack_10 = param_1` gone | `mov.l r4,@-r15` is a push whose pop restores r4 | right |
| 7 | 007 | 060247B0 | decl, sig, stmt | `param_1` gone, `xStack_10 = param_1` gone | r4 is only pushed and popped | right |
| 8 | 007 | 0602FD46 | array, decl, stmt | `_axStackY_30 = (int4)(char)(uVar2 + 1)` and the array gone | the word is the sixth stack argument, which the call already prints | right (A3-99) |
| 9 | 007 | 06023D52 | decl, stmt | four locals and spills gone, registers kept | the callee is an indirect call through RAM; no frame address is handed (r2, r4-r7 hold values) | right, cleaner |
| 10 | 007 | 06023E68 | decl, stmt | the same | the same | right, cleaner |
| 11 | 007 | 06006D14 | array, decl, notes, stmt | `func_0x06019a48(2,0xc,5,0x604e8a0,0)`: the pushed 0 is the fifth argument; `axStackY_90 [128]`; slot note | `mov.l r0,@-r15` before the `jsr` | right (the note names the saved-register pops, A3-109) |
| 12 | 007 | 06027420 | array, decl, notes, stmt | about 92 `xStack_8 = <return address>` lines gone; one call gains `0x602a566` as the fifth argument | the pushes are words the callees read | right |
| 13 | 007 | 06008378 | sig, notes | 8 phantom parameters (param_3 to param_10), 5 r14 notes | the function reads r4 and r5 only | text right; header wrong (A3-108) |
| 14 | 007 | 06008836 | sig, notes | 7 phantom parameters, a slot note | reads r4, r5 | text right; header wrong (A3-108) |
| 15 | 007 | 06021BA8 | sig, stmt | withheld reason changed, `[8]` array, slot note | calls 06021C20 | right (A3-109, A3-111: the cascade) |
| 16 | 007 | 0602C4AC | sig, notes | 5 phantom parameters, 2 notes | | text right; header wrong |
| 17 | 007 | 0601EBB6 | sig, notes | 7 phantom parameters | | text right; header wrong |
| 18 | 007 | 06023500 | decl, notes, stmt | the result of an indirect call kept in `iStack_8` | `mov.l r0,@r14` after the call, reloaded | right |
| 19 | 007 | 0601DFA8 | decl, notes, stmt | `&uStack_18` printed as the fifth argument, `pxStack_1c` gone | the pointer is pushed before the `jsr` | right |
| 20 | 007 | 06021A58 | decl, notes, stmt | `pxStack_18 = param_1`, a stack-argument note | the second call's words beyond the first unwritten one are dropped by the engine; the slot at +16 holds `&xStack_10` | right |
| 21 | 007 | 060278C4 | decl, stmt | three loads kept as locals | three `mov.l r?,@(n,r14)` stores | right |
| 22 | 007 | 06008518 | sig, decl, notes, stmt | `iVar1 = param_11` before the first call, used at the end | `mov.l @(0x1c,r14),r12` in the delay slot of the first `jsr`, stored at the end; the base read the stack word after the calls (wrong order) | right, and the base was not |
| 23 | 007 | 06021AB8 | decl, stmt | partial store `xStack_18._3_1_` | as 3 | right, harder to read |
| 24 | 007 | 06022118 | decl, sig, stmt | as 23 | as 23 | right, harder to read |
| 25 | 007 | 0601F6FC | sig, notes | 7 phantom parameters | | text right; header wrong |
| 26 | 007 | 06030ED6 | sig, notes | 8 phantom parameters | | text right; header wrong |
| 27 | 007 | 0601E4C0 | sig, notes | 8 phantom parameters | | text right; header wrong |
| 28 | 007 | 06021630 | decl, stmt (lent to lent) | `xStack_24 = param_1` and `~xStack_24._2_2_` for `~(uint2)param_1` | the spill of r4 and the low half read | right, noisier |
| 29 | 007 | 0600D308 | array | `acStack_28 [8]` to `[40]` | the 8 locals plus 32 bytes of saved registers (the indexed store's guard covers them: A3-102) | right, misleading size |
| 30 | 008 | 06022208 | array | `aiStack_14 [4]` to `[10]` | 8 local bytes plus 12 of saved registers | right, misleading size |
| 31 | 008 | 06060B0C | decl, stmt (lent to lent) | as 28 | as 28 | right, noisier |
| 32 | 008 | 060610FC | sig, stmt | lent to withheld; as 5 | as 5 | right, precision |
| 33 | 008 | 060614D8 | decl, notes, stmt | the call result and the spill of r4 kept in locals | as 4 | right, noisier |
| 34 | 008 | 0605A418 | decl, stmt | three spills of parameters, three stores move up | independent stores of different locals | right, noisier |
| 35 | 008 | 06046640 | array, decl, notes, stmt | `func_0x0604ae60(...,xVar16)` and `func_0x06047dd0(...,iVar13,iVar8)` gain stack arguments | `mov.l r1,@-r15` (the 0xf) before the `bra` to the call | right, more precise |
| 36 | 008 | 06061AA4 | decl, sig, stmt | as 2 | as 2 | right |
| 37 | 008 | 0600FA2E | sig, notes | 7 phantom parameters | | text right; header wrong |
