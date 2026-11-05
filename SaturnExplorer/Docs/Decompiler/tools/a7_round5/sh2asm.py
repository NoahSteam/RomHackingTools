#!/usr/bin/env python3
# tiny SH-2 assembler for probe lines. usage: asm.py file.s  -> prints probe line name|base|words
import re,sys
def reg(s):
    s=s.strip()
    if s=='sp': return 15
    m=re.fullmatch(r'r(\d+)',s)
    if not m: raise Exception('reg '+s)
    return int(m.group(1))
def num(s):
    return int(s,0)
def assemble(lines, base):
    # pass: items = (kind, data)
    items=[]; labels={}; pool=[]; firstend=None  # pool entries (value or label)
    addr=base
    for ln in lines:
        ln=ln.split(';')[0].strip()
        if not ln: continue
        m=re.match(r'(\w+):\s*(.*)',ln)
        while m:
            labels[m.group(1)]=addr
            ln=m.group(2).strip()
            m=re.match(r'(\w+):\s*(.*)',ln)
        if not ln: continue
        if ln.startswith('.org'):
            a=num(ln.split()[1])
            if firstend is None: firstend=addr
            addr=a
            continue
        if ln.startswith('.align4'):
            if addr%4: items.append(('raw',addr,9)); addr+=2
            continue
        if ln.startswith('.word'):
            items.append(('word',addr,ln.split(None,1)[1])); addr+=2; continue
        if ln.startswith('.long'):
            items.append(('long',addr,ln.split(None,1)[1])); addr+=4; continue
        items.append(('ins',addr,ln)); addr+=2
    # pool
    e=firstend if firstend is not None else addr
    poolbase=e+(e%4)
    pooladdr={}
    pidx=0
    for kind,a,ln in items:
        if kind=='ins':
            m=re.search(r'=(\S+?),',ln+',')
            if m and ('mov.l' in ln.split()[0] or 'mov.w' in ln.split()[0]):
                key=(ln.split()[0][-1],m.group(1))
                if key not in pooladdr:
                    pooladdr[key]=None
    # allocate: longs first then words
    cur=poolbase
    for key in list(pooladdr):
        if key[0]=='l': pooladdr[key]=cur; cur+=4
    for key in list(pooladdr):
        if key[0]=='w': pooladdr[key]=cur; cur+=2
    out={}
    def val(s):
        s=s.strip()
        if s in labels: return labels[s]
        if '-' in s and not s.startswith('-'):
            a,b=s.split('-',1); return val(a)-val(b)
        return num(s)
    for kind,a,ln in items:
        if kind=='raw': out[a]=ln
        elif kind=='word': out[a]=val(ln)&0xFFFF
        elif kind=='long': v=val(ln)&0xFFFFFFFF; out[a]=v>>16; out[a+2]=v&0xFFFF
        else: out[a]=enc(ln,a,labels,pooladdr,val)
    for key,pa in pooladdr.items():
        v=val(key[1])&0xFFFFFFFF
        if key[0]=='l': out[pa]=v>>16; out[pa+2]=v&0xFFFF
        else: out[pa]=v&0xFFFF
    return out
