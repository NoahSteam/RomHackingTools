#!/usr/bin/env python3
"""Area 3 round 42: writes fixtures.src. Each fixture is a caller F0 (passes r4-r7 = 5,6,7,8 and `nstack` stack words 0x2222, 0x3333, 0x4444 to F1, returns F1's r0)
and callee functions shaped as GCC/SHC emit them; `expect` lists the constants the printed call in function 0 must show (stack words the callee reads),
`absent` the ones it must not. run.py prints `expect: ok` or `expect: MISSING ...` / `absent: PRESENT ...`.
usage: mk.py   (writes fixtures.src beside it)"""
import os
PRP, PRL = '.word 0x4F22', '.word 0x4F26'           # sts.l pr,@-r15 / lds.l @r15+,pr
MACH_S, MACL_S, MACH_L, MACL_L = '.word 0x4F02', '.word 0x4F12', '.word 0x4F06', '.word 0x4F16'
GBR_S, GBR_L = '.word 0x4F13', '.word 0x4F17'       # stc.l gbr,@-r15 / ldc.l @r15+,gbr
STK = [0x2222, 0x3333, 0x4444]
out = ['# x1: the smallest module: selftest.sh runs it against a substituted generator binary', 'name x1', 'ret 1', 'env SE_GENERATED_CLOBBERS=0', 'F0:', 'mov r4,r0', 'rts', 'add r5,r0']


def caller(nstack, via='r1', pre=()):
    c = ['F0:', PRP]
    if nstack:
        c.append('add #-%d,r15' % (4 * nstack))
    c += list(pre)
    for i in range(nstack):
        c += ['mov.l =0x%X,r1' % STK[i], 'mov.l r1,@(%d,r15)' % (4 * i)]
    c += ['mov #5,r4', 'mov #6,r5', 'mov #7,r6', 'mov #8,r7']
    c += ['mov.l =@F1,%s' % via, 'jsr @%s' % via, 'nop']
    if nstack:
        c.append('add #%d,r15' % (4 * nstack))
    c += [PRL, 'rts', 'nop']
    return c


def fx(name, doc, nstack, callee, expect=(), absent=(), extra=(), clob='0,0', ret='1,1', via='r1', caller_lines=None):
    out.append('# %s: %s' % (name, doc))
    out.append('name ' + name)
    out.append('ret ' + ret)
    out.append('env SE_GENERATED_CLOBBERS=' + clob)
    if expect:
        out.append('expect ' + ','.join('0x%X' % STK[i] for i in expect))
    if absent:
        out.append('absent ' + ','.join('0x%X' % STK[i] for i in absent))
    out.extend(caller_lines or caller(nstack, via))
    out.append('fn')
    out.extend(['F1:'] + callee)
    for e in extra:
        out.append('fn')
        out.extend(e)


LOC1 = ['F0:', PRP, 'add #-4,r15', 'mov.l =0x2222,r1', 'mov.l r1,@(0,r15)', 'mov #5,r4', 'mov #6,r5', 'mov #7,r6', 'mov #8,r7', 'mov.l =@F1,r1', 'jsr @r1', 'nop',
        'mov.l @(0,r15),r1', 'add r1,r0', 'add #4,r15', PRL, 'rts', 'nop']      # a caller with a local at its r15+0 that passes only register arguments

# ---- 1. va_arg ways ---------------------------------------------------------------------------------------------------------------
va_pro = ['mov.l r7,@-r15', 'mov.l r6,@-r15', 'mov.l r5,@-r15']     # f(int n, ...): r5-r7 pushed, r7 first; the stack word sits above the spill
fx('d1_unroll', 'va_arg unrolled, ap in r2 post-incremented four times: r5, r6, r7, then the stack word', 1,
   va_pro + ['mov r15,r2', 'mov.l @r2+,r0', 'mov.l @r2+,r3', 'add r3,r0', 'mov.l @r2+,r3', 'add r3,r0', 'mov.l @r2+,r3', 'add r3,r0', 'add #12,r15', 'rts', 'nop'], expect=[0])
fx('d1_loop_memap', 'va_list kept in a stack slot (-O0): reload, read, advance, store, four times in a counted loop', 1,
   va_pro + ['add #-4,r15', 'mov r15,r1', 'add #4,r1', 'mov.l r1,@(0,r15)', 'mov #4,r1', 'mov #0,r0',
             'F1_L:', 'mov.l @(0,r15),r2', 'mov.l @r2+,r3', 'mov.l r2,@(0,r15)', 'add r3,r0', 'dt r1', 'bf F1_L', 'add #16,r15', 'rts', 'nop'], expect=[0])
