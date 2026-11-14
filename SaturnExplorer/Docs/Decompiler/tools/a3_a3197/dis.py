#!/usr/bin/env python3
"""Area 3, A3-197: a small SH-2 listing of a memory image (the common opcodes; enough to read a callee's saves and returns).
usage: dis.py <hwram image> <from hex> <to hex>"""
import sys
img=open(sys.argv[1],'rb').read(); a0=int(sys.argv[2],16); a1=int(sys.argv[3],16)
def w(a): o=a-0x06000000; return img[o]<<8|img[o+1]
def L(a): return w(a)<<16|w(a+2)
def s8(x): return x-256 if x&0x80 else x
def s12(x): return x-4096 if x&0x800 else x
def dis(a,i):
    n=(i>>8)&15; m=(i>>4)&15; d=i&0xff; t=i>>12
    tb={0x0009:'nop',0x000B:'rts',0x0008:'clrt',0x0018:'sett',0x002B:'rte'}
    if i in tb: return tb[i]
    if t==0:
        lo=i&15; lo8=i&0xff
        x={0x02:'stc sr,r%d',0x12:'stc gbr,r%d',0x22:'stc vbr,r%d',0x0A:'sts mach,r%d',0x1A:'sts macl,r%d',0x2A:'sts pr,r%d',0x29:'movt r%d',0x23:'braf r%d',0x03:'bsrf r%d'}
        if lo8 in x: return x[lo8]%n
        y={4:'mov.b r%d,@(r0,r%d)',5:'mov.w r%d,@(r0,r%d)',6:'mov.l r%d,@(r0,r%d)',0xC:'mov.b @(r0,r%d),r%d',0xD:'mov.w @(r0,r%d),r%d',0xE:'mov.l @(r0,r%d),r%d',7:'mul.l r%d,r%d',0xF:'mac.l @r%d+,@r%d+'}
        if lo in y: return y[lo]%((m,n) if lo in (4,5,6,7,0xF) else (m,n))
    if t==1: return 'mov.l r%d,@(%d,r%d)'%(m,(i&15)*4,n)
    if t==2:
        ops=['mov.b r%d,@r%d','mov.w r%d,@r%d','mov.l r%d,@r%d','?','mov.b r%d,@-r%d','mov.w r%d,@-r%d','mov.l r%d,@-r%d','div0s r%d,r%d','tst r%d,r%d','and r%d,r%d','xor r%d,r%d','or r%d,r%d','cmp/str r%d,r%d','xtrct r%d,r%d','mulu.w r%d,r%d','muls.w r%d,r%d']
        return ops[i&15]%(m,n)
    if t==3:
        ops=['cmp/eq','?','cmp/hs','cmp/ge','div1','dmulu.l','cmp/hi','cmp/gt','sub','?','subc','subv','add','dmuls.l','addc','addv']
        return ops[i&15]+' r%d,r%d'%(m,n)
    if t==4:
        lo8=i&0xff
        x={0x00:'shll',0x01:'shlr',0x04:'rotl',0x05:'rotr',0x08:'shll2',0x09:'shlr2',0x18:'shll8',0x19:'shlr8',0x28:'shll16',0x29:'shlr16',0x10:'dt',0x11:'cmp/pz',0x15:'cmp/pl',0x20:'shal',0x21:'shar',0x24:'rotcl',0x25:'rotcr',0x0B:'jsr @',0x2B:'jmp @',0x1B:'tas.b @',0x22:'sts.l pr,@-',0x26:'lds.l @+pr,',0x0E:'ldc sr,',0x1E:'ldc gbr,',0x2E:'ldc vbr,',0x12:'sts.l macl,@-',0x16:'lds.l @+macl,',0x2A:'lds pr,',0x1A:'lds macl,',0x0A:'lds mach,',0x03:'stc.l sr,@-',0x13:'stc.l gbr,@-',0x07:'ldc.l @+sr,',0x17:'ldc.l @+gbr,',0x02:'sts.l mach,@-',0x06:'lds.l @+mach,'}
        if lo8 in x: return x[lo8]+'r%d'%n
        if i&15==0xF: return 'mac.w @r%d+,@r%d+'%(m,n)
    if t==5: return 'mov.l @(%d,r%d),r%d'%((i&15)*4,m,n)
    if t==6:
        ops=['mov.b @r%d,r%d','mov.w @r%d,r%d','mov.l @r%d,r%d','mov r%d,r%d','mov.b @r%d+,r%d','mov.w @r%d+,r%d','mov.l @r%d+,r%d','not r%d,r%d','swap.b r%d,r%d','swap.w r%d,r%d','negc r%d,r%d','neg r%d,r%d','extu.b r%d,r%d','extu.w r%d,r%d','exts.b r%d,r%d','exts.w r%d,r%d']
        return ops[i&15]%(m,n)
    if t==7: return 'add #%d,r%d'%(s8(d),n)
    if t==8:
        sub=n
        if sub==0: return 'mov.b r0,@(%d,r%d)'%(i&15,m)
        if sub==1: return 'mov.w r0,@(%d,r%d)'%((i&15)*2,m)
        if sub==4: return 'mov.b @(%d,r%d),r0'%(i&15,m)
        if sub==5: return 'mov.w @(%d,r%d),r0'%((i&15)*2,m)
        if sub==8: return 'cmp/eq #%d,r0'%s8(d)
        nm={9:'bt',0xB:'bf',0xD:'bt/s',0xF:'bf/s'}
        if sub in nm: return '%s %08X'%(nm[sub],a+4+2*s8(d))
    if t==9: ad=a+4+2*d; return 'mov.w @(%08X),r%d  ; =%04X'%(ad,n,w(ad))
    if t==0xA: return 'bra %08X'%(a+4+2*s12(i&0xfff))
    if t==0xB: return 'bsr %08X'%(a+4+2*s12(i&0xfff))
    if t==0xC:
        sub=n; x={0:'mov.b r0,@(%d,gbr)',1:'mov.w r0,@(%d,gbr)',2:'mov.l r0,@(%d,gbr)',4:'mov.b @(%d,gbr),r0',5:'mov.w @(%d,gbr),r0',6:'mov.l @(%d,gbr),r0',8:'tst #%d,r0',9:'and #%d,r0',0xA:'xor #%d,r0',0xB:'or #%d,r0',3:'trapa #%d'}
        if sub==7: ad=(a&~3)+4+4*d; return 'mova @(%08X),r0'%ad
        if sub in x: return x[sub]%d
    if t==0xD: ad=(a&~3)+4+4*d; return 'mov.l @(%08X),r%d  ; =%08X'%(ad,n,L(ad))
    if t==0xE: return 'mov #%d,r%d'%(s8(d),n)
    return '.word %04X'%i
for a in range(a0,a1,2):
    print('%08X %04X  %s'%(a,w(a),dis(a,w(a))))