def enc(ln,a,labels,pooladdr,val):
    parts=ln.split(None,1); op=parts[0]; args=[x.strip() for x in re.split(r',(?![^()]*\))',parts[1])] if len(parts)>1 else []
    A=args
    def disp(s): return val(s)
    if op=='nop': return 0x0009
    if op=='rts': return 0x000B
    if op=='mov' :
        if A[0].startswith('#'): return 0xE000|reg(A[1])<<8|(num(A[0][1:])&0xFF)
        return 0x6003|reg(A[1])<<8|reg(A[0])<<4
    if op in('mov.l','mov.w') and A[0].startswith('='):
        pa=pooladdr[(op[-1],A[0][1:])]
        if op=='mov.l': d=(pa-((a&~3)+4))//4; return 0xD000|reg(A[1])<<8|d
        d=(pa-(a+4))//2; return 0x9000|reg(A[1])<<8|d
    sz={'b':0,'w':1,'l':2}
    m=re.fullmatch(r'mov\.([bwl])',op)
    if m:
        s=sz[m.group(1)]
        s0,s1=A
        mm=lambda x,p: re.fullmatch(p,x)
        if mm(s1,r'@-r(\d+)') : return 0x2004|s|reg('r'+mm(s1,r'@-r(\d+)').group(1))<<8|reg(s0)<<4
        if mm(s0,r'@r(\d+)\+'): return 0x6004|s|reg(s1)<<8|reg('r'+mm(s0,r'@r(\d+)\+').group(1))<<4
        if mm(s1,r'@\(r0,(r\d+)\)'): return 0x0004|s+0|reg(mm(s1,r'@\(r0,(r\d+)\)').group(1))<<8|reg(s0)<<4 if True else 0
        if mm(s0,r'@\(r0,(r\d+)\)'): return 0x000C|s|reg(s1)<<8|reg(mm(s0,r'@\(r0,(r\d+)\)').group(1))<<4
        x=mm(s1,r'@\(([^,]+),(r\d+)\)')
        if x:
            d=val(x.group(1)); n=reg(x.group(2))
            if m.group(1)=='l': return 0x1000|n<<8|reg(s0)<<4|(d//4)
            return 0x8000|(0 if m.group(1)=='b' else 0x100)|n<<4|(d//(1 if m.group(1)=='b' else 2)) if reg(s0)==0 else (_ for _ in ()).throw(Exception('r0 only'))
        x=mm(s0,r'@\(([^,]+),(r\d+)\)')
        if x:
            d=val(x.group(1)); n=reg(x.group(2))
            if m.group(1)=='l': return 0x5000|reg(s1)<<8|n<<4|(d//4)
            return 0x8400|(0 if m.group(1)=='b' else 0x100)|n<<4|(d//(1 if m.group(1)=='b' else 2))
        x=mm(s1,r'@(r\d+)')
        if x: return 0x2000|s|reg(x.group(1))<<8|reg(s0)<<4
        x=mm(s0,r'@(r\d+)')
        if x: return 0x6000|s|reg(s1)<<8|reg(x.group(1))<<4
        raise Exception('mov '+ln)
    bin3={'add':0x300C,'sub':0x3008,'and':0x2009,'or':0x200B,'xor':0x200A,'tst':0x2008,'cmp/eq':0x3000,'cmp/gt':0x3007,'cmp/ge':0x3003,'cmp/hi':0x3006,'cmp/hs':0x3002,'extu.b':0x600C,'extu.w':0x600D,'exts.b':0x600E,'exts.w':0x600F,'neg':0x600B,'not':0x6007,'mul.l':0x0007}
    if op=='add' and A[0].startswith('#'): return 0x7000|reg(A[1])<<8|(num(A[0][1:])&0xFF)
    if op=='cmp/eq' and A[0].startswith('#'): return 0x8800|(num(A[0][1:])&0xFF)
    if op=='tst' and A[0].startswith('#'): return 0xC800|(num(A[0][1:])&0xFF)
    if op=='and' and A[0].startswith('#'): return 0xC900|(num(A[0][1:])&0xFF)
    if op=='or' and A[0].startswith('#'): return 0xCB00|(num(A[0][1:])&0xFF)
    if op in bin3: return bin3[op]|reg(A[1])<<8|reg(A[0])<<4
    un={'shll':0x4000,'shlr':0x4001,'shll2':0x4008,'shll8':0x4018,'shlr2':0x4009,'shlr8':0x4019,'dt':0x4010,'cmp/pl':0x4015,'cmp/pz':0x4011,'shll16':0x4028,'shlr16':0x4029}
    if op in un: return un[op]|reg(A[0])<<8
    br={'bt':0x8900,'bf':0x8B00,'bt/s':0x8D00,'bf/s':0x8F00}
    if op in br:
        d=(val(A[0])-(a+4))//2; return br[op]|(d&0xFF)
    if op=='bra': d=(val(A[0])-(a+4))//2; return 0xA000|(d&0xFFF)
    if op=='bsr': d=(val(A[0])-(a+4))//2; return 0xB000|(d&0xFFF)
    if op=='braf': return 0x0023|reg(A[0])<<8
    if op=='bsrf': return 0x0003|reg(A[0])<<8
    if op=='jmp': return 0x402B|reg(A[0].strip('@'))<<8
    if op=='jsr': return 0x400B|reg(A[0].strip('@'))<<8
    if op=='mova':
        t=A[0].lstrip('@') ; d=(val(t)-((a&~3)+4))//4; return 0xC700|(d&0xFF)
    if op=='sts.l' : return 0x4F22
    if op=='lds.l' : return 0x4F26
    if op=='sts' : return 0x002A|reg(A[1])<<8
    if op=='lds' : return 0x402A|reg(A[0])<<8
    raise Exception('unknown '+ln)
if __name__=='__main__':
    for src in open(sys.argv[1]).read().split('\n==\n'):
        L=src.strip().split('\n')
        hdr=L[0]  # name|base|lend
        f=hdr.split('|'); name=f[0]; base=int(f[1],16); lend=f[2] if len(f)>2 else ''
        o=assemble(L[1:],base)
        ws=[]; cur=base
        for a in sorted(o):
            if a!=cur: ws.append('@%X'%a); cur=a
            ws.append('%04X'%(o[a]&0xFFFF)); cur=a+2
        print('%s|%X|%s|%s'%(name,base,' '.join(ws),lend))