fx('d1_unroll_memap', 'va_list in a stack slot, four unrolled advances', 1,
   va_pro + ['add #-4,r15', 'mov r15,r1', 'add #4,r1', 'mov.l r1,@(0,r15)', 'mov #0,r0'] +
   sum([['mov.l @(0,r15),r2', 'mov.l @r2,r3', 'add #4,r2', 'mov.l r2,@(0,r15)', 'add r3,r0'] for _ in range(4)], []) + ['add #16,r15', 'rts', 'nop'], expect=[0])
fx('d1_vprintf_wrapper', 'my_printf(fmt, ...) { vprintf(fmt, ap) }: the address of the spill goes to a callee that walks it past the spill into the stack word', 1,
   [PRP] + va_pro + ['mov r15,r5', 'mov.l =@F2,r1', 'jsr @r1', 'nop', 'add #12,r15', PRL, 'rts', 'nop'], expect=[0],
   extra=[['F2:', 'mov r5,r1', 'mov #4,r2', 'mov #0,r0', 'F2_L:', 'mov.l @r1+,r3', 'add r3,r0', 'dt r2', 'bf F2_L', 'rts', 'nop']])
fx('d1_va_countloop', 'va_arg loop counted by r4 (n values), n = 5 here: words r5-r7 then the stack word', 1,
   va_pro + ['mov r15,r2', 'mov r4,r1', 'mov #0,r0', 'F1_L:', 'mov.l @r2+,r3', 'add r3,r0', 'dt r1', 'bf F1_L', 'add #12,r15', 'rts', 'nop'], expect=[0])

# ---- 2. the frame pointer ---------------------------------------------------------------------------------------------------------
fp_pro = ['mov.l r14,@-r15', 'mov.l r7,@-r15', 'mov.l r6,@-r15', 'mov.l r5,@-r15', 'mov.l r4,@-r15', 'mov r15,r14']
fp_epi = ['mov r14,r15', 'add #16,r15', 'rts', 'mov.l @r15+,r14']
fx('d2_r14_fixed', 'frame pointer r14 = r15 after pushing r14 and r4-r7; the fifth argument read at @(20,r14)', 1,
   fp_pro + ['mov.l @(20,r14),r0', 'mov.l @(0,r14),r1', 'add r1,r0'] + fp_epi, expect=[0])
fx('d2_r14_loop', 'the same with a loop through r14 over the five words', 1,
   fp_pro + ['mov r14,r2', 'mov #5,r1', 'mov #0,r0', 'F1_L:', 'mov.l @r2+,r3', 'add r3,r0', 'dt r1', 'bf F1_L'] + fp_epi, expect=[0])
fx('d2_r14_index', 'index through r0 from r14: @(r0,r14) with r0 = 20', 1,
   fp_pro + ['mov #20,r0', 'mov.l @(r0,r14),r0'] + fp_epi, expect=[0])

# ---- 3. address of a stack parameter -----------------------------------------------------------------------------------------------
fx('d3_addr_param5', '&param5 handed to a leaf that reads it (F1 itself never reads the word)', 1,
   ['mov.l r8,@-r15', PRP, 'mov r4,r8', 'mov r15,r4', 'add #8,r4', 'mov.l =@F2,r1', 'jsr @r1', 'nop', 'add r8,r0', PRL, 'rts', 'mov.l @r15+,r8'], expect=[0],
   extra=[['F2:', 'mov.l @r4,r0', 'rts', 'nop']])
fx('d3_addr_param6', '&param5 handed to a leaf that reads param5 and the word after it (param6)', 2,
   ['mov.l r8,@-r15', PRP, 'mov r4,r8', 'mov r15,r4', 'add #8,r4', 'mov.l =@F2,r1', 'jsr @r1', 'nop', 'add r8,r0', PRL, 'rts', 'mov.l @r15+,r8'], expect=[0, 1],
   extra=[['F2:', 'mov.l @r4,r0', 'mov.l @(4,r4),r1', 'rts', 'add r1,r0']])
