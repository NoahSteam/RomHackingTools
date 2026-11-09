#!/usr/bin/env python3
"""Area 3 round 22: the A3-177 shape (an instruction that is the delay slot of a conditional delayed branch and the target of a bra, a poll on one of its two paths)
in a few hundred variants. usage: variants.py <outdir>: writes <outdir>/<name>.s for each variant (asm22.py input)."""
import itertools, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
slots = {'b':'mov.b r2,@r6', 'w':'mov.w r2,@r6', 'l':'mov.l r2,@r6', 'ld':'mov.l r2,@(4,r6)', 'pd':'mov.l r2,@-r6', 'ix':'mov.w r2,@(r0,r6)'}
reads = {'none':'', 'w':'mov.w @r6,r3', 'b':'mov.b @r6,r3', 'l':'mov.l @r6,r3'}
brs = {'bt':('cmp/eq r4,r4','bt/s'), 'bf':('cmp/eq r4,r5','bf/s')}
after = {'none':'', 'st':'mov.b r4,@r6', 'stl':'mov.l r4,@r6'}
names=[]
for (sn,sl),(rn,rd),(bn,(cmp,br)),(an,af),swap in itertools.product(slots.items(), reads.items(), brs.items(), after.items(), (0,1)):
    # swap: which path polls. swap=0: the fall-through after the slot polls and the branch target returns; swap=1: the target polls and the fall-through returns
    if swap == 0:
        body = f"""mov.l =0x0600228C,r6
mov r5,r2
mov #0,r0
tst r4,r5
bf B
{cmp}
{br} R
S:
{sl}
mov.l =0x26001018,r7
mov #1,r0
mov.w r0,@r7
P:
mov.w @r7,r0
tst #1,r0
bf P
{af}
rts
{rd or 'nop'}
R:
rts
{rd or 'nop'}
B:
bra S
nop"""
    else:
        body = f"""mov.l =0x0600228C,r6
mov r5,r2
mov #0,r0
tst r4,r5
bf B
{cmp}
{br} Q
S:
{sl}
rts
{rd or 'nop'}
Q:
mov.l =0x26001018,r7
mov #1,r0
mov.w r0,@r7
P:
mov.w @r7,r0
tst #1,r0
bf P
{af}
rts
{rd or 'nop'}
B:
bra S
nop"""
    names.append((f"{sn}-{rn}-{bn}-{an}-{swap}", body))

d = sys.argv[1]
os.makedirs(d, exist_ok=True)
for n, b in names:
    open(os.path.join(d, n + '.s'), 'w').write(b + '\n')
