#!/usr/bin/env python3
"""Tiny SH-2 assembler for the area 5 review round (scratch tool, not part of the product).

usage: asm.py file.s   -> prints the hex words, for SE_GENERATED_WORDS (function at 0x06009000)
Syntax, one instruction per line, ';' comments, 'L:' labels:
  mov.l =0x06030000,r6      a literal-pool load (the pool follows the code; mov.w =x,rn too)
  mov #imm,rn   mov rm,rn   mov.b|w|l rm,@rn  /  @rm,rn  /  rm,@-rn  /  @rm+,rn
  mov.x rm,@(d,rn)  mov.x @(d,rm),rn  (d in bytes)   mov.x r0,@(d,gbr)  mov.x @(d,gbr),r0
  mov.x rm,@(r0,rn)  mov.x @(r0,rm),rn
  add rm,rn  add #i,rn  sub and or xor tst cmp/eq (rm,rn)  cmp/eq #i,r0  tst #i,r0 (as tst #i,r0)
  tas.b @rn  mac.l @rm+,@rn+  mac.w  dt rn  shll2 shll8 shlr2 shlr16 shll shlr  neg not extu.b extu.w exts.b exts.w movt rn
  ldc rm,gbr  stc gbr,rn  sett clrt  or.b|and.b|xor.b|tst.b #i,@(r0,gbr)
  bra/bsr/bt/bf/bt/s/bf/s label   jsr @rn  jmp @rn  rts  nop
"""
import re, sys

def reg(s):
    m = re.fullmatch(r'r(\d+)', s.strip())
    assert m, s
    return int(m.group(1))

def num(s):
    return int(s.strip().lstrip('#'), 0)

SZ = {'b': 0, 'w': 1, 'l': 2}