fx('d3_param5_rmw', 'param5 modified in place (its slot is the variable): read, add, write back, return it', 1,
   ['mov.l @(0,r15),r0', 'add r4,r0', 'mov.l r0,@(0,r15)', 'rts', 'nop'], expect=[0])
fx('d3_param5_ptr_store', 'param5 address stored into a global, read back through the global in the same function', 1,
   ['mov.l =0x06020000,r2', 'mov r15,r1', 'mov.l r1,@r2', 'mov.l @r2,r3', 'mov.l @r3,r0', 'rts', 'nop'], expect=[0])

# ---- 4. structs by value on the stack ---------------------------------------------------------------------------------------------
fx('d4_struct_fixed', 'a 3-word struct passed on the stack, the callee copies it to a local by fixed offsets', 3,
   ['add #-12,r15', 'mov.l @(12,r15),r1', 'mov.l r1,@(0,r15)', 'mov.l @(16,r15),r1', 'mov.l r1,@(4,r15)', 'mov.l @(20,r15),r1', 'mov.l r1,@(8,r15)',
    'mov.l @(0,r15),r0', 'mov.l @(8,r15),r2', 'add r2,r0', 'add #12,r15', 'rts', 'nop'], expect=[0, 1, 2])
fx('d4_struct_loop', 'the same copy by a counted loop through a pointer into the incoming words', 3,
   ['add #-12,r15', 'mov r15,r2', 'add #12,r2', 'mov r15,r3', 'mov #3,r1', 'F1_L:', 'mov.l @r2+,r5', 'mov.l r5,@r3', 'add #4,r3', 'dt r1', 'bf F1_L',
    'mov.l @(0,r15),r0', 'mov.l @(8,r15),r2', 'add r2,r0', 'add #12,r15', 'rts', 'nop'], expect=[0, 1, 2])
fx('d4_struct_memcpy', 'the incoming struct handed to a memcpy-like callee by address (r5 = entry r15), the copy read afterwards', 3,
   [PRP, 'add #-12,r15', 'mov r15,r4', 'mov r15,r5', 'add #16,r5', 'mov #12,r6', 'mov.l =@F2,r1', 'jsr @r1', 'nop', 'mov.l @(0,r15),r0', 'mov.l @(8,r15),r2', 'add r2,r0', 'add #12,r15', PRL, 'rts', 'nop'],
   expect=[0, 1, 2], extra=[['F2:', 'mov #3,r1', 'F2_L:', 'mov.l @r5+,r2', 'mov.l r2,@r4', 'add #4,r4', 'dt r1', 'bf F2_L', 'rts', 'nop']])

# ---- 5. calls through a function pointer --------------------------------------------------------------------------------------------
fx('d5_ind_reg3', 'the caller reaches the callee through r3 (mov.l =F1,r3 ; jsr @r3), a pointer it knows; stack word read', 1,
   ['mov.l @(0,r15),r0', 'add r4,r0', 'rts', 'nop'], expect=[0], via='r3')
fx('d5_ind_loaded', 'the caller loads the target from a global (not resolvable); the callee reads the stack word', 1,
   ['mov.l @(0,r15),r0', 'add r4,r0', 'rts', 'nop'], expect=[0],
   caller_lines=['F0:', PRP, 'add #-4,r15', 'mov.l =0x2222,r1', 'mov.l r1,@(0,r15)', 'mov #5,r4', 'mov #6,r5', 'mov #7,r6', 'mov #8,r7',
                 'mov.l =0x06020000,r2', 'mov.l @r2,r1', 'jsr @r1', 'nop', 'add #4,r15', PRL, 'rts', 'nop'])
fx('d5_ind_inside', 'F1 calls F2 through a pointer in r3 with a stack word of its own; F2 reads it', 1,
   [PRP, 'mov.l @(4,r15),r1', 'add #-4,r15', 'mov.l r1,@(0,r15)', 'mov.l =@F2,r3', 'jsr @r3', 'nop', 'add #4,r15', PRL, 'rts', 'nop'], expect=[0],
   extra=[['F2:', 'mov.l @(0,r15),r0', 'add r4,r0', 'rts', 'nop']])

# ---- 6. leaf and non-leaf stack readers --------------------------------------------------------------------------------------------
fx('d6_leaf_param6only', 'a leaf that reads the sixth argument only (@(4,r15)); the fifth is not read', 2,
   ['mov.l @(4,r15),r0', 'rts', 'nop'], expect=[1])
