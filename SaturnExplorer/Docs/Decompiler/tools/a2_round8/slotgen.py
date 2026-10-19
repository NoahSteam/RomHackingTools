#!/usr/bin/env python3
# Area 2 round 8 instrument: SE_AREA2_FIXTURES file: the slot loads of jmp, braf, jsr, bsrf and rte, both alignments (TestArea2Round8 holds a C++ copy of the builder).
# usage: slotgen.py > slots.txt; SE_AREA2_FIXTURES=slots.txt SaturnExplorerSh2DecompilerTests <scratch>
# Slot-load fixtures for jmp, jsr, braf, bsrf, rte: SE_AREA2_FIXTURES lines.
import sys
BASE = 0x06009000
def w(x): return "%04X" % x

def build(kind, slot, d, shift, talign, extra=None):
    """returns (name, words, flags, lend) ; byte offsets relative to BASE"""
    mem = {}
    pos = 0
    def put(off, word): mem[off] = word & 0xFFFF
    off = 0
    if shift:
        put(off, 0x0009); off += 2
    T = 0x28 + (2 if talign else 0)
    P = 0x20           # pool, 4-aligned
    def poolload(at, reg):
        base = (at & ~3) + 4
        assert P >= base and (P - base) % 4 == 0, (at, base)
        return 0xD000 | (reg << 8) | ((P - base) // 4)
    slotw = {
        'mova': 0xC700 | d,
        'movw0': 0x9000 | d,       # mov.w @(d,pc),r0
        'movl0': 0xD000 | d,       # mov.l @(d,pc),r0
        'movw4': 0x9400 | d,       # mov.w @(d,pc),r4
        'movl4': 0xD400 | d,       # mov.l @(d,pc),r4
    }[slot]
    lend = ""
    flags = "p" if False else ""
    if kind == 'jmp':
        put(off, poolload(off, 1)); off += 2
        put(off, 0x412B); off += 2
        put(off, slotw); off += 2
        pool = BASE + T
        put(T, 0x000B); put(T + 2, 0x6043 if slot in ('movw4','movl4') else 0x0009)
        if slot in ('movw4','movl4'): lend = "%X" % (BASE + T)
    elif kind == 'braf':
        braf_at = off + 2
        put(off, poolload(off, 1)); off += 2
        put(off, 0x0123); off += 2
        put(off, slotw); off += 2
        pool = T - (braf_at + 4)
        put(T, 0x000B); put(T + 2, 0x6043 if slot in ('movw4','movl4') else 0x0009)
        if slot in ('movw4','movl4'): lend = "%X" % (BASE + T)
    elif kind in ('jsr', 'bsrf'):
        put(off, 0x4F22); off += 2           # sts.l pr,@-r15
        put(off, poolload(off, 1)); off += 2
        at = off
        put(off, 0x410B if kind == 'jsr' else 0x0103); off += 2
        put(off, slotw); off += 2
        put(off, 0x4F26); off += 2           # lds.l @r15+,pr
        put(off, 0x000B); off += 2
        put(off, 0x0009); off += 2
        pool = (BASE + T) if kind == 'jsr' else (T - (at + 4))
        if slot in ('movw4', 'movl4'):
            put(T, 0x000B); put(T + 2, 0x6043)   # rts ; mov r4,r0
        else:
            put(T, 0x000B); put(T + 2, 0x0009)   # rts ; nop (void)
        lend = "%X" % (BASE + T)
    elif kind == 'rte_t':
        put(off, 0x4F03); off += 2           # stc.l sr,@-r15
        put(off, poolload(off, 1)); off += 2
        put(off, 0x2F16); off += 2           # mov.l r1,@-r15
        put(off, 0x002B); off += 2
        put(off, slotw); off += 2
        pool = BASE + T
        put(T, 0x000B); put(T + 2, 0x0009)
    elif kind == 'rte_s':
        put(off, 0x4F03); off += 2
        put(off, 0x4F22); off += 2
        put(off, 0x002B); off += 2
        put(off, slotw); off += 2
        pool = None
        put(T, 0x000B); put(T + 2, 0x0009)
    else:
        raise SystemExit(kind)
    if pool is not None:
        put(P, pool >> 16); put(P + 2, pool & 0xFFFF)
    # code beyond: nop padding up to 0x20 is implicit by filler (filler words are distinct data)
    # filler: distinct words
    top = 0x100
    words = []
    for o in range(0, top, 2):
        words.append(mem.get(o, 0x4000 + o // 2))
    # the instruction stream after the last code instruction up to P: nops (keep the straight line defined)
    for o in range(off, P, 2):
        if o not in mem: words[o // 2] = 0x0009
    name = "%s+%s d=%d shift=%d talign=%d" % (kind, slot, d, shift, talign)
    return name, words, lend

def main():
    out = []
    for kind in ('jmp', 'braf', 'jsr', 'bsrf', 'rte_t', 'rte_s'):
        slots = ['mova', 'movw0', 'movl0']
        if kind in ('jsr', 'bsrf'): slots = ['movw4', 'movl4', 'mova']
        if kind in ('jmp', 'braf'): slots = ['movw4', 'movl4']
        for slot in slots:
            for d in range(0, 7):
                for shift in (0, 1):
                    for talign in (0, 1):
                        if kind == 'rte_s' and talign: continue
                        name, words, lend = build(kind, slot, d, shift, talign)
                        flags = ("v" if slot == 'mova' and kind in ('jsr', 'bsrf') else "") + "pc"
                        out.append("%s|%s|0|%s|%s" % (name, " ".join(w(x) for x in words), flags, lend))
    print("\n".join(out))
main()
