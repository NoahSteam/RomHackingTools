#!/usr/bin/env python3
"""walkfuzz.py seed count > out.src : lent-callee / frame-address caller pairs.
The caller keeps 5 in the local at r15+0, passes a frame address (r15+16, r4/r5/r6) to a callee at 06009100 that writes below or at its pointer after a
random transform of it, and reads r15+0 back. The run says whether the callee reached it; the text must agree (checked by walkfuzz_run.py)."""
import random, sys
seed=int(sys.argv[1]); n=int(sys.argv[2])
rnd=random.Random(seed)

def callee_transform(reg, tmp):
    """returns lines transforming `reg` (a pointer) into an equal-valued one by laundering ops (value unchanged)."""
    kinds=['none','spill_l','spill_hw','spill_b','global','mask','shll_shlr','xor2','neg2','swap2','mov14','pushpop','add_sub','rot2','via_r0idx']
    k=rnd.choice(kinds)
    L=[]
    if k=='none': pass
    elif k=='spill_l':
        L+=['add #-4,r15','mov.l %s,@r15'%reg,'mov.l @r15,%s'%reg,'add #4,r15']
    elif k=='spill_hw':
        L+=['add #-4,r15','mov r15,%s'%tmp,'add #2,%s'%tmp,'mov %s,r0'%reg,'mov.w r0,@%s'%tmp,'shlr16 r0','add #-2,%s'%tmp,'mov.w r0,@%s'%tmp,'mov.l @r15,%s'%reg,'add #4,r15']
    elif k=='spill_b':
        L+=['add #-4,r15','mov.l %s,@r15'%reg,'mov r15,%s'%tmp,'mov.b @%s+,r0'%tmp,'extu.b r0,r0','shll8 r0','mov.b @%s+,%s'%(tmp,tmp),'extu.b %s,%s'%(tmp,tmp),'or %s,r0'%tmp,'mov r15,%s'%tmp,'add #2,%s'%tmp,'mov.w @%s,%s'%(tmp,tmp),'extu.w %s,%s'%(tmp,tmp),'mov.w @r15,r0','extu.w r0,r0','shll16 r0','or %s,r0'%tmp,'mov r0,%s'%reg,'add #4,r15']
    elif k=='global':
        L+=['mov.l =0x0600F100,%s'%tmp,'mov.l %s,@%s'%(reg,tmp),'mov.l @%s,%s'%(tmp,reg)]
    elif k=='mask': L+=['mov #-4,%s'%tmp,'and %s,%s'%(tmp,reg)]
    elif k=='shll_shlr': L+=['shll %s'%reg,'shlr %s'%reg] if False else ['shll2 %s'%reg,'shlr2 %s'%reg]
    elif k=='xor2': L+=['mov #3,%s'%tmp,'xor %s,%s'%(tmp,reg),'xor %s,%s'%(tmp,reg)]
    elif k=='neg2': L+=['neg %s,%s'%(reg,reg),'neg %s,%s'%(reg,reg)]
    elif k=='swap2': L+=['swap.w %s,%s'%(reg,reg),'swap.w %s,%s'%(reg,reg)]
    elif k=='mov14': L+=['mov.l r14,@-r15','mov %s,r14'%reg,'mov r14,%s'%reg,'mov.l @r15+,r14']
    elif k=='pushpop': L+=['mov.l %s,@-r15'%reg,'mov.l @r15+,%s'%reg]
    elif k=='add_sub': L+=['mov #8,%s'%tmp,'neg %s,%s'%(tmp,tmp),'add %s,%s'%(tmp,reg),'sub %s,%s'%(tmp,reg)]
    elif k=='rot2': L+=['rotl %s'%reg,'rotr %s'%reg]
    return k,L

def callee_store(reg):
    kinds=['loop_l','loop_b','single_neg','single_neg_reg','single_up','single_neg_unfolded']
    k=rnd.choice(kinds)
    if k=='loop_l': return k,['mov #4,r1','L:','mov.l r1,@-%s'%reg,'dt r1','bf L']
    if k=='loop_b': return k,['mov #16,r1','L:','mov.b r1,@-%s'%reg,'dt r1','bf L']
    if k=='single_neg': return k,['mov #1,r1','add #-16,%s'%reg,'mov.l r1,@%s'%reg]
    if k=='single_neg_reg': return k,['mov #1,r1','mov #-16,r0','mov.l r1,@(r0,%s)'%reg]
    if k=='single_up': return k,['mov #1,r1','mov.l r1,@%s'%reg]
    if k=='single_neg_unfolded': return k,['mov #1,r1','mov #4,r3','neg r3,r3','shll2 r3','add r3,%s'%reg,'mov.l r1,@%s'%reg]

def wrapper(kind, reg):
    """callee body forms: direct, or via a forwarding wrapper to a lent filler at 06009200"""
    return kind

out=[]
for i in range(n):
    reg=rnd.choice(['r4','r4','r4','r5','r6'])
    tmp=rnd.choice(['r2','r3'])
    tk,tl=callee_transform(reg,tmp)
    sk,sl=callee_store(reg)
    # caller side laundering of the pointer
    ck,cl=callee_transform(reg,'r3' if tmp=='r2' else 'r2') if rnd.random()<0.4 else ('none',[])
    # in the caller, the transform must not use the caller's own r15 frame carelessly: caller has a 32-byte frame; the spill uses a word below r15 (add #-4,r15)
    callee=['mov.l r14,@-r15'] if False else []
    body=tl+sl
    wrap=rnd.random()<0.25
    cal=[]
    cal+=['.word 0x4F22','add #-32,r15','mov #5,r0','mov.l r0,@r15']
    cal+=['mov r15,%s'%reg,'add #%d,%s'%(rnd.choice([16,16,16,20]),reg)]
    cal+=cl
    cal+=['mov.l =0x06009100,r1','jsr @r1','nop','mov.l @r15,r0','add #32,r15','.word 0x4F26','rts','nop']
    name='WF seed=%d i=%d reg=%s transform=%s store=%s caller=%s%s'%(seed,i,reg,tk,sk,ck,' wrap' if wrap else '')
    out.append('name '+name)
    out.append('asm 06009000'); out+=cal; out.append('endasm')
    if wrap:
        # callee at 06009100 forwards to 06009200 which does transform+store; the wrapper keeps PR
        w=['.word 0x4F22','mov.l =0x06009200,r1','jsr @r1','nop','.word 0x4F26','rts','nop']
        out.append('asm 06009100'); out+=w; out.append('endasm')
        out.append('asm 06009200'); out+=body+['rts','nop']; out.append('endasm')
        out.append('lend 06009200 06009100')
    else:
        out.append('asm 06009100'); out+=body+['rts','nop']; out.append('endasm')
        out.append('lend 06009100')
    out.append('quiet'); out.append('args 0 0 0 0'); out.append('end')
print('\n'.join(out))