fx('d6_cond_read', 'the stack word read on one path only (tst r4 ; bt)', 1,
   ['tst r4,r4', 'bt F1_Z', 'mov.l @(0,r15),r0', 'rts', 'nop', 'F1_Z:', 'mov #0,r0', 'rts', 'nop'], expect=[0])
fx('d6_read_after_call', 'the non-leaf reads param5 after a call (PR saved: the word is at r15+4)', 1,
   [PRP, 'mov.l =@F2,r1', 'jsr @r1', 'nop', 'mov.l @(4,r15),r1', 'add r1,r0', PRL, 'rts', 'nop'], expect=[0], extra=[['F2:', 'mov r4,r0', 'rts', 'nop']])
fx('d6_const_index', 'param5 read through a literal index: mov.l =20,r0 ; mov.l @(r0,r15),r0 under a 16 byte frame', 1,
   ['add #-16,r15', 'mov.l =20,r0', 'mov.l @(r0,r15),r0', 'add #16,r15', 'rts', 'nop'], expect=[0])
fx('d6_far_word', 'param 17: word 12 above the entry r15 (offset 48) read through an added pointer; the caller passes one word, the callee reads past it (undefined), text must show the word as an argument only if 12 are passed: here 1 word', 1,
   ['mov r15,r1', 'add #0,r1', 'mov.l @r1,r0', 'rts', 'nop'], expect=[0])
fx('d6_two_regs_one_stack', 'a leaf reading stack word 1 and r7 but not r4-r6', 2,
   ['mov.l @(0,r15),r0', 'add r7,r0', 'rts', 'nop'], expect=[0])

# ---- 7. tail calls --------------------------------------------------------------------------------------------------------------
fx('d7_thunk_stack', 'a thunk (mov.l =F2,r1 ; jmp @r1 ; nop) to a callee that reads the stack word passed through', 1,
   ['mov.l =@F2,r1', 'jmp @r1', 'nop'], expect=[0], extra=[['F2:', 'mov.l @(0,r15),r0', 'add r4,r0', 'rts', 'nop']])
fx('d7_tail_after_pop', 'adjusts r4, restores PR, then jumps (SHC tail call with the PR restored before the jump): the stack word passes through', 1,
   [PRP, 'add #1,r4', PRL, 'mov.l =@F2,r1', 'jmp @r1', 'nop'], expect=[0], extra=[['F2:', 'mov.l @(0,r15),r0', 'add r4,r0', 'rts', 'nop']])
fx('d7_tail_bra', 'a tail call by bra with the stack word read by the target', 1,
   ['add #1,r4', 'bra F2', 'nop'], expect=[0],
   extra=[['F2:', 'mov.l @(0,r15),r0', 'add r4,r0', 'rts', 'nop']])

# ---- 8. chains of calls ---------------------------------------------------------------------------------------------------------
fx('d8_forward_stack', 'F1 forwards its own stack argument to F2 (re-pushed in the outgoing area); only F2 reads it', 1,
   [PRP, 'add #-4,r15', 'mov.l @(8,r15),r1', 'mov.l r1,@(0,r15)', 'mov.l =@F2,r1', 'jsr @r1', 'nop', 'add #4,r15', PRL, 'rts', 'nop'], expect=[0],
   extra=[['F2:', 'mov.l @(0,r15),r0', 'add r4,r0', 'rts', 'nop']])
fx('d8_pass_reg_chain', 'F1 passes r4-r7 through to F2, which reads r7 and a stack word; F1 never touches the stack argument', 1,
   [PRP, 'mov.l =@F2,r1', 'jsr @r1', 'nop', PRL, 'rts', 'nop'], expect=[0],
   extra=[['F2:', 'mov.l @(0,r15),r0', 'add r7,r0', 'rts', 'nop']])

# ---- 9. MACH/MACL/GBR saved by the callee ----------------------------------------------------------------------------------------------
fx('d9_mac_saved', 'the callee saves MACH and MACL, multiplies, restores them; the caller keeps a product in MACL across the call and reads it after', 0,
   [MACL_S, MACH_S, 'dmulu.l r4,r5', 'sts macl,r0', MACH_L, 'rts', MACL_L], clob='100000,0',
   caller_lines=['F0:', PRP, 'mul.l r4,r5', 'mov #5,r4', 'mov #6,r5', 'mov.l =@F1,r1', 'jsr @r1', 'nop', 'sts macl,r1', 'add r1,r0', PRL, 'rts', 'nop'])