def assemble(text, base=0x06009000):
    lines = []
    for raw in text.split('\n'):
        ln = raw.split(';')[0].strip()
        if not ln:
            continue
        while ':' in ln.split()[0] if ln.split() else False:
            lab, _, rest = ln.partition(':')
            lines.append(('label', lab.strip()))
            ln = rest.strip()
            if not ln:
                break
        if ln:
            lines.append(('ins', ln))
    # pass 1: addresses
    labels = {}
    addr = 0
    insns = []
    for kind, v in lines:
        if kind == 'label':
            labels[v] = addr
        else:
            insns.append((addr, v))
            addr += 2
    codeend = addr
    pool = []   # (value, size)
    poolpos = {}
    words = []
    pend = []
    # pool starts at the next 4-aligned address after the code (code ends with a nop slot by the user)
    pstart = (codeend + 3) & ~3
    def pooladdr(val, size):
        key = (val, size)
        if key not in poolpos:
            poolpos[key] = len(pool)
            pool.append(key)
        return key
    # pre-scan pool uses to place entries (longs first, then words)
    for a, ln in insns:
        m = re.match(r'mov\.([lw])\s+=([^,]+),', ln)
        if m:
            pooladdr(num(m.group(2)), 4 if m.group(1) == 'l' else 2)
    layout = {}
    p = pstart
    for key in pool:
        if key[1] == 4:
            layout[key] = p
            p += 4
    for key in pool:
        if key[1] == 2:
            layout[key] = p
            p += 2
    out = []
    for a, ln in insns:
        op, _, args = ln.partition(' ')
        op = op.lower()
        args = args.strip()
        A = [x.strip() for x in re.split(r',(?![^(]*\))', args)] if args else []
        w = None
        if op in ('nop',): w = 0x0009
        elif op == '.word': w = num(A[0])
        elif op == 'sts.l' and A[0] == 'pr': w = 0x4022 | reg(A[1].strip('@-')) << 8
        elif op == 'lds.l' and A[1] == 'pr': w = 0x4026 | reg(A[0].strip('@+')) << 8
        elif op == 'rts': w = 0x000B
        elif op == 'sett': w = 0x0018
        elif op == 'clrt': w = 0x0008
        elif op in ('bra', 'bsr', 'bt', 'bf', 'bt/s', 'bf/s'):
            d = (labels[A[0]] - (a + 4)) // 2
            if op in ('bra', 'bsr'):
                w = (0xA000 if op == 'bra' else 0xB000) | (d & 0xFFF)
            else:
                base_ = {'bt': 0x8900, 'bf': 0x8B00, 'bt/s': 0x8D00, 'bf/s': 0x8F00}[op]
                w = base_ | (d & 0xFF)
        elif op == 'jsr': w = 0x400B | reg(A[0].strip('@')) << 8
        elif op == 'jmp': w = 0x402B | reg(A[0].strip('@')) << 8
        elif op == 'tas.b': w = 0x401B | reg(A[0].strip('@')) << 8
        elif op == 'dt': w = 0x4010 | reg(A[0]) << 8
        elif op == 'movt': w = 0x0029 | reg(A[0]) << 8
        elif op in ('shll', 'shlr', 'shll2', 'shlr2', 'shll8', 'shlr8', 'shll16', 'shlr16'):
            t = {'shll': 0x4000, 'shlr': 0x4001, 'shll2': 0x4008, 'shlr2': 0x4009, 'shll8': 0x4018, 'shlr8': 0x4019,
                 'shll16': 0x4028, 'shlr16': 0x4029}
            w = t[op] | reg(A[0]) << 8
        elif op in ('neg', 'not', 'extu.b', 'extu.w', 'exts.b', 'exts.w'):
            t = {'neg': 0x600B, 'not': 0x6007, 'extu.b': 0x600C, 'extu.w': 0x600D, 'exts.b': 0x600E, 'exts.w': 0x600F}
            w = t[op] | reg(A[1]) << 8 | reg(A[0]) << 4
        elif op == 'ldc' and A[1] == 'gbr': w = 0x401E | reg(A[0]) << 8
        elif op == 'ldc' and A[1] == 'sr': w = 0x400E | reg(A[0]) << 8
        elif op == 'ldc' and A[1] == 'vbr': w = 0x402E | reg(A[0]) << 8
        elif op == 'sleep': w = 0x001B
        elif op == 'trapa': w = 0xC300 | (num(A[0]) & 0xFF)
        elif op == 'ocbp' : w = 0
        elif op == 'stc' and A[0] == 'gbr': w = 0x0012 | reg(A[1]) << 8
        elif op == 'mac.l': w = 0x000F | reg(A[1].strip('@+')) << 8 | reg(A[0].strip('@+')) << 4
        elif op == 'mac.w': w = 0x400F | reg(A[1].strip('@+')) << 8 | reg(A[0].strip('@+')) << 4
        elif op in ('or.b', 'and.b', 'xor.b', 'tst.b'):
            t = {'tst.b': 0xCC00, 'and.b': 0xCD00, 'xor.b': 0xCE00, 'or.b': 0xCF00}
            w = t[op] | (num(A[0]) & 0xFF)
        elif op in ('add', 'sub', 'and', 'or', 'xor', 'tst', 'cmp/eq', 'cmp/hs', 'cmp/hi', 'cmp/gt', 'cmp/ge'):
            if A[0].startswith('#') or A[0][0].isdigit() or A[0][0] == '-':
                i = num(A[0])
                if op == 'add': w = 0x7000 | reg(A[1]) << 8 | (i & 0xFF)
                elif op == 'cmp/eq': w = 0x8800 | (i & 0xFF)
                elif op == 'tst': w = 0xC800 | (i & 0xFF)
                elif op == 'and': w = 0xC900 | (i & 0xFF)
                elif op == 'xor': w = 0xCA00 | (i & 0xFF)
                elif op == 'or': w = 0xCB00 | (i & 0xFF)
                else: raise Exception(ln)
            else:
                t = {'add': 0x300C, 'sub': 0x3008, 'and': 0x2009, 'or': 0x200B, 'xor': 0x200A, 'tst': 0x2008, 'cmp/eq': 0x3000,
                     'cmp/hs': 0x3002, 'cmp/ge': 0x3003, 'cmp/hi': 0x3006, 'cmp/gt': 0x3007}
                w = t[op] | reg(A[1]) << 8 | reg(A[0]) << 4
        elif op == 'mov':
            if A[0].startswith('#') or re.fullmatch(r'-?\d.*', A[0]):
                w = 0xE000 | reg(A[1]) << 8 | (num(A[0]) & 0xFF)
            else:
                w = 0x6003 | reg(A[1]) << 8 | reg(A[0]) << 4
        elif op.startswith('mov.'):
            sz = SZ[op[4]]
            s, d = A
            m = re.fullmatch(r'=(.+)', s)
            if m:
                key = (num(m.group(1)), 4 if sz == 2 else 2)
                tgt = layout[key]
                if sz == 2:
                    disp = (tgt - ((a + 4) & ~3)) // 4
                    w = 0xD000 | reg(d) << 8 | disp
                else:
                    disp = (tgt - (a + 4)) // 2
                    w = 0x9000 | reg(d) << 8 | disp
            elif re.fullmatch(r'@\(r0,r\d+\)', d):       # store r,@(r0,rn)
                w = 0x0004 | sz | reg(d[4:-1].split(',')[1]) << 8 | reg(s) << 4
            elif re.fullmatch(r'@\(r0,r\d+\)', s):
                w = 0x000C | sz | reg(d) << 8 | reg(s[1:].strip('()').split(',')[1]) << 4
            elif re.fullmatch(r'@\([^,]+,gbr\)', d):
                w = 0xC000 | sz << 8 | (num(d[2:].split(',')[0]) >> sz)
            elif re.fullmatch(r'@\([^,]+,gbr\)', s):
                w = 0xC400 | sz << 8 | (num(s[2:].split(',')[0]) >> sz)
            elif re.fullmatch(r'@\([^,]+,r\d+\)', d):    # store, displacement
                dd, rn = d[2:-1].split(',')
                dd = num(dd) >> sz
                if sz == 2: w = 0x1000 | reg(rn) << 8 | reg(s) << 4 | dd
                else: w = 0x8000 | sz << 8 | reg(rn) << 4 | dd
            elif re.fullmatch(r'@\([^,]+,r\d+\)', s):
                dd, rm = s[2:-1].split(',')
                dd = num(dd) >> sz
                if sz == 2: w = 0x5000 | reg(d) << 8 | reg(rm) << 4 | dd
                else: w = 0x8400 | sz << 8 | reg(rm) << 4 | dd
            elif d.startswith('@-'):
                w = 0x2004 | sz | reg(d[2:]) << 8 | reg(s) << 4
            elif d.startswith('@'):
                w = 0x2000 | sz | reg(d[1:]) << 8 | reg(s) << 4
            elif s.endswith('+') and s.startswith('@'):
                w = 0x6004 | sz | reg(d) << 8 | reg(s[1:-1]) << 4
            elif s.startswith('@'):
                w = 0x6000 | sz | reg(d) << 8 | reg(s[1:]) << 4
            else:
                raise Exception(ln)
        else:
            raise Exception('unknown ' + ln)
        out.append(w)
    # pool
    ws = list(out)
    while len(ws) * 2 < pstart:
        ws.append(0x0009)
    for key in pool:
        if key[1] == 4:
            ws.append(key[0] >> 16 & 0xFFFF)
            ws.append(key[0] & 0xFFFF)
    for key in pool:
        if key[1] == 2:
            ws.append(key[0] & 0xFFFF)
    return ws

if __name__ == '__main__':
    ws = assemble(open(sys.argv[1]).read())
    print(' '.join('%04X' % x for x in ws))
