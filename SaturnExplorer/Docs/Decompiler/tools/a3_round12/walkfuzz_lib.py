rnd=None
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