fx('d9_gbr_saved', 'the callee sets GBR from r4, reads @(4,gbr), restores GBR; the caller reads @(8,gbr) after the call', 0,
   [GBR_S, 'ldc r4,gbr', 'mov.l @(4,gbr),r0', GBR_L, 'rts', 'nop'],
   caller_lines=['F0:', PRP, 'mov.l =@F1,r1', 'mov #5,r4', 'jsr @r1', 'nop', 'mov r0,r2', 'mov.l @(8,gbr),r0', 'add r2,r0', PRL, 'rts', 'nop'])
fx('d9_gbr_unsaved', 'the callee sets GBR and does not restore it (a context-switch helper); the caller reads @(8,gbr) after: a caution is expected', 0,
   ['ldc r4,gbr', 'mov.l @(4,gbr),r0', 'rts', 'nop'], clob='0x20000,0x20000',
   caller_lines=['F0:', PRP, 'mov.l =@F1,r1', 'mov #5,r4', 'jsr @r1', 'nop', 'mov r0,r2', 'mov.l @(8,gbr),r0', 'add r2,r0', PRL, 'rts', 'nop'])

# ---- 10. ordinary code that A3-284's withholding must not make print extra arguments ----------------------------------------------------
fx('d10_array_sum4', 'int v[4]={a,b,c,d}; sum by a counted pointer loop (-O0): r4-r7 into a local array, a loop over it; the caller has a local at its r15+0 and passes four register arguments', 0,
   ['mov.l r7,@-r15', 'mov.l r6,@-r15', 'mov.l r5,@-r15', 'mov.l r4,@-r15', 'mov r15,r2', 'mov #4,r1', 'mov #0,r0', 'F1_L:', 'mov.l @r2+,r3', 'add r3,r0', 'dt r1', 'bf F1_L', 'add #16,r15', 'rts', 'nop'],
   absent=[0], caller_lines=LOC1)
fx('d10_spill_fixed', 'non-varargs -O0: r4-r7 stored to the frame, read back at fixed offsets, an array slot written by a variable index; the caller has a local at r15+0', 0,
   ['add #-32,r15', 'mov.l r4,@(16,r15)', 'mov.l r5,@(20,r15)', 'mov.l r6,@(24,r15)', 'mov.l r7,@(28,r15)', 'mov.l @(16,r15),r1', 'mov r5,r0', 'shll2 r0', 'mov.l r1,@(r0,r15)',
    'mov.l @(20,r15),r0', 'add #32,r15', 'rts', 'nop'], absent=[0], caller_lines=LOC1)
fx('d10_addr_param_local', 'int f(int a) { g(&a); return a; }: r4 spilled, its address handed to a callee that writes it; the caller has a local at r15+0', 0,
   [PRP, 'mov.l r4,@-r15', 'mov r15,r4', 'mov.l =@F2,r1', 'jsr @r1', 'nop', 'mov.l @r15+,r0', PRL, 'rts', 'nop'], absent=[0],
   extra=[['F2:', 'mov.l @r4,r1', 'add #1,r1', 'mov.l r1,@r4', 'rts', 'nop']], caller_lines=LOC1)
fx('d10_loop_local_array', 'a local int v[8] filled and summed by pointer loops (not from a spill), r4 spilled above it; the caller has a local at r15+0', 0,
   ['add #-36,r15', 'mov.l r4,@(32,r15)', 'mov r15,r2', 'mov #8,r1', 'F1_L:', 'mov.l r1,@r2', 'add #4,r2', 'dt r1', 'bf F1_L', 'mov r15,r2', 'mov #8,r1', 'mov #0,r0', 'F1_M:', 'mov.l @r2+,r3', 'add r3,r0', 'dt r1', 'bf F1_M',
    'mov.l @(32,r15),r1', 'add r1,r0', 'add #36,r15', 'rts', 'nop'], absent=[0], caller_lines=LOC1)


