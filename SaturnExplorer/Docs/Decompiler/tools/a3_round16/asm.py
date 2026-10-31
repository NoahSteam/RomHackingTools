#!/usr/bin/env python3
"""Tiny SH-2 assembler for fixtures: asm.py < source  -> hex words (code, pad, pool).
Syntax per line: label:  |  insn  |  .long N  (appended to pool in order; 'lit N' via mov.l #N,rX / mov.w pool)
mov.l =N,rX loads a pool long; mov.w =N,rX loads a pool word (N 16-bit, sign extended).
"""
import sys, re

def r(s):
    m = re.fullmatch(r'r(\d+)', s.strip())
    assert m, s
    return int(m.group(1))

def num(s):
    return int(s, 0)

def assemble(lines):
    code = []   # (kind, ...)
    labels = {}
    pool = []
    pool_idx = {}
    pending = []
    for ln in lines:
        ln = ln.split(';')[0].strip()
        if not ln: continue
        if ln.endswith(':'):
            labels[ln[:-1]] = len(code)
            continue
        code.append(ln)
    # layout: code words, align pool to 4, pool entries
    n = len(code)
    pool_start_words = n + (n & 1)
    # first pass: gather pool literals
    longs = []
    words = []
    for ln in code:
        m = re.fullmatch(r'mov\.l\s+=(\S+),\s*(r\d+)', ln)
        if m:
            v = num(m.group(1)) & 0xFFFFFFFF
            if v not in longs: longs.append(v)
        m = re.fullmatch(r'mov\.w\s+=(\S+),\s*(r\d+)', ln)
        if m:
            v = num(m.group(1)) & 0xFFFF
            if v not in words: words.append(v)
    # pool: words first (any alignment), then longs aligned
    pool_words = list(words)
    wpool_start = pool_start_words
    lpool_start = wpool_start + len(pool_words)
    if lpool_start & 1: lpool_start += 1
    out = []
    for i, ln in enumerate(code):
        pc = 2 * i
        def target(lbl):
            return labels[lbl]
        mm = None
        def L(pat):
            return re.fullmatch(pat, ln)
        m = L(r'mov\.l\s+=(\S+),\s*r(\d+)')
        if m:
            v = num(m.group(1)) & 0xFFFFFFFF
            addr = 2 * lpool_start + 4 * longs.index(v)
            disp = (addr - ((pc & ~3) + 4)) // 4
            assert 0 <= disp < 256, (ln, disp)
            out.append(0xD000 | (int(m.group(2)) << 8) | disp); continue
        m = L(r'mov\.w\s+=(\S+),\s*r(\d+)')
        if m:
            v = num(m.group(1)) & 0xFFFF
            addr = 2 * (wpool_start + pool_words.index(v))
            disp = (addr - (pc + 4)) // 2
            assert 0 <= disp < 256, (ln, disp)
            out.append(0x9000 | (int(m.group(2)) << 8) | disp); continue
        m = L(r'mov\s+#(\S+),\s*r(\d+)')
        if m:
            out.append(0xE000 | (int(m.group(2)) << 8) | (num(m.group(1)) & 0xFF)); continue
        m = L(r'add\s+#(\S+),\s*r(\d+)')
        if m:
            out.append(0x7000 | (int(m.group(2)) << 8) | (num(m.group(1)) & 0xFF)); continue
        m = L(r'mov\s+r(\d+),\s*r(\d+)')
        if m:
            out.append(0x6003 | (int(m.group(2)) << 8) | (int(m.group(1)) << 4)); continue
        sz = {'b': 0, 'w': 1, 'l': 2}
        m = L(r'mov\.([bwl])\s+r(\d+),\s*@r(\d+)')
        if m:
            out.append(0x2000 | (int(m.group(3)) << 8) | (int(m.group(2)) << 4) | sz[m.group(1)]); continue
        m = L(r'mov\.([bwl])\s+@r(\d+),\s*r(\d+)')
        if m:
            out.append(0x6000 | (int(m.group(3)) << 8) | (int(m.group(2)) << 4) | sz[m.group(1)]); continue
        m = L(r'mov\.([bwl])\s+@r(\d+)\+,\s*r(\d+)')
        if m:
            out.append(0x6004 + sz[m.group(1)] | (int(m.group(3)) << 8) | (int(m.group(2)) << 4)); continue
        m = L(r'mov\.([bwl])\s+r(\d+),\s*@-r(\d+)')
        if m:
            out.append(0x2004 + sz[m.group(1)] | (int(m.group(3)) << 8) | (int(m.group(2)) << 4)); continue
        m = L(r'mov\.([bwl])\s+@\(r0,r(\d+)\),\s*r(\d+)')
        if m:
            out.append(0x000C + sz[m.group(1)] | (int(m.group(3)) << 8) | (int(m.group(2)) << 4)); continue
        m = L(r'mov\.([bwl])\s+r(\d+),\s*@\(r0,r(\d+)\)')
        if m:
            out.append(0x0004 + sz[m.group(1)] | (int(m.group(3)) << 8) | (int(m.group(2)) << 4)); continue
        m = L(r'mov\.l\s+r(\d+),\s*@\((\d+),r(\d+)\)')
        if m:
            out.append(0x1000 | (int(m.group(3)) << 8) | (int(m.group(1)) << 4) | (int(m.group(2)) // 4)); continue
        m = L(r'mov\.l\s+@\((\d+),r(\d+)\),\s*r(\d+)')
        if m:
            out.append(0x5000 | (int(m.group(3)) << 8) | (int(m.group(2)) << 4) | (int(m.group(1)) // 4)); continue
        for name, base in (('add', 0x300C), ('and', 0x2009), ('or', 0x200B), ('xor', 0x200A), ('tst', 0x2008),
                           ('cmp/eq', 0x3000), ('cmp/hs', 0x3002), ('cmp/hi', 0x3006), ('sub', 0x3008), ('cmp/ge', 0x3003),
                           ('cmp/gt', 0x3007), ('mul.l', 0x0007), ('extu.b', 0x600C), ('extu.w', 0x600D), ('neg', 0x600B),
                           ('not', 0x6007), ('xtrct', 0x200D)):
            m = L(re.escape(name) + r'\s+r(\d+),\s*r(\d+)')
            if m:
                out.append(base | (int(m.group(2)) << 8) | (int(m.group(1)) << 4)); break
        else:
            m = None
        if len(out) > i: continue
        for name, base in (('dt', 0x4010), ('shll', 0x4000), ('shlr', 0x4001), ('shll2', 0x4008), ('shlr2', 0x4009), ('shll8', 0x4018),
                           ('shlr8', 0x4019), ('shll16', 0x4028), ('shlr16', 0x4029), ('jsr', 0x400B), ('jmp', 0x402B),
                           ('rotl', 0x4004), ('rotr', 0x4005), ('cmp/pl', 0x4015), ('cmp/pz', 0x4011)):
            m = L(re.escape(name) + r'\s+@?r(\d+)')
            if m:
                out.append(base | (int(m.group(1)) << 8)); break
        if len(out) > i: continue
        m = L(r'(bf|bt|bf/s|bt/s)\s+(\w+)')
        if m:
            base = {'bt': 0x8900, 'bf': 0x8B00, 'bt/s': 0x8D00, 'bf/s': 0x8F00}[m.group(1)]
            disp = target(m.group(2)) - (i + 2)
            assert -128 <= disp < 128
            out.append(base | (disp & 0xFF)); continue
        m = L(r'bra\s+(\w+)')
        if m:
            disp = target(m.group(1)) - (i + 2)
            out.append(0xA000 | (disp & 0xFFF)); continue
        m = L(r'tst\s+#(\S+),\s*r0')
        if m:
            out.append(0xC800 | (num(m.group(1)) & 0xFF)); continue
        m = L(r'and\s+#(\S+),\s*r0')
        if m:
            out.append(0xC900 | (num(m.group(1)) & 0xFF)); continue
        m = L(r'or\s+#(\S+),\s*r0')
        if m:
            out.append(0xCB00 | (num(m.group(1)) & 0xFF)); continue
        m = L(r'cmp/eq\s+#(\S+),\s*r0')
        if m:
            out.append(0x8800 | (num(m.group(1)) & 0xFF)); continue

        # ---- reviewer additions ----
        m = L(r'(addc|addv|subc|subv|swap\.b|swap\.w|exts\.b|exts\.w|negc|div1|cmp/str|mulu\.w|muls\.w|dmuls\.l|dmulu\.l|mov\.l_dummy)\s+r(\d+),\s*r(\d+)')
        if m:
            base = {'addc':0x300E,'addv':0x300F,'subc':0x300A,'subv':0x300B,'swap.b':0x6008,'swap.w':0x6009,'exts.b':0x600E,'exts.w':0x600F,
                    'negc':0x600A,'div1':0x3004,'cmp/str':0x200C,'mulu.w':0x200E,'muls.w':0x200F,'dmuls.l':0x300D,'dmulu.l':0x3005}[m.group(1)]
            out.append(base | (int(m.group(3)) << 8) | (int(m.group(2)) << 4)); continue
        m = L(r'(rotcl|rotcr|shar|shal|cmp/pl|cmp/pz|tas\.b_dummy)\s+r(\d+)')
        if m:
            base = {'rotcl':0x4024,'rotcr':0x4025,'shar':0x4021,'shal':0x4020,'cmp/pl':0x4015,'cmp/pz':0x4011}[m.group(1)]
            out.append(base | (int(m.group(2)) << 8)); continue
        m = L(r'(stc|sts)\s+(sr|gbr|vbr|mach|macl|pr),\s*r(\d+)')
        if m:
            base = {'sr':0x0002,'gbr':0x0012,'vbr':0x0022,'mach':0x000A,'macl':0x001A,'pr':0x002A}[m.group(2)]
            out.append(base | (int(m.group(3)) << 8)); continue
        m = L(r'(ldc|lds)\s+r(\d+),\s*(sr|gbr|vbr|mach|macl|pr)')
        if m:
            base = {'sr':0x400E,'gbr':0x401E,'vbr':0x402E,'mach':0x400A,'macl':0x401A,'pr':0x402A}[m.group(3)]
            out.append(base | (int(m.group(2)) << 8)); continue
        m = L(r'xor\s+#(\S+),\s*r0')
        if m:
            out.append(0xCA00 | (num(m.group(1)) & 0xFF)); continue
        m = L(r'mov\.([bwl])\s+@\((\d+),gbr\),\s*r0')
        if m:
            out.append(0xC400 | (sz[m.group(1)] << 8) | (int(m.group(2)) // {'b':1,'w':2,'l':4}[m.group(1)])); continue
        m = L(r'mov\.([bwl])\s+r0,\s*@\((\d+),gbr\)')
        if m:
            out.append(0xC000 | (sz[m.group(1)] << 8) | (int(m.group(2)) // {'b':1,'w':2,'l':4}[m.group(1)])); continue
        m = L(r'mov\.([bw])\s+@\((\d+),r(\d+)\),\s*r0')
        if m:
            out.append(0x8400 | ((sz[m.group(1)]) << 8) | (int(m.group(3)) << 4) | (int(m.group(2)) // {'b':1,'w':2}[m.group(1)])); continue
        m = L(r'mov\.([bw])\s+r0,\s*@\((\d+),r(\d+)\)')
        if m:
            out.append(0x8000 | ((sz[m.group(1)]) << 8) | (int(m.group(3)) << 4) | (int(m.group(2)) // {'b':1,'w':2}[m.group(1)])); continue
        m = L(r'mova\s+@\((\d+),pc\),\s*r0')
        if m:
            out.append(0xC700 | (int(m.group(1)) // 4)); continue
        m = L(r'(jsr|jmp|braf|bsrf)\s+@r(\d+)')
        if m:
            out.append({'jsr':0x400B,'jmp':0x402B,'braf':0x0023,'bsrf':0x0003}[m.group(1)] | (int(m.group(2)) << 8)); continue
        m = L(r'bsr\s+(\w+)')
        if m:
            disp = target(m.group(1)) - (i + 2)
            out.append(0xB000 | (disp & 0xFFF)); continue
        m = L(r'tas\.b\s+@r(\d+)')
        if m:
            out.append(0x401B | (int(m.group(1)) << 8)); continue
        m = L(r'(clrt|sett|clrmac|div0u|sleep|rte)')
        if m:
            out.append({'clrt':0x0008,'sett':0x0018,'clrmac':0x0028,'div0u':0x0019,'sleep':0x001B,'rte':0x002B}[m.group(1)]); continue
        m = L(r'mov\.l\s+r(\d+),\s*@\((\d+),r15\)')
        if m:
            out.append(0x1000 | (15 << 8) | (int(m.group(1)) << 4) | (int(m.group(2)) // 4)); continue
        m = L(r'cmp/eq\s+r(\d+),\s*r(\d+)')
        if m:
            out.append(0x3000 | (int(m.group(2)) << 8) | (int(m.group(1)) << 4)); continue
        m = L(r'mov\.([bwl])\s+@r(\d+)\+,\s*r(\d+)')
        if m:
            out.append(0x6004 + sz[m.group(1)] | (int(m.group(3)) << 8) | (int(m.group(2)) << 4)); continue
        m = L(r'mov\.([bwl])\s+@\(r0,r(\d+)\),\s*r(\d+)')
        if m:
            out.append(0x000C + sz[m.group(1)] | (int(m.group(3)) << 8) | (int(m.group(2)) << 4)); continue
        m = L(r'sub\s+r(\d+),\s*r(\d+)')
        if m:
            out.append(0x3008 | (int(m.group(2)) << 8) | (int(m.group(1)) << 4)); continue
        if ln == 'rts': out.append(0x000B); continue
        if ln == 'nop': out.append(0x0009); continue
        if ln == 'movt r0': out.append(0x0029); continue
        m = L(r'movt\s+r(\d+)')
        if m:
            out.append(0x0029 | (int(m.group(1)) << 8)); continue
        m = L(r'\.word\s+(\S+)')
        if m:
            out.append(num(m.group(1)) & 0xFFFF); continue
        raise SystemExit('cannot assemble: ' + ln)
    while len(out) < pool_start_words: out.append(0x0009)
    for w in pool_words: out.append(w)
    while len(out) < lpool_start: out.append(0x0009)
    for v in longs:
        out.append(v >> 16); out.append(v & 0xFFFF)
    return out

if __name__ == '__main__':
    src = sys.stdin.read().splitlines()
    print(' '.join('%04X' % w for w in assemble(src)))