# ---- 11. more spill shapes the A3-280 flag may not reach ---------------------------------------------------------------------------------
fx('d11_spill_reverse', 'r4-r7 pushed in the other order (r4 first, so r7 is the lowest word); a counted loop from r15 over five words ends on the stack word', 1,
   ['mov.l r4,@-r15', 'mov.l r5,@-r15', 'mov.l r6,@-r15', 'mov.l r7,@-r15', 'mov r15,r2', 'mov #5,r1', 'mov #0,r0', 'F1_L:', 'mov.l @r2+,r3', 'add r3,r0', 'dt r1', 'bf F1_L', 'add #16,r15', 'rts', 'nop'], expect=[0])
fx('d11_spill_stores', 'SHC style: add #-16,r15 then mov.l r4,@r15 ... r7,@(12,r15); a loop from r15 reads five words (the fifth is the stack argument)', 1,
   ['add #-16,r15', 'mov.l r4,@r15', 'mov.l r5,@(4,r15)', 'mov.l r6,@(8,r15)', 'mov.l r7,@(12,r15)', 'mov r15,r2', 'mov #5,r1', 'mov #0,r0', 'F1_L:', 'mov.l @r2+,r3', 'add r3,r0', 'dt r1', 'bf F1_L', 'add #16,r15', 'rts', 'nop'], expect=[0])
fx('d11_spill_r7only', 'f(a, b, c, ...): only r7 is a varargs word and is pushed; the walk reads r7 and then the stack word (two words)', 1,
   ['mov.l r7,@-r15', 'mov r15,r2', 'mov #2,r1', 'mov #0,r0', 'F1_L:', 'mov.l @r2+,r3', 'add r3,r0', 'dt r1', 'bf F1_L', 'add #4,r15', 'rts', 'nop'], expect=[0])
fx('d11_index_spill', 'the index through r0 from the current r15 with the spill at r15+0: r0 = 16 reaches the stack word', 1,
   ['mov.l r7,@-r15', 'mov.l r6,@-r15', 'mov.l r5,@-r15', 'mov.l r4,@-r15', 'mov #12,r0', 'shll2 r0', 'shlr2 r0', 'add #4,r0', 'mov.l @(r0,r15),r0', 'add #16,r15', 'rts', 'nop'], expect=[0])
fx('d11_pointer_in_arg', 'the spill pointer handed in a callee-saved register across a loop with a call: r8 = r15 after the spill, F2 called, then the read through r8+16', 1,
   ['mov.l r8,@-r15', PRP, 'mov.l r7,@-r15', 'mov.l r6,@-r15', 'mov.l r5,@-r15', 'mov.l r4,@-r15', 'mov r15,r8', 'mov.l =@F2,r1', 'jsr @r1', 'nop', 'mov.l @(24,r8),r1', 'add r1,r0', 'add #16,r15', PRL, 'rts', 'mov.l @r15+,r8'], expect=[0],
   extra=[['F2:', 'mov r4,r0', 'rts', 'nop']])

# ---- 12. returns ---------------------------------------------------------------------------------------------------------------------
fx('d12_ret64_onepath', 'returns r0 only on one path and r0:r1 (a 64-bit product) on the other; the caller uses r0 and r1', 0,
   ['tst r4,r4', 'bt F1_Z', 'dmulu.l r4,r5', 'sts macl,r1', 'rts', 'sts mach,r0', 'F1_Z:', 'mov #0,r0', 'rts', 'mov #0,r1'], clob='180000,180000',
   caller_lines=['F0:', PRP, 'mov #5,r4', 'mov #6,r5', 'mov.l =@F1,r1', 'jsr @r1', 'nop', 'add r1,r0', PRL, 'rts', 'nop'])
fx('d12_sret_thunk', 'a struct return: the caller passes the hidden pointer in r2 (a local), the callee is a thunk to F2 which writes through r2; the caller reads the local', 0,
   ['mov.l =@F2,r1', 'jmp @r1', 'nop'], ret='1,0',
   extra=[['F2:', 'mov.l r4,@r2', 'mov.l r5,@(4,r2)', 'rts', 'nop']],
   caller_lines=['F0:', PRP, 'add #-8,r15', 'mov r15,r2', 'mov #5,r4', 'mov #6,r5', 'mov.l =@F1,r1', 'jsr @r1', 'nop', 'mov.l @(0,r15),r0', 'mov.l @(4,r15),r1', 'add r1,r0', 'add #8,r15', PRL, 'rts', 'nop'])
fx('d12_cond_tail_ret', 'one path returns 0 itself, the other tail-calls F2 which returns r0: the call yields a value the caller returns', 0,
   ['tst r4,r4', 'bt F1_Z', 'mov.l =@F2,r1', 'jmp @r1', 'nop', 'F1_Z:', 'rts', 'mov #0,r0'],
   extra=[['F2:', 'mov r4,r0', 'rts', 'add r5,r0']])

# ---- 13. delay slots of calls and tail jumps ---------------------------------------------------------------------------------------------
fx('d13_jsr_slot_args', 'the caller sets r7 and r5 in the delay slots of the argument setup: jsr @r1 with `mov #8,r7` as the delay slot; the callee reads r4-r7', 0,
   ['mov r4,r0', 'add r5,r0', 'add r6,r0', 'rts', 'add r7,r0'],
   caller_lines=['F0:', PRP, 'mov #5,r4', 'mov #6,r5', 'mov #7,r6', 'mov.l =@F1,r1', 'jsr @r1', 'mov #8,r7', PRL, 'rts', 'nop'])
fx('d13_jmp_slot_thunk', 'a C++ this-adjusting thunk: jmp @r1 with `add #8,r4` in the delay slot; the target reads r4, r5 and r6', 0,
   ['mov.l =@F2,r1', 'jmp @r1', 'add #8,r4'],
   extra=[['F2:', 'mov r4,r0', 'add r5,r0', 'rts', 'add r6,r0']],
   caller_lines=['F0:', PRP, 'mov #5,r4', 'mov #6,r5', 'mov #7,r6', 'mov.l =@F1,r1', 'jsr @r1', 'nop', PRL, 'rts', 'nop'])
fx('d13_bsr_slot_arg', 'bsr with the fourth argument set in the delay slot', 0,
   ['mov r4,r0', 'add r5,r0', 'add r6,r0', 'rts', 'add r7,r0'],
   caller_lines=['F0:', PRP, 'mov #5,r4', 'mov #6,r5', 'mov #7,r6', 'bsr F1', 'mov #8,r7', PRL, 'rts', 'nop'])

# ---- 14. the address of an incoming stack word handed on (A3-286's class) -------------------------------------------------------------
fx('d14_addr_param5_indirect', '&param5 handed to a callee reached through a pointer the engine cannot resolve (the target is loaded from a global)', 1,
   ['mov.l r8,@-r15', PRP, 'mov r4,r8', 'mov r15,r4', 'add #8,r4', 'mov.l =0x06020000,r1', 'mov.l @r1,r1', 'jsr @r1', 'nop', 'add r8,r0', PRL, 'rts', 'mov.l @r15+,r8'], expect=[0])
fx('d14_addr_param5_write', '&param5 handed to a callee that writes through it (an out parameter in the fifth slot); the caller reads its outgoing word back after the call', 1,
   ['mov.l r8,@-r15', PRP, 'mov r4,r8', 'mov r15,r4', 'add #8,r4', 'mov.l =@F2,r1', 'jsr @r1', 'nop', 'add r8,r0', PRL, 'rts', 'mov.l @r15+,r8'], expect=[0],
   extra=[['F2:', 'mov.l @r4,r0', 'add #1,r0', 'rts', 'mov.l r0,@r4']])
fx('d14_addr_param5_chain', '&param5 passed down two calls (F1 -> F2 -> F3 reads it)', 1,
   ['mov.l r8,@-r15', PRP, 'mov r4,r8', 'mov r15,r4', 'add #8,r4', 'mov.l =@F2,r1', 'jsr @r1', 'nop', 'add r8,r0', PRL, 'rts', 'mov.l @r15+,r8'], expect=[0],
   extra=[[ 'F2:', PRP, 'mov.l =@F3,r1', 'jsr @r1', 'nop', PRL, 'rts', 'nop'], ['F3:', 'mov.l @r4,r0', 'rts', 'nop']])
fx('d14_addr_stored_local', 'the address of param5 stored to a local pointer variable (-O0 `int *p = &e;`), reloaded and read: no loop, no call', 1,
   ['add #-4,r15', 'mov r15,r1', 'add #8,r1', 'mov.l r1,@(0,r15)', 'mov.l @(0,r15),r2', 'mov.l @r2,r0', 'add #4,r15', 'rts', 'nop'], expect=[0])

open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'fixtures.src'), 'w').write('\n'.join(out) + '\n')
print(len([l for l in out if l.startswith('name ')]), 'fixtures')
